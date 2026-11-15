// JieLi USB_KEY sender on a Blue Pill (STM32F103C8), bare metal, no HAL.
// Draft 2026-11-15: built, never run.
//
// Puts a JieLi chip (JUZH_3B4, AC6321A / BD19) into its ROM USB download
// mode (UBOOT): right after reset it sends the 16-bit key 0x16EF on D+/D-
// again and again for SEND_MS (one line clock, one data, MSB first, data
// latched on the clock's rising edge, ~50 kHz). The JUZH's USB cable stays
// plugged into the PC, so the ROM sees the PC's SOF once it has the key.
// Logic: ../jl_usbkey_ch32v203/main.c (ENTER, order 0 = auto) without the
// analog switch, power switch and USB. Sources: kagaimiq/jielie
// isp/usb/usb-key.md, kagaimiq/jl-uboot-tool how-to-enter-uboot.md.
//
// LED PC13: on while sending, then slow blink (double flash = ack sensed,
// a hint only, see config.h). Reset button = start again.
#include <stdint.h>
#include "pins.h"
#include "config.h"

// ---- registers (RM0008) ----
#define REG(a) (*(volatile uint32_t *)(a))
#define RCC_CR REG(0x40021000u)
#define RCC_CFGR REG(0x40021004u)
#define RCC_APB2ENR REG(0x40021018u)
#define FLASH_ACR REG(0x40022000u)
#define SYST_CSR REG(0xE000E010u)
#define SYST_RVR REG(0xE000E014u)
#define SYST_CVR REG(0xE000E018u)

typedef struct {
  volatile uint32_t CRL, CRH, IDR, ODR, BSRR, BRR, LCKR;
} gpio_t;
static gpio_t *port_of(uint8_t p) { return (gpio_t *)(0x40010800u + 0x400u * p); } // A, B, C

typedef struct {
  uint8_t port, bit;
} pin_t;
static const pin_t pinDp = PIN_DP, pinDm = PIN_DM, pinLed = PIN_LED;

// ---- clock: HSE 8 MHz x9 = 72 MHz, else HSI/2 x16 = 64 MHz ----
static uint32_t tpu; // SysTick ticks (= HCLK cycles) per us

static int wait_bit(uint32_t mask) {
  for (uint32_t n = 0; n < 200000u; n++)
    if (RCC_CR & mask)
      return 1;
  return 0;
}

static void clock_init(void) {
  FLASH_ACR = 0x12; // prefetch on, 2 wait states (needed above 48 MHz)
  RCC_CR |= 1u << 16; // HSEON
  int hse = wait_bit(1u << 17); // HSERDY
  uint32_t cfgr = 4u << 8; // APB1 = HCLK/2 (max 36 MHz), APB2 = HCLK
  if (hse)
    cfgr |= (1u << 16) | (7u << 18); // PLL from HSE, x9
  else {
    RCC_CR &= ~(1u << 16);
    cfgr |= (14u << 18); // PLL from HSI/2, x16
  }
  RCC_CFGR = cfgr;
  RCC_CR |= 1u << 24; // PLLON
  if (!wait_bit(1u << 25)) { // no PLL: stay on HSI 8 MHz
    tpu = 8;
    return;
  }
  RCC_CFGR = cfgr | 2u; // SW = PLL
  while (((RCC_CFGR >> 2) & 3u) != 2u) {
  }
  tpu = hse ? 72 : 64;
}

// ---- time: SysTick free-running at HCLK, extended to 32 bits ----
// Call ticks() at least every 0x1000000 cycles (233 ms at 72 MHz): every
// wait here does. 32 bits wrap after 59 s at 72 MHz (unsigned diffs only).
static uint32_t lastCvr, acc;
static void time_init(void) {
  SYST_RVR = 0xFFFFFFu;
  SYST_CVR = 0;
  SYST_CSR = 5; // enable, HCLK, no interrupt
  lastCvr = SYST_CVR;
}
static uint32_t ticks(void) {
  uint32_t c = SYST_CVR;
  acc += (lastCvr - c) & 0xFFFFFFu; // counts down
  lastCvr = c;
  return acc;
}
static void delay_us(uint32_t us) {
  uint32_t t0 = ticks(), n = us * tpu;
  while (ticks() - t0 < n) {
  }
}
static void delay_ms(uint32_t ms) {
  while (ms--)
    delay_us(1000);
}

