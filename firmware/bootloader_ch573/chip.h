#ifndef __BL_CHIP_H__
#define __BL_CHIP_H__
// Chip layer of the bootloader: main.c (commands) and usb_hid.c call only
// these. chip_ch57x.c implements them for the CH573 (CH582 shares the USB,
// timer, GPIO and ISP-library interface; only CHIP_ID and the library differ).
#include <stdint.h>

#ifndef CHIP_ID
#define CHIP_ID 0x73 // HELLO [45]: CH573 (wchisp chip id 0x7313)
#endif

void chip_init(void);              // 60 MHz clock, ms timer
uint32_t chip_ms(void);            // ms since chip_init (call often: polled timer)
void chip_delay_ms(uint32_t ms);
int chip_take_mark(void);          // 1 = the app asked to stay (BOOT); clears the mark
int chip_boot_key(void);           // 1 = the board's boot key is held
void chip_uid(uint8_t out[8]);     // 64-bit chip unique ID
int chip_flash_erase(uint32_t addr, uint32_t len);            // 4 KB blocks; 1 = ok
int chip_flash_write(uint32_t addr, const void *buf, uint32_t len); // erased area, RAM buffer; 1 = ok + verified
void chip_led(int on);             // board's bootloader LEDs (none: nothing)
void chip_jump_app(void);          // pins and timer back to reset state, then the app
void chip_reset(void);             // software reset (the enter mark survives it)

// Diagnostic build only (BL_USB_DIAG=1, SAYO_2B3 LEDs)
void chip_diag_leds(uint8_t bits); // bit n = LED L3, L4, L6, L7, L8 (usb_hid.h USB_DIAG_*)
int chip_diag_key5(void);          // 1 = key 5 (PA10) held
void chip_diag_save(const uint8_t *buf, uint32_t len); // Data-Flash 0.. (the app's config area)

#endif
