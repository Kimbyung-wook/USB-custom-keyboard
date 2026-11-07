// CH32V203 bootloader for the macro keyboard (docs/BOOTLOADER_CH32V203.md).
// Stays when key 7 (PB2) is held at power-up, the app left the RAM mark, or the
// app info is missing/bad; otherwise jumps to the app at 0x08002000.
// While staying: all LEDs purple, vendor HID 1209:C55E (HELLO/ERASE/WRITE/CHECK/RUN).
#include "ch32v20x.h"
#include "ch32v20x_flash.h"
#include "boot_layout.h"
#include "usb_hid.h"
#include <string.h>

#define BL_VER_MAJOR 1
#define BL_VER_MINOR 0
#ifndef BOARD_NAME
#define BOARD_NAME "3X3V_RGB"
#endif

// ---- time: SysTick at HCLK/8 = 6 MHz ----
#define TICKS_MS 6000u
static uint32_t ticks(void) { return (uint32_t)SysTick->CNT; }
static void delay_ms(uint32_t ms) {
  uint32_t t0 = ticks();
  while (ticks() - t0 < ms * TICKS_MS) {
  }
}

// ---- GPIO ----
// mode nibble: 0x2 = push-pull output 2 MHz, 0x8 = input with pull
static void pin_mode(GPIO_TypeDef *g, uint8_t pin, uint32_t mode) {
  volatile uint32_t *cr = pin < 8 ? &g->CFGLR : &g->CFGHR;
  uint8_t sh = (pin & 7) * 4;
  *cr = (*cr & ~(0xFu << sh)) | (mode << sh);
}

// 3X3V_RGB LEDs (src/boards/3x3v_rgb.h): row cathodes on PA (low = lit),
// per-key anodes on PB (high = lit). Red and blue together show only red, so
// purple alternates them.
static const uint8_t led_red[3] = {11, 8, 0}, led_green[3] = {12, 9, 1},
                     led_blue[3] = {13, 10, 2};
static const uint8_t led_anode[9] = {3, 4, 9, 14, 5, 8, 13, 12, 0};
#define KEY7_PIN 2 // PB2

static void led_init(void) {
  // SWJ off: PA13 (blue row 1) and PB3/PB4 (anodes) are debug pins
  AFIO->PCFR1 = (AFIO->PCFR1 & ~(7u << 24)) | (4u << 24);
  for (int r = 0; r < 3; r++) {
    GPIOA->BSHR = (1u << led_red[r]) | (1u << led_green[r]) | (1u << led_blue[r]);
    pin_mode(GPIOA, led_red[r], 0x2);
    pin_mode(GPIOA, led_green[r], 0x2);
    pin_mode(GPIOA, led_blue[r], 0x2);
  }
  for (int k = 0; k < 9; k++) {
#ifdef DIAG
    GPIOB->BSHR = 1u << (led_anode[k] + (k ? 16 : 0)); // key 1 only
#else
    GPIOB->BSHR = 1u << led_anode[k];
#endif
    pin_mode(GPIOB, led_anode[k], 0x2);
  }
}

// phase 0 = off, 1 = red, 2 = blue, 3 = green
static void led_phase(int phase) {
  uint32_t on = 0, all = 0;
  for (int r = 0; r < 3; r++) {
    all |= (1u << led_red[r]) | (1u << led_blue[r]) | (1u << led_green[r]);
    if (phase == 1)
      on |= 1u << led_red[r];
    if (phase == 2)
      on |= 1u << led_blue[r];
    if (phase == 3)
      on |= 1u << led_green[r];
  }
  GPIOA->BSHR = (all & ~on) | (on << 16); // set = off, reset = lit
}

static uint32_t busy_t; // last erase/write: blink for a while after it
static int busy_seen;

#ifdef DIAG
// USB step as a colour (two phases alternate): 0 red = no bus reset,
// 1 blue = reset only, 2 green = some transfer, 3 yellow = SETUP,
// 4 cyan = address set, 5 purple = configured
extern volatile uint8_t usb_stage;
static const uint8_t diag_colors[6][2] = {{1, 1}, {2, 2}, {3, 3}, {1, 3}, {3, 2}, {1, 2}};
#endif

