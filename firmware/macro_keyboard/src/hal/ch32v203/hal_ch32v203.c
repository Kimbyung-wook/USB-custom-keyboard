// RISC-V (CH32V20x) only: the Arduino (CH552, SDCC) build compiles every file under src/
#ifdef __riscv
// CH32V203C8U6 implementation of src/hal/hal.h (see hal_ch32v203.h).
#include <string.h>
#include "ch32v20x.h"
#include "ch32v20x_gpio.h"
#include "ch32v20x_rcc.h"
#include "ch32v20x_misc.h"
#include "ch32v20x_tim.h"
#include "ch32v20x_flash.h"
#include "debug.h"
#include "ch32v20x_usbfs_device.h"
#include "hal/hal.h"
#include "core/led.h"

#define NO_PORT 0xFF

static GPIO_TypeDef *portPtr(uint8_t port) {
  return port == 0 ? GPIOA : (port == 1 ? GPIOB : GPIOC);
}

static const uint8_t keyPins[BOARD_KEYS][2] = {BOARD_KEY_PINS};
static const uint8_t ledPins[BOARD_LED_ROWS][3][2] = {BOARD_LED_PINS};

static void pinMode(uint8_t port, uint8_t bit, GPIOMode_TypeDef mode) {
  GPIO_InitTypeDef init;
  init.GPIO_Pin = (uint16_t)(1u << bit);
  init.GPIO_Speed = GPIO_Speed_50MHz;
  init.GPIO_Mode = mode;
  GPIO_Init(portPtr(port), &init);
}

// ---- time: TIM2 counts milliseconds (SDK Delay_* reset SysTick) ----
// 16-bit counter at 1 kHz wraps at 65536 ms, same as hal_ms(): no call-rate
// limit (flash writes and long USB waits cannot lose time)
uint16_t hal_ms(void) { return (uint16_t)TIM2->CNT; }

// ---- keys ----
hal_sw_t hal_switches(void) {
  hal_sw_t sw = 0;
  for (uint8_t i = 0; i < BOARD_KEYS; i++) {
    if (!(portPtr(keyPins[i][0])->INDR & (1u << keyPins[i][1]))) {
      sw |= (hal_sw_t)(1u << i);
    }
  }
  return sw;
}

// ---- LEDs: software PWM on TIM3 ----
// Each row has an R, G and B line shared by its 3 keys, low = lit. 16 levels
// at 1 kHz. Full scale (palette value 40) differs per color because the green
// LED is much brighter than red and blue.
#define PWM_STEPS 16
#define PALETTE_FULL 40
static const uint8_t fullDuty[3] = {12, 3, 8}; // R, G, B at palette value 40
static volatile uint8_t duty[3];
static uint8_t tick;

void TIM3_IRQHandler(void) __attribute__((interrupt("machine")));
void TIM3_IRQHandler(void) {
  uint8_t t = tick;

  TIM3->INTFR = (uint16_t)~TIM_IT_Update;
  for (uint8_t row = 0; row < BOARD_LED_ROWS; row++) {
    for (uint8_t c = 0; c < 3; c++) {
      uint8_t port = ledPins[row][c][0];
      uint32_t m;
      if (port == NO_PORT) {
        continue;
      }
      m = 1u << ledPins[row][c][1];
      if (t < duty[c]) {
        portPtr(port)->BCR = m; // lit
      } else {
        portPtr(port)->BSHR = m;
      }
    }
  }
  tick = (uint8_t)((t + 1) & (PWM_STEPS - 1));
}

void hal_leds_all(uint8_t color) {
  for (uint8_t c = 0; c < 3; c++) {
    duty[c] = (uint8_t)((hal_palette[color][c] * fullDuty[c] + PALETTE_FULL / 2) / PALETTE_FULL);
  }
}

static void ledInit(void) {
  TIM_TimeBaseInitTypeDef tb;
  NVIC_InitTypeDef nv;

  for (uint8_t row = 0; row < BOARD_LED_ROWS; row++) {
    for (uint8_t c = 0; c < 3; c++) {
      if (ledPins[row][c][0] != NO_PORT) {
        GPIO_WriteBit(portPtr(ledPins[row][c][0]), (uint16_t)(1u << ledPins[row][c][1]), Bit_SET);
        pinMode(ledPins[row][c][0], ledPins[row][c][1], GPIO_Mode_Out_PP);
      }
    }
  }
  RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);
  tb.TIM_Period = 62 - 1; // 1 MHz / 62 = 16 kHz
  tb.TIM_Prescaler = 48 - 1;
  tb.TIM_ClockDivision = TIM_CKD_DIV1;
  tb.TIM_CounterMode = TIM_CounterMode_Up;
  tb.TIM_RepetitionCounter = 0;
  TIM_TimeBaseInit(TIM3, &tb);
  TIM_ITConfig(TIM3, TIM_IT_Update, ENABLE);
  nv.NVIC_IRQChannel = TIM3_IRQn;
  nv.NVIC_IRQChannelPreemptionPriority = 1;
  nv.NVIC_IRQChannelSubPriority = 1;
  nv.NVIC_IRQChannelCmd = ENABLE;
  NVIC_Init(&nv);
  TIM_Cmd(TIM3, ENABLE);
}

