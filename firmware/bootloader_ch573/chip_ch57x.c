// CH573 chip layer of the bootloader (chip.h). Registers as in the SDK's
// CH573SFR.h; flash through the ROM ISP library (libISP573.a) with interrupts
// off (USB is interrupt-driven: usb_hid.c). The ms timer is TMR0 polled.
#include "CH57x_common.h"
#include "boot_layout.h"
#include "chip.h"

// ---- clock, time ----
static uint32_t ms;

void chip_init(void) {
  SetSysClock(CLK_SOURCE_PLL_60MHz);
  R8_TMR0_CTRL_MOD = RB_TMR_ALL_CLEAR;
  R32_TMR0_CNT_END = FREQ_SYS / 1000;
  R8_TMR0_INT_FLAG = RB_TMR_IF_CYC_END;
  R8_TMR0_CTRL_MOD = RB_TMR_COUNT_EN;
}

uint32_t chip_ms(void) {
  if (R8_TMR0_INT_FLAG & RB_TMR_IF_CYC_END) {
    R8_TMR0_INT_FLAG = RB_TMR_IF_CYC_END;
    ms++;
  }
  return ms;
}

void chip_delay_ms(uint32_t n) {
  uint32_t t0 = chip_ms();
  while (chip_ms() - t0 < n) {
  }
}

// ---- enter mark ----
int chip_take_mark(void) {
  int stay = R8_GLOB_RESET_KEEP == BOOT_MARK_ENTER;
  R8_GLOB_RESET_KEEP = 0;
  return stay;
}

// ---- GPIO: port registers DIR, PIN, OUT, CLR, PU, PD_DRV in a row ----
enum { G_DIR, G_PIN, G_OUT, G_CLR, G_PU, G_PD };
static volatile uint32_t *gpio(uint8_t port) { return port ? &R32_PB_DIR : &R32_PA_DIR; }

// back to the reset state: input, no pull, output latch 0
__attribute__((unused)) static void pin_reset(uint8_t port, uint8_t bit) {
  volatile uint32_t *g = gpio(port);
  uint32_t m = 1u << bit;
  g[G_DIR] &= ~m;
  g[G_PU] &= ~m;
  g[G_PD] &= ~m;
  g[G_CLR] = m;
}

__attribute__((unused)) static void pin_out(uint8_t port, uint8_t bit, int high) {
  volatile uint32_t *g = gpio(port);
  uint32_t m = 1u << bit;
  if (high)
    g[G_OUT] |= m;
  else
    g[G_CLR] = m;
  g[G_PD] &= ~m; // 5 mA drive
  g[G_DIR] |= m;
}

#ifdef BOARD_BOOT_KEY_PIN
static const uint8_t key_pin[2] = {BOARD_BOOT_KEY_PIN};
#endif
#ifdef BOARD_BOOT_KEY_DRIVE
static const uint8_t key_drive[2] = {BOARD_BOOT_KEY_DRIVE};
#endif

// Pressed = low (pull-up), 20 reads 1 ms apart; pins back to reset state after
int chip_boot_key(void) {
#ifdef BOARD_BOOT_KEY_PIN
  volatile uint32_t *g = gpio(key_pin[0]);
  uint32_t m = 1u << key_pin[1];
  int held = 1;
#ifdef BOARD_BOOT_KEY_DRIVE
  pin_out(key_drive[0], key_drive[1], 0);
#endif
  g[G_DIR] &= ~m;
  g[G_PD] &= ~m;
  g[G_PU] |= m;
  chip_delay_ms(1);
  for (int i = 0; i < 20 && held; i++) {
    if (g[G_PIN] & m)
      held = 0;
    chip_delay_ms(1);
  }
  pin_reset(key_pin[0], key_pin[1]);
#ifdef BOARD_BOOT_KEY_DRIVE
  pin_reset(key_drive[0], key_drive[1]);
#endif
  return held;
#else
  return 0;
#endif
}

void chip_led(int on) {
#ifdef BOARD_BOOT_LED_PINS
  static const uint8_t led[][2] = {BOARD_BOOT_LED_PINS};
  for (unsigned i = 0; i < sizeof led / sizeof led[0]; i++)
    pin_out(led[i][0], led[i][1], BOARD_BOOT_LED_HIGH ? on : !on);
#else
  (void)on;
#endif
}