static void led_update(void) {
  uint32_t t = ticks();
  int phase = (t / (2 * TICKS_MS)) & 1 ? 2 : 1; // swap every 2 ms
#ifdef DIAG
  // only key 1 lit (led_init), 0.5 s on / 0.5 s off: little LED current, and
  // the blink rate shows whether the clock is right (2 s period = half speed)
  phase = (t / (500 * TICKS_MS)) & 1 ? 0 : diag_colors[usb_stage][(t / (2 * TICKS_MS)) & 1];
  led_phase(phase);
  return;
#endif
  if (busy_seen && t - busy_t < 300 * TICKS_MS && (t / (100 * TICKS_MS)) & 1)
    phase = 0;
  led_phase(phase);
}

// ---- CRC32 (zlib) ----
static uint32_t crc32(const uint8_t *p, uint32_t n) {
  static const uint32_t t[16] = {
      0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC, 0x76DC4190, 0x6B6B51F4,
      0x4DB26158, 0x5005713C, 0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C,
      0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C};
  uint32_t c = 0xFFFFFFFFu;
  while (n--) {
    c ^= *p++;
    c = (c >> 4) ^ t[c & 15];
    c = (c >> 4) ^ t[c & 15];
  }
  return ~c;
}

static const boot_info_t *const info = (const boot_info_t *)BOOT_INFO_ADDR;

static int app_valid(void) {
  if (info->magic != BOOT_INFO_MAGIC ||
      info->check != (~BOOT_INFO_MAGIC ^ info->length ^ info->crc32))
    return 0;
  if (info->length < 8 || info->length > BOOT_APP_MAX)
    return 0;
  return crc32((const uint8_t *)BOOT_APP_ADDR, info->length) == info->crc32;
}

// ---- start-up decision ----
static int key7_held(void) {
  RCC->APB2PCENR |= RCC_APB2Periph_GPIOB;
  GPIOB->BSHR = 1u << KEY7_PIN; // pull-up
  pin_mode(GPIOB, KEY7_PIN, 0x8);
  delay_ms(1);
  for (int i = 0; i < 20; i++) {
    if (GPIOB->INDR & (1u << KEY7_PIN))
      return 0;
    delay_ms(1);
  }
  return 1;
}

static void jump_app(void) {
  SysTick->CTLR = 0;
  RCC->APB2PRSTR = RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB | RCC_APB2Periph_AFIO;
  RCC->APB2PRSTR = 0;
  RCC->APB2PCENR = 0;
  __asm volatile("li t0, 0x8\n csrc mstatus, t0" ::: "t0"); // MIE off
  ((void (*)(void))(BOOT_APP_ADDR - BOOT_FLASH_BASE))(); // linked at 0x00002000
}

static void sys_reset(void) {
  PFIC->CFGR = 0xBEEF0080u; // key + SYSRESET
  while (1) {
  }
}

// ---- flash ----
static __attribute__((aligned(4))) uint8_t page[BOOT_PAGE_SIZE];
static uint32_t page_addr; // 0 = no page pending
static int erased;

static int program_page(uint32_t addr, const uint8_t *buf) {
  FLASH_Unlock_Fast();
  FLASH_ErasePage_Fast(addr);
  FLASH_ProgramPage_Fast(addr, (uint32_t *)buf);
  FLASH_Lock_Fast();
  return memcmp((const void *)addr, buf, BOOT_PAGE_SIZE) == 0;
}

static int flush_page(void) {
  int ok = 1;
  if (page_addr)
    ok = program_page(page_addr, page);
  page_addr = 0;
  return ok;
}

// Info page first (so a cut-off update stays in the bootloader), then the app
// area in 4 KB sectors: 0x08002000..0x0800EFFF (the last one holds the info page)
static int erase_app(void) {
  memset(page, 0xFF, sizeof page);
  if (!program_page(BOOT_INFO_ADDR, page))
    return 0;
  FLASH_Unlock();
  int ok = 1;
  for (uint32_t a = BOOT_APP_ADDR; a < BOOT_STORE_ADDR; a += 0x1000)
    if (FLASH_ErasePage(a) != FLASH_COMPLETE)
      ok = 0;
  FLASH_Lock();
  page_addr = 0;
  return ok;
}

// ---- commands (64-byte reports; header as docs/PROTOCOL.md) ----
enum { ST_OK = 0, ST_BAD_CMD = 1, ST_BAD_ARG = 2, ST_BAD_CRC = 3, ST_FLASH_ERR = 5 };
enum { C_HELLO = 0x01, C_ERASE = 0x10, C_WRITE = 0x11, C_CHECK = 0x12, C_RUN = 0x13 };
#define WRITE_MAX 56 // data bytes 8..63

