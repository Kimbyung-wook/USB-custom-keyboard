// JieLi USB_KEY adapter on a CH32V203 (draft 2026-11-15, built, never run).
// Puts a JieLi chip (JUZH_3B4 keyboard, AC6956C) into its ROM USB download
// mode (UBOOT) for the first install or a recovery, the way JieLi's USB
// updater dongle does:
//   1. route the target's D+/D- to this board (USB 2:1 analog switch, SEL pin)
//   2. power the target off and on (PWR pin, high-side switch on its VBUS)
//   3. send the 16-bit key 0x16EF again and again on D+/D- (one line clock,
//      one data, MSB first, data latched on the clock's rising edge, ~50 kHz)
//   4. the chip answers by pulling both lines low for 1-2 ms
//   5. route D+/D- back to the PC: the chip shows up as the "UBOOT" disk
// Sources: github.com/kagaimiq/jielie isp/usb/usb-key.md and
// github.com/kagaimiq/jl-uboot-tool docs/how-to-enter-uboot.md. The two
// disagree on which line is the clock, so ENTER tries both (order 0 = auto).
// The lines are open-drain with external pull-ups (4.7 kOhm or lower to
// 3.3 V): the chip's own ~15 kOhm pull-downs would beat the internal ones.
//
// The PC (GUI) drives it over vendor HID 1209:C560, 64-byte reports, header
// as docs/PROTOCOL.md (reply [0] = cmd | 0x80, [1] = sequence, [2] = status).
// Commands: see handle() below. Pins: pins.h.
#include "ch32v20x.h"
#include "usb_hid.h"
#include "pins.h"
#include <string.h>

#define VER_MAJOR 0
#define VER_MINOR 1

// ---- time: SysTick at HCLK/8 = 6 MHz ----
#define TICKS_US 6u
#define TICKS_MS 6000u
static uint32_t ticks(void) { return (uint32_t)SysTick->CNT; }
static void delay_ticks(uint32_t n) {
  uint32_t t0 = ticks();
  while (ticks() - t0 < n) {
  }
}
static void delay_us(uint32_t us) { delay_ticks(us * TICKS_US); }
static void delay_ms(uint32_t ms) {
  while (ms--) {
    delay_ticks(TICKS_MS);
    usb_poll(); // keep the PC side answering (the controller NAKs meanwhile)
  }
}

// ---- GPIO ----
// mode nibble: 0x2 push-pull out 2 MHz, 0x6 open-drain out 2 MHz, 0x4 floating in
static GPIO_TypeDef *port_of(uint8_t p) { return p == 0 ? GPIOA : p == 1 ? GPIOB : GPIOC; }
static void pin_mode(pin_t p, uint32_t mode) {
  GPIO_TypeDef *g = port_of(p.port);
  volatile uint32_t *cr = p.bit < 8 ? &g->CFGLR : &g->CFGHR;
  uint8_t sh = (p.bit & 7) * 4;
  *cr = (*cr & ~(0xFu << sh)) | (mode << sh);
}
static void pin_set(pin_t p, int high) { port_of(p.port)->BSHR = 1u << (p.bit + (high ? 0 : 16)); }
static int pin_get(pin_t p) { return (port_of(p.port)->INDR >> p.bit) & 1; }

static const pin_t pinDp = PIN_DP, pinDm = PIN_DM, pinSel = PIN_SEL, pinPwr = PIN_PWR;

// Key lines: open drain, 1 = released (pull-up makes it high)
static void lines_release(void) {
  pin_set(pinDp, 1);
  pin_set(pinDm, 1);
  pin_mode(pinDp, 0x6);
  pin_mode(pinDm, 0x6);
}
static void lines_float(void) { // not driven at all (routed to the PC)
  pin_mode(pinDp, 0x4);
  pin_mode(pinDm, 0x4);
}

static uint8_t routedToKey, powered;
static void route(int toKey) {
  routedToKey = toKey ? 1 : 0;
  pin_set(pinSel, toKey ? SEL_TO_KEY : !SEL_TO_KEY);
}
static void power(int on) {
  powered = on ? 1 : 0;
  pin_set(pinPwr, on ? PWR_ON : !PWR_ON);
}

// ---- USB_KEY ----
#define USB_KEY 0x16EF
// One key, MSB first: data set while the clock is low, latched on the rising edge
static void send_key(pin_t clk, pin_t dat, uint32_t halfUs) {
  for (int i = 15; i >= 0; i--) {
    pin_set(clk, 0);
    pin_set(dat, (USB_KEY >> i) & 1);
    delay_us(halfUs);
    pin_set(clk, 1);
    delay_us(halfUs);
  }
  pin_set(dat, 1);
}