// ---- flash ----
void chip_uid(uint8_t out[8]) {
  static __attribute__((aligned(4))) uint8_t id[8];
  uint32_t irq;
  SYS_DisableAllIrq(&irq);
  FLASH_EEPROM_CMD(CMD_GET_UNIQUE_ID, 0, id, 0);
  SYS_RecoverIrq(irq);
  for (int i = 0; i < 8; i++)
    out[i] = id[i];
}

// Interrupts off: the USB handler must not run while the ROM library works
// on the flash (USB NAKs meanwhile)
int chip_flash_erase(uint32_t addr, uint32_t len) {
  uint32_t irq;
  SYS_DisableAllIrq(&irq);
  int ok = FLASH_ROM_ERASE(addr, len) == 0;
  SYS_RecoverIrq(irq);
  return ok;
}

int chip_flash_write(uint32_t addr, const void *buf, uint32_t len) {
  uint32_t irq;
  SYS_DisableAllIrq(&irq);
  int ok = FLASH_ROM_WRITE(addr, (void *)buf, len) == 0 &&
           FLASH_ROM_VERIFY(addr, (void *)buf, len) == 0;
  SYS_RecoverIrq(irq);
  return ok;
}

// ---- leave ----
void chip_jump_app(void) {
  R8_TMR0_CTRL_MOD = RB_TMR_ALL_CLEAR;
  R8_TMR0_INT_FLAG = 0xFF;
  // MIE off (called before usb_init: no interrupt was enabled); MPP = 3: the app's start-up code ends
  // with mret into its main, which then stays in machine mode (the SDK's
  // start-up sets only MIE/MPIE, our own mret into main left MPP = user)
  __asm volatile("csrci mstatus, 0x8\n li t0, 0x1800\n csrs mstatus, t0" ::: "t0");
  ((void (*)(void))BOOT_APP_ADDR)();     // the app's _start: j handle_reset
  while (1) {
  }
}

void chip_reset(void) {
  SYS_ResetExecute();
  while (1) {
  }
}

// ---- diagnostic build (BL_USB_DIAG=1): SAYO_2B3 status LEDs, high = on ----
#if BL_USB_DIAG
// L3 PA4 stall, L4 PA5 setup, L6 PA14 address, L7 PA13 config, L8 PA12 report
static const uint8_t diag_led[5] = {4, 5, 14, 13, 12};

void chip_diag_leds(uint8_t bits) {
  for (int i = 0; i < 5; i++)
    pin_out(0, diag_led[i], (bits >> i) & 1);
}

int chip_diag_key5(void) {
  uint32_t m = 1u << 10; // PA10
  R32_PA_DIR &= ~m;
  R32_PA_PD_DRV &= ~m;
  R32_PA_PU |= m;
  chip_delay_ms(2);
  return !(R32_PA_PIN & m);
}

void chip_diag_save(const uint8_t *buf, uint32_t len) {
  static __attribute__((aligned(4))) uint8_t page[256];
  uint32_t irq;
  for (uint32_t off = 0; off < len; off += 256) {
    for (int i = 0; i < 256; i++)
      page[i] = buf[off + i];
    SYS_DisableAllIrq(&irq);
    EEPROM_ERASE(off, 256);
    EEPROM_WRITE(off, page, 256);
    SYS_RecoverIrq(irq);
  }
}

// A trap: all six LEDs on, L5 blinking stops
__attribute__((interrupt("WCH-Interrupt-fast"), section(".highcode"))) void HardFault_Handler(void) {
  R32_PA_OUT |= (1u << 4) | (1u << 5) | (1u << 15) | (1u << 14) | (1u << 13) | (1u << 12);
  R32_PA_DIR |= (1u << 4) | (1u << 5) | (1u << 15) | (1u << 14) | (1u << 13) | (1u << 12);
  while (1) {
  }
}

__attribute__((interrupt("WCH-Interrupt-fast"), section(".highcode"))) void NMI_Handler(void) {
  R32_PA_OUT |= 1u << 4; // NMI: L3 too, then as a trap
  HardFault_Handler();
}
#endif