static uint32_t get32(const uint8_t *p) {
  return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void put32(uint8_t *p, uint32_t v) {
  p[0] = v, p[1] = v >> 8, p[2] = v >> 16, p[3] = v >> 24;
}

static uint8_t handle(const uint8_t *rq, uint8_t *rs) {
  switch (rq[0]) {
  case C_HELLO: {
    rs[3] = BL_VER_MAJOR;
    rs[4] = BL_VER_MINOR;
    strncpy((char *)rs + 5, BOARD_NAME, 16);
    memcpy(rs + 21, (const void *)0x1FFFF7E8u, 12); // chip UID
    put32(rs + 33, BOOT_APP_ADDR);
    put32(rs + 37, BOOT_APP_MAX);
    rs[41] = BOOT_PAGE_SIZE & 0xFF, rs[42] = BOOT_PAGE_SIZE >> 8;
    rs[43] = app_valid();
    rs[44] = WRITE_MAX;
    return ST_OK;
  }
  case C_ERASE:
    busy_t = ticks(), busy_seen = 1;
    erased = erase_app();
    return erased ? ST_OK : ST_FLASH_ERR;
  case C_WRITE: {
    uint32_t addr = get32(rq + 2), n = rq[6];
    if (!erased || n == 0 || n > WRITE_MAX || addr < BOOT_APP_ADDR ||
        addr + n > BOOT_INFO_ADDR || (addr % BOOT_PAGE_SIZE) + n > BOOT_PAGE_SIZE)
      return ST_BAD_ARG;
    busy_t = ticks(), busy_seen = 1;
    uint32_t pa = addr & ~(BOOT_PAGE_SIZE - 1);
    if (pa != page_addr) {
      if (!flush_page())
        return ST_FLASH_ERR;
      memset(page, 0xFF, sizeof page);
      page_addr = pa;
    }
    memcpy(page + (addr - pa), rq + 8, n);
    return ST_OK;
  }
  case C_CHECK: {
    uint32_t len = get32(rq + 2), crc = get32(rq + 6);
    if (!flush_page())
      return ST_FLASH_ERR;
    if (len < 8 || len > BOOT_APP_MAX)
      return ST_BAD_ARG;
    uint32_t got = crc32((const uint8_t *)BOOT_APP_ADDR, len);
    put32(rs + 3, got);
    if (got != crc)
      return ST_BAD_CRC;
    memset(page, 0xFF, sizeof page);
    boot_info_t *bi = (boot_info_t *)page;
    bi->magic = BOOT_INFO_MAGIC;
    bi->length = len;
    bi->crc32 = crc;
    bi->check = ~BOOT_INFO_MAGIC ^ len ^ crc;
    if (!program_page(BOOT_INFO_ADDR, page))
      return ST_FLASH_ERR;
    erased = 0;
    return ST_OK;
  }
  case C_RUN:
    return ST_OK; // reset after the reply went out (main loop)
  default:
    return ST_BAD_CMD;
  }
}

int main(void) {
  SysTick->CTLR = 1; // enable, HCLK/8
  volatile uint32_t *mark = (volatile uint32_t *)BOOT_MARK_ADDR;
  int stay = *mark == BOOT_MARK_ENTER;
  *mark = 0;
  if (!stay)
    stay = key7_held();
  if (!stay && app_valid())
    jump_app();

  RCC->APB2PCENR |= RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB | RCC_APB2Periph_AFIO;
  led_init();
  usb_init();
  static uint8_t rq[64], rs[64];
  int run = 0;
  uint32_t run_t = 0;
  while (1) {
    usb_poll();
    led_update();
    if (!run && usb_recv(rq)) {
      memset(rs, 0, sizeof rs);
      rs[2] = handle(rq, rs);
      rs[0] = rq[0] | 0x80;
      rs[1] = rq[1];
      usb_send(rs);
      if (rq[0] == C_RUN && rs[2] == ST_OK)
        run = 1, run_t = ticks();
    }
    // RUN: let the reply go out, then detach and reset into the app
    if (run && !usb_tx_busy() && ticks() - run_t > 20 * TICKS_MS) {
      usb_stop();
      delay_ms(200); // the host must see the device leave (same as the app's BOOT)
      sys_reset();
    }
  }
}