// Both lines released; ack = both read low for at least ACK_MIN_US within ACK_WAIT_US
#define ACK_WAIT_US 3000u
#define ACK_MIN_US 300u
static int wait_ack(void) {
  uint32_t t0 = ticks(), low0 = 0;
  int low = 0;
  while (ticks() - t0 < ACK_WAIT_US * TICKS_US) {
    if (!pin_get(pinDp) && !pin_get(pinDm)) {
      if (!low)
        low = 1, low0 = ticks();
      else if (ticks() - low0 >= ACK_MIN_US * TICKS_US)
        return 1;
    } else {
      low = 0;
    }
  }
  return 0;
}

typedef struct {
  uint8_t acked, order;  // order 1: D+ clock, D- data; 2: D- clock, D+ data
  uint16_t keys;
  uint32_t ms;           // from power on (or start) to the ack
} enter_result_t;

// ENTER: off_ms 0 = no power cycle (the user turns the target on), timeout in ms,
// half period in us (0 = 10, 50 kHz), order 0 = auto (switch every 8 keys)
#define AUTO_SWITCH 8
static void enter(uint16_t offMs, uint16_t timeoutMs, uint8_t halfUs, uint8_t order, enter_result_t *r) {
  memset(r, 0, sizeof *r);
  if (!halfUs)
    halfUs = 10;
  if (!timeoutMs)
    timeoutMs = 3000;
  lines_release();
  route(1);
  if (offMs) {
    power(0);
    delay_ms(offMs);
  }
  power(1);
  uint32_t t0 = ticks(), limit = (uint32_t)timeoutMs * TICKS_MS;
  uint8_t cur = order ? order : 1;
  while (ticks() - t0 < limit) {
    if (cur == 1)
      send_key(pinDp, pinDm, halfUs);
    else
      send_key(pinDm, pinDp, halfUs);
    r->keys++;
    if (wait_ack()) {
      r->acked = 1;
      r->order = cur;
      r->ms = (ticks() - t0) / TICKS_MS;
      break;
    }
    if (!order && r->keys % AUTO_SWITCH == 0)
      cur = cur == 1 ? 2 : 1;
  }
  // Hand the lines to the PC either way (on a miss the target just boots)
  lines_float();
  delay_us(100);
  route(0);
}

// ---- commands ----
enum { ST_OK = 0, ST_BAD_CMD = 1, ST_BAD_ARG = 2 };
enum { C_HELLO = 0x01, C_POWER = 0x20, C_ROUTE = 0x21, C_ENTER = 0x22, C_LINES = 0x23 };

static uint16_t get16(const uint8_t *p) { return p[0] | (p[1] << 8); }
static void put16(uint8_t *p, uint16_t v) { p[0] = v, p[1] = v >> 8; }

static uint8_t handle(const uint8_t *rq, uint8_t *rs) {
  switch (rq[0]) {
  case C_HELLO:
    rs[3] = VER_MAJOR;
    rs[4] = VER_MINOR;
    strncpy((char *)rs + 5, "JL_USBKEY", 16);
    memcpy(rs + 21, (const void *)0x1FFFF7E8u, 12); // chip UID
    rs[33] = powered;
    rs[34] = routedToKey;
    return ST_OK;
  case C_POWER: // [2] 0 off, 1 on
    if (rq[2] > 1)
      return ST_BAD_ARG;
    power(rq[2]);
    return ST_OK;
  case C_ROUTE: // [2] 0 PC, 1 this board (lines released)
    if (rq[2] > 1)
      return ST_BAD_ARG;
    if (rq[2])
      lines_release();
    else
      lines_float();
    route(rq[2]);
    return ST_OK;
  case C_ENTER: { // [2..3] off ms, [4..5] timeout ms, [6] half period us, [7] order
    enter_result_t r;
    if (rq[7] > 2 || rq[6] > 100)
      return ST_BAD_ARG;
    enter(get16(rq + 2), get16(rq + 4), rq[6], rq[7], &r);
    rs[3] = r.acked;
    rs[4] = r.order;
    put16(rs + 5, r.keys);
    put16(rs + 7, r.ms > 0xFFFF ? 0xFFFF : (uint16_t)r.ms);
    return ST_OK;
  }
  case C_LINES: // levels now: [3] D+, [4] D-
    rs[3] = pin_get(pinDp);
    rs[4] = pin_get(pinDm);
    return ST_OK;
  default:
    return ST_BAD_CMD;
  }
}

int main(void) {
  SysTick->CTLR = 1; // enable, HCLK/8
  RCC->APB2PCENR |= RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB | RCC_APB2Periph_GPIOC | RCC_APB2Periph_AFIO;
  // Safe start: target on and routed to the PC (works like a plain USB cable)
  route(0);
  power(1);
  pin_mode(pinSel, 0x2);
  pin_mode(pinPwr, 0x2);
  lines_float();
  usb_init();
  static uint8_t rq[64], rs[64];
  while (1) {
    usb_poll();
    if (usb_recv(rq)) {
      memset(rs, 0, sizeof rs);
      rs[2] = handle(rq, rs);
      rs[0] = rq[0] | 0x80;
      rs[1] = rq[1];
      usb_send(rs);
    }
  }
}
