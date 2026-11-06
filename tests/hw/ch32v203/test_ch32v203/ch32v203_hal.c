// Chip layer for test_ch32v203: CH32V203C8U6 on the WCH CH32V20x EVT SDK.
// Portable test logic is in test_ch32v203.c; the interface is ch32v203_hal.h.
#include <stdint.h>
#include <string.h>
#include "ch32v20x.h"
#include "ch32v20x_gpio.h"
#include "ch32v20x_rcc.h"
#include "ch32v20x_misc.h"
#include "ch32v20x_tim.h"
#include "system_ch32v20x.h"
#include "debug.h"
#include "ch32v20x_usbfs_device.h"
#include "ch32v203_hal.h"

// Port 0 = A, 1 = B, 2 = C
static GPIO_TypeDef *portPtr(uint8_t port) {
  return port == 0 ? GPIOA : (port == 1 ? GPIOB : GPIOC);
}

// ---- time ----
// TIM2 runs at 1 MHz and is read by polling. The SDK's Delay_Us/Delay_Ms
// (used inside the USB driver) reset and stop SysTick, so SysTick is not used.
// TIM2 wraps every 65.5 ms; hal_millis() must run at least that often (the
// main loop does).

static uint16_t lastCnt;
static uint32_t usRem;
static uint32_t msAcc;

uint32_t hal_millis(void) {
  uint16_t now = TIM2->CNT;
  usRem += (uint16_t)(now - lastCnt); // unsigned: handles one wrap
  lastCnt = now;
  msAcc += usRem / 1000;
  usRem %= 1000;
  return msAcc;
}

void hal_delay_ms(uint32_t ms) {
  uint32_t t0 = hal_millis();
  while (hal_millis() - t0 < ms) {
  }
}

uint8_t hal_reset_cause(void) {
  uint8_t c = 0;
  if (RCC_GetFlagStatus(RCC_FLAG_PINRST)) c |= 1;
  if (RCC_GetFlagStatus(RCC_FLAG_PORRST)) c |= 2;
  if (RCC_GetFlagStatus(RCC_FLAG_SFTRST)) c |= 4;
  if (RCC_GetFlagStatus(RCC_FLAG_IWDGRST)) c |= 8;
  if (RCC_GetFlagStatus(RCC_FLAG_WWDGRST)) c |= 16;
  if (RCC_GetFlagStatus(RCC_FLAG_LPWRRST)) c |= 32;
  RCC_ClearFlag();
  return c;
}

// ---- fault record ----
// The SDK HardFault handler resets the chip. The Makefile makes it call
// hal_fault_record() first; the values sit in RAM above .bss and below the stack.
#define FLT ((volatile uint32_t *)0x20001800u)
#define FLT_MAGIC 0xF17E5EADu

void hal_fault_record(void) {
  uint32_t a, b, c;
  __asm__ volatile("csrr %0, mcause" : "=r"(a));
  __asm__ volatile("csrr %0, mepc" : "=r"(b));
  __asm__ volatile("csrr %0, mtval" : "=r"(c));
  FLT[1] = a;
  FLT[2] = b;
  FLT[3] = c;
  FLT[0] = FLT_MAGIC;
}

uint8_t hal_fault_read(uint32_t out[3]) {
  if (FLT[0] != FLT_MAGIC) {
    return 0;
  }
  out[0] = FLT[1];
  out[1] = FLT[2];
  out[2] = FLT[3];
  FLT[0] = 0;
  return 1;
}

// ---- GPIO ----

static void gpioSetup(uint8_t port, uint8_t bit, GPIOMode_TypeDef mode) {
  GPIO_InitTypeDef init;
  init.GPIO_Pin = (uint16_t)(1u << bit);
  init.GPIO_Speed = GPIO_Speed_50MHz;
  init.GPIO_Mode = mode;
  GPIO_Init(portPtr(port), &init);
}