// ---- GPIO ----
// mode nibble: 0x3 push-pull out 50 MHz, 0x2 push-pull out 2 MHz,
// 0x4 floating in, 0x8 in with pull (ODR 1 = up)
static void pin_mode(pin_t p, uint32_t mode) {
  gpio_t *g = port_of(p.port);
  volatile uint32_t *cr = p.bit < 8 ? &g->CRL : &g->CRH;
  uint8_t sh = (p.bit & 7) * 4;
  *cr = (*cr & ~(0xFu << sh)) | (mode << sh);
}
static void pin_set(pin_t p, int high) { port_of(p.port)->BSRR = 1u << (p.bit + (high ? 0 : 16)); }
static int pin_get(pin_t p) { return (port_of(p.port)->IDR >> p.bit) & 1; }
static void led(int on) { pin_set(pinLed, on ? LED_ON : !LED_ON); }

static void lines_drive_high(void) { // ODR first, so they come out high
  pin_set(pinDp, 1);
  pin_set(pinDm, 1);
  pin_mode(pinDp, 0x3);
  pin_mode(pinDm, 0x3);
}
static void lines_release(void) { // input, internal pull-up
  pin_set(pinDp, 1);
  pin_set(pinDm, 1);
  pin_mode(pinDp, 0x8);
  pin_mode(pinDm, 0x8);
}
static void lines_float(void) { // not driven, no pull: hand the lines to the PC
  pin_mode(pinDp, 0x4);
  pin_mode(pinDm, 0x4);
}

// ---- USB_KEY ----
// One key, MSB first: data set while the clock is low, latched on the rising edge
static void send_key(pin_t clk, pin_t dat) {
  lines_drive_high();
  for (int i = 15; i >= 0; i--) {
    pin_set(clk, 0);
    pin_set(dat, (USB_KEY >> i) & 1);
    delay_us(HALF_US);
    pin_set(clk, 1);
    delay_us(HALF_US);
  }
  pin_set(dat, 1);
}

// Gap after a key, ACK_WAIT_US long; returns 1 if both lines read low for
// at least ACK_MIN_US (only with GAP_RELEASE). With ACK_STOPS 0 the gap
// always lasts the full ACK_WAIT_US, ack or not.
static int gap(void) {
#if GAP_RELEASE
  lines_release();
  uint32_t t0 = ticks(), low0 = 0;
  int low = 0, acked = 0;
  while (ticks() - t0 < ACK_WAIT_US * tpu) {
    if (!pin_get(pinDp) && !pin_get(pinDm)) {
      if (!low)
        low = 1, low0 = ticks();
      else if (!acked && ticks() - low0 >= ACK_MIN_US * tpu) {
        acked = 1;
        if (ACK_STOPS)
          break;
      }
    } else {
      low = 0;
    }
  }
  return acked;
#else
  delay_us(ACK_WAIT_US); // lines stay driven high
  return 0;
#endif
}

int main(void) {
  clock_init();
  time_init();
  RCC_APB2ENR |= (1u << 2) | (1u << 3) | (1u << 4); // GPIOA, B, C
  pin_set(pinLed, !LED_ON);
  pin_mode(pinLed, 0x2);
  lines_float();

  // Send for SEND_MS after reset (clock line swaps every AUTO_SWITCH keys)
  led(1);
  uint32_t t0 = ticks(), keys = 0, acks = 0;
  uint8_t cur = FIRST_CLK;
  while (ticks() - t0 < SEND_MS * 1000u * tpu) {
    if (cur == 1)
      send_key(pinDp, pinDm);
    else
      send_key(pinDm, pinDp);
    keys++;
    if (gap()) {
      acks++;
      if (ACK_STOPS)
        break;
    }
    if (keys % AUTO_SWITCH == 0)
      cur = cur == 1 ? 2 : 1;
  }
  lines_float(); // done: D+/D- belong to the PC again
  led(0);

  // Slow blink forever; double flash if an ack was sensed
  while (1) {
    if (acks) {
      led(1), delay_ms(150), led(0), delay_ms(150);
      led(1), delay_ms(150), led(0), delay_ms(1550);
    } else {
      led(1), delay_ms(BLINK_MS), led(0), delay_ms(BLINK_MS);
    }
  }
}