// ---- config storage: last BLOB_CAPACITY bytes of flash, RAM shadow ----
// Erased flash reads 0xE339E339 (not 0xFF): storage.c sees an invalid blob and
// uses the defaults. The Makefile keeps the image below STORE_BASE.
#define FLASH_END 0x08010000u // CH32V203C8: 64 KB code flash
#define PAGE_SIZE 256u        // fast erase/program page
#define STORE_BASE (FLASH_END - BLOB_CAPACITY)
#if BLOB_CAPACITY % 256
#error BLOB_CAPACITY must be whole 256-byte pages
#endif
static uint32_t shadow32[BLOB_CAPACITY / 4]; // word aligned for page programming
#define shadow ((uint8_t *)shadow32)

uint8_t hal_store_read(blob_pos_t addr) { return shadow[addr]; }
void hal_store_write(blob_pos_t addr, uint8_t value) { shadow[addr] = value; }

uint8_t hal_store_flush(void) {
  // Rewrite only the pages that changed (fewer erase cycles, shorter pause)
  for (uint32_t off = 0; off < BLOB_CAPACITY; off += PAGE_SIZE) {
    uint32_t addr = STORE_BASE + off;
    if (memcmp(shadow + off, (const void *)addr, PAGE_SIZE) == 0) {
      continue;
    }
    // No interrupt may run (and fetch from flash) mid erase/program, as in the
    // SDK FLASH example. A few ms per page: LED PWM pauses, USB NAKs meanwhile.
    __disable_irq();
    FLASH_Unlock_Fast();
    FLASH_ErasePage_Fast(addr);
    FLASH_ProgramPage_Fast(addr, shadow32 + off / 4);
    FLASH_Lock_Fast();
    __enable_irq();
  }
  if (memcmp(shadow, (const void *)STORE_BASE, BLOB_CAPACITY) == 0) {
    return 1;
  }
  // Failed: make the shadow match what the next boot will read
  memcpy(shadow, (const void *)STORE_BASE, BLOB_CAPACITY);
  return 0;
}

// ---- USB ----
uint8_t hal_res[64];

uint8_t kb_ready(void) { return USBFS_DevEnumStatus; }
uint8_t kb_busy(void) { return USBFS_Endp_Busy[DEF_UEP1]; }

uint8_t kb_send(uint8_t modifiers, uint8_t keycode) {
  uint8_t report[8] = {modifiers, 0, keycode, 0, 0, 0, 0, 0};
  return USBFS_Endp_DataUp(DEF_UEP1, report, 8, DEF_UEP_CPY_LOAD) == 0;
}

void kb_cancel(void) {
  USBFSD->UEP1_TX_CTRL = (USBFSD->UEP1_TX_CTRL & ~USBFS_UEP_T_RES_MASK) | USBFS_UEP_T_RES_NAK;
  USBFS_Endp_Busy[DEF_UEP1] = 0;
}

// Send the response, then accept the next request (EP2 OUT is NAKed while a
// request is being handled)
void vendor_send(void) {
  USBFS_Endp_DataUp(DEF_UEP2, hal_res, 64, DEF_UEP_CPY_LOAD);
  USBFS_Vendor_Req = 0;
  USBFSD->UEP2_RX_CTRL = (USBFSD->UEP2_RX_CTRL & ~USBFS_UEP_R_RES_MASK) | USBFS_UEP_R_RES_ACK;
}

// ---- system ----
void hal_init(void) {
  TIM_TimeBaseInitTypeDef tb;

  RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOA |
                             RCC_APB2Periph_GPIOB | RCC_APB2Periph_GPIOC,
                         ENABLE);
  // Frees PA13/PA14 (SWD) and PA15 (JTAG) for keys and LEDs
  GPIO_PinRemapConfig(GPIO_Remap_SWJ_Disable, ENABLE);
  NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);

  RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);
  tb.TIM_Period = 0xFFFF;
  tb.TIM_Prescaler = 48000 - 1; // 48 MHz / 48000 = 1 kHz
  tb.TIM_ClockDivision = TIM_CKD_DIV1;
  tb.TIM_CounterMode = TIM_CounterMode_Up;
  tb.TIM_RepetitionCounter = 0;
  TIM_TimeBaseInit(TIM2, &tb);
  TIM_Cmd(TIM2, ENABLE);

  Delay_Init(); // the USB driver uses Delay_Us/Delay_Ms

  for (uint8_t i = 0; i < BOARD_KEYS; i++) {
    pinMode(keyPins[i][0], keyPins[i][1], GPIO_Mode_IPU);
  }
  memcpy(shadow, (const void *)STORE_BASE, BLOB_CAPACITY);
  ledInit();
}

void hal_usb_start(void) {
  USBFS_RCC_Init();
  USBFS_Device_Init(ENABLE);
}

void hal_enter_bootloader(void) {
  uint16_t t0 = hal_ms();
  while ((uint16_t)(hal_ms() - t0) < 300) {
  }
  *(volatile uint32_t *)0xE000E048u = 0xBEEF0080u; // PFIC CFGR: key + SYSRESET
  while (1) {
  }
}

uint8_t hal_chip_uid(uint8_t i) { return ((volatile uint8_t *)0x1FFFF7E8u)[i]; }

#endif // __riscv