void hal_gpio_input_pullup(uint8_t port, uint8_t bit) {
  gpioSetup(port, bit, GPIO_Mode_IPU);
}

void hal_gpio_output(uint8_t port, uint8_t bit) {
  gpioSetup(port, bit, GPIO_Mode_Out_PP);
}

void hal_gpio_write(uint8_t port, uint8_t bit, uint8_t high) {
  GPIO_WriteBit(portPtr(port), (uint16_t)(1u << bit), high ? Bit_SET : Bit_RESET);
}

uint8_t hal_gpio_read(uint8_t port, uint8_t bit) {
  return GPIO_ReadInputDataBit(portPtr(port), (uint16_t)(1u << bit)) == Bit_SET;
}

// ---- WS2812 ----
// Bit-banged with deadlines on the SysTick counter, which counts HCLK (48 MHz)
// when CTLR = enable | HCLK clock source. The first version counted loop
// iterations; the LEDs then showed a fixed green, so the timing was off.
// Interrupts are masked while the frame is sent. SDK Delay_Us/Delay_Ms
// reprogram SysTick, so it is set up again at every call.
#define STK_CTLR (*(volatile uint32_t *)0xE000F000u)
#define STK_CNTL (*(volatile uint32_t *)0xE000F008u)
#define WS_PERIOD 60u    // 1.25 us per bit
#define WS_T1H 34u       // ~0.71 us high for a 1 bit
#define WS_T0H 15u       // ~0.31 us high for a 0 bit
#define WS_RESET 14400u  // 300 us low latches the frame

static inline void waitUntil(uint32_t start, uint32_t t) {
  while ((uint32_t)(STK_CNTL - start) < t) {
  }
}

void hal_ws2812_show(uint8_t port, uint8_t bit, const uint8_t *grb, uint16_t len) {
  GPIO_TypeDef *g = portPtr(port);
  uint32_t mask = 1u << bit;
  uint32_t saved;
  uint32_t start;
  uint32_t next = 0;

  // QingKe V4 has no immediate CSR forms (csrrci is an illegal instruction) and
  // keeps the global interrupt enable in CSR 0x800, as the SDK does
  __asm__ volatile("csrr %0, 0x800" : "=r"(saved));
  __asm__ volatile("csrc 0x800, %0" : : "r"(0x88));

  STK_CTLR = 5; // enable, HCLK, counting up
  start = STK_CNTL;
  for (uint16_t i = 0; i < len; i++) {
    uint8_t b = grb[i];
    for (int k = 7; k >= 0; k--) {
      waitUntil(start, next);
      g->BSHR = mask;
      waitUntil(start, next + ((b & (1u << k)) ? WS_T1H : WS_T0H));
      g->BCR = mask;
      next += WS_PERIOD;
    }
  }
  waitUntil(start, next + WS_RESET);

  if (saved & 8) {
    __asm__ volatile("csrs 0x800, %0" : : "r"(0x88));
  }
}

// ---- USB HID keyboard ----

#define HID_MOD_SHIFT 0x02

// Send one 8-byte boot keyboard report on EP1 IN. Waits up to 50 ms for the
// endpoint to be free, then gives up (keys are dropped if the host is absent).
static void usbSend(const uint8_t *report) {
  uint32_t t0;

  if (!USBFS_DevEnumStatus) {
    return;
  }
  t0 = hal_millis();
  while (USBFS_Endp_Busy[DEF_UEP1]) {
    if (hal_millis() - t0 > 50) {
      return;
    }
  }
  USBFS_Endp_DataUp(DEF_UEP1, (uint8_t *)report, 8, DEF_UEP_CPY_LOAD);
}

