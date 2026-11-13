// CH573 bootloader for the macro keyboard: the CH32V203 bootloader's commands
// (firmware/bootloader_ch32v203, docs/BOOTLOADER_CH32V203.md) on the CH57x
// chip layer (chip.h) and flash layout (boot_layout.h).
// Stays when the boot key (board file BOARD_BOOT_KEY_PIN) is held at power-on,
// the app left the enter mark (BOOT command), or the app info is missing/bad;
// otherwise jumps to the app at BOOT_APP_ADDR (0x3000).
// While staying: vendor HID 1209:C55E (HELLO/ERASE/WRITE/CHECK/RUN), the
// board's LED (if any) blinks.
// Differences from the CH32V203 one: flash erases in 4 KB blocks, so ERASE
// clears only the info block and WRITE erases each app block when it first
// reaches it (ERASE answers at once, whatever the app area size). HELLO [45]
// is the chip ID (0x73), the UID is 8 bytes (+ 4 zero).
#include "boot_layout.h"
#include "chip.h"
#include "usb_hid.h"
#include <string.h>

#define BL_VER_MAJOR 1
#define BL_VER_MINOR 0
#ifndef BOARD_NAME
#error "board file needs BOARD_NAME"
#endif

// ---- LED: 1 s blink while waiting, fast for a while after erase/write ----
static uint32_t busy_t;
static int busy_seen;

static void led_update(void) {
  uint32_t t = chip_ms();
  int on = (t / 500) & 1;
  if (busy_seen && t - busy_t < 300)
    on = (t / 50) & 1;
  chip_led(on);
#if BL_USB_DIAG
  chip_diag_leds(usb_diag);
#endif
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

// ---- flash: one 256-byte page buffered, app blocks erased on first use ----
static __attribute__((aligned(4))) uint8_t page[BOOT_PAGE_SIZE];
static uint32_t page_addr;  // 0 = no page pending
static uint32_t erase_next; // first app block not erased since ERASE
static int erased;          // ERASE done, CHECK not yet

static int flush_page(void) {
  int ok = 1;
  if (page_addr) {
    while (ok && erase_next <= page_addr) {
      ok = chip_flash_erase(erase_next, BOOT_BLOCK_SIZE);
      erase_next += BOOT_BLOCK_SIZE;
    }
    if (ok)
      ok = chip_flash_write(page_addr, page, BOOT_PAGE_SIZE);
  }
  page_addr = 0;
  return ok;
}

// Info block first (a cut-off update then stays in the bootloader)
static int erase_app(void) {
  page_addr = 0;
  erase_next = BOOT_APP_ADDR;
  return chip_flash_erase(BOOT_INFO_ADDR, BOOT_BLOCK_SIZE);
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
    chip_uid(rs + 21); // [29..32] stay 0
    put32(rs + 33, BOOT_APP_ADDR);
    put32(rs + 37, BOOT_APP_MAX);
    rs[41] = BOOT_PAGE_SIZE & 0xFF, rs[42] = BOOT_PAGE_SIZE >> 8;
    rs[43] = app_valid();
    rs[44] = WRITE_MAX;
    rs[45] = CHIP_ID;
    return ST_OK;
  }
  case C_ERASE:
    busy_t = chip_ms(), busy_seen = 1;
    erased = erase_app();
    return erased ? ST_OK : ST_FLASH_ERR;
  case C_WRITE: {
    uint32_t addr = get32(rq + 2), n = rq[6];
    if (!erased || n == 0 || n > WRITE_MAX || addr < BOOT_APP_ADDR ||
        addr + n > BOOT_APP_ADDR + BOOT_APP_MAX || (addr % BOOT_PAGE_SIZE) + n > BOOT_PAGE_SIZE)
      return ST_BAD_ARG;
    busy_t = chip_ms(), busy_seen = 1;
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
    if (!erased || len < 8 || len > BOOT_APP_MAX || BOOT_APP_ADDR + len > erase_next)
      return ST_BAD_ARG; // not all of it written since ERASE
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
    if (!chip_flash_write(BOOT_INFO_ADDR, page, BOOT_PAGE_SIZE))
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
  chip_init();
  int stay = chip_take_mark();
  if (!stay)
    stay = chip_boot_key();
  if (!stay && app_valid())
    chip_jump_app();

#if BL_USB_DIAG
  // Diagnostic build: key 5 also held = variant (usb_hid.c usb_variant)
  usb_variant = (uint8_t)chip_diag_key5();
  usb_log[0] = 'U', usb_log[1] = 'S', usb_log[2] = 'B', usb_log[3] = 'L', usb_log[4] = 'O',
  usb_log[5] = 'G', usb_log[6] = usb_variant, usb_log[9] = USB_LOG_REC;
  uint16_t saved = 0;
#endif
  usb_init();
  static uint8_t rq[64], rs[64];
  int run = 0;
  uint32_t run_t = 0;
  while (1) {
    led_update();
#if BL_USB_DIAG
    // Log to Data-Flash once the host has been quiet for 2 s
    if (usb_log_n != saved && chip_ms() - usb_log_ms > 2000) {
      saved = usb_log_n;
      usb_log[7] = (uint8_t)saved, usb_log[8] = (uint8_t)(saved >> 8);
      chip_diag_save(usb_log, USB_LOG_SIZE);
    }
#endif
    if (!run && usb_recv(rq)) {
      memset(rs, 0, sizeof rs);
      rs[2] = handle(rq, rs);
      rs[0] = rq[0] | 0x80;
      rs[1] = rq[1];
      usb_send(rs);
      if (rq[0] == C_RUN && rs[2] == ST_OK)
        run = 1, run_t = chip_ms();
    }
    // RUN: let the reply go out, then detach and reset into the app
    if (run && !usb_tx_busy() && chip_ms() - run_t > 20) {
      usb_stop();
      chip_led(0);
      chip_delay_ms(200); // the host must see the device leave (same as the app's BOOT)
      chip_reset();
    }
  }
}