// ASCII to HID usage (US layout). Returns 0 if the character has no key.
static uint8_t keyFor(char c, uint8_t *mod) {
  *mod = 0;
  if (c >= 'a' && c <= 'z') return (uint8_t)(0x04 + (c - 'a'));
  if (c >= 'A' && c <= 'Z') {
    *mod = HID_MOD_SHIFT;
    return (uint8_t)(0x04 + (c - 'A'));
  }
  if (c >= '1' && c <= '9') return (uint8_t)(0x1E + (c - '1'));
  if (c == '0') return 0x27;
  switch (c) {
    case '\n': return 0x28; // Enter
    case ' ':  return 0x2C;
    case '-':  return 0x2D;
    case '.':  return 0x37;
    case ':':  *mod = HID_MOD_SHIFT; return 0x33;
    case '?':  *mod = HID_MOD_SHIFT; return 0x38;
    default:   return 0;
  }
}

void hal_hid_write(char c) {
  uint8_t report[8];
  uint8_t mod;
  uint8_t key = keyFor(c, &mod);

  if (!key) {
    return;
  }
  memset(report, 0, sizeof(report));
  report[0] = mod;
  report[2] = key;
  usbSend(report);      // press
  memset(report, 0, sizeof(report));
  usbSend(report);      // release
}

void hal_hid_print(const char *s) {
  while (*s) {
    hal_hid_write(*s++);
  }
}

// ---- init ----

void hal_init(void) {
  TIM_TimeBaseInitTypeDef tb;

  RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOA |
                             RCC_APB2Periph_GPIOB | RCC_APB2Periph_GPIOC,
                         ENABLE);
  // Full SWJ off: frees PA13/PA14 (SWD) and PA15/PB3/PB4 (JTAG).
  // Debugging by SWD is then not possible; reflash via wchisp.
  GPIO_PinRemapConfig(GPIO_Remap_SWJ_Disable, ENABLE);

  NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);

  // TIM2: 48 MHz / 48 = 1 MHz, free running up-count, 16 bits
  RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);
  tb.TIM_Period = 0xFFFF;
  tb.TIM_Prescaler = 48 - 1;
  tb.TIM_ClockDivision = TIM_CKD_DIV1;
  tb.TIM_CounterMode = TIM_CounterMode_Up;
  TIM_TimeBaseInit(TIM2, &tb);
  TIM_Cmd(TIM2, ENABLE);
  lastCnt = TIM2->CNT;

  // The SDK USB driver calls Delay_Us/Delay_Ms, which need this first
  Delay_Init();

  USBFS_RCC_Init();
  USBFS_Device_Init(ENABLE);
}

// Plain chip reset (not recorded as a fault)
void hal_reset(void) {
  *(volatile uint32_t *)0xE000E048u = 0xBEEF0080u; // PFIC CFGR: key + SYSRESET
  while (1) {}
}

// Leave the application and start the WCH ISP bootloader in system flash (0x1FFF8000).
// Not documented for CH32V20x: this is an experiment. USB is dropped first so the host
// sees a disconnect, then clocks and peripherals are put back to the reset state.
void hal_jump_bootloader(void) {
  uint32_t i;

  __asm__ volatile("csrc 0x800, %0" ::"r"(0x88)); // global interrupts off
  USBFSD->BASE_CTRL = 0;
  USBFSD->UDEV_CTRL = 0;
  hal_gpio_output(0, 11);
  hal_gpio_output(0, 12);
  hal_gpio_write(0, 11, 0);
  hal_gpio_write(0, 12, 0);
  hal_delay_ms(100); // host sees the device go away
  for (i = 0; i < 8; i++) {
    NVIC->IRER[i] = 0xFFFFFFFFu;
  }
  RCC->APB2PRSTR = 0xFFFFFFFFu;
  RCC->APB2PRSTR = 0;
  RCC->APB1PRSTR = 0xFFFFFFFFu;
  RCC->APB1PRSTR = 0;
  RCC->CFGR0 = 0;          // back to HSI
  RCC->CTLR = 0x00000083u; // HSION, reset trim
  __asm__ volatile("li t0, 0x1FFF8000\n\tjr t0");
  while (1) {}
}
