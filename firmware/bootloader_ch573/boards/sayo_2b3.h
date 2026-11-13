#ifndef __BL_BOARD_SAYO_2B3_H__
#define __BL_BOARD_SAYO_2B3_H__
// SAYO_2B3 (6-key Bluetooth keyboard, CH573F) for the bootloader
// (docs/board/BOARD_SAYO_2X3B_RGB.md). Pins as `port, bit` (port 0 = A, 1 = B).
// When the app gets a board file (src/boards/sayo_2b3.h) these move there.

#define BOARD_NAME "SAYO_2B3"

// Boot key: held at power-on (SW1) = stay in the bootloader. Input with
// pull-up, low = pressed. Key 4 = PA9, bottom row left, as on the other
// boards (2026-11-13; was key 6). Not the ROM bootloader's PB22.
#define BOARD_BOOT_KEY_PIN 0, 9
// Matrix key only: this pin is driven low while the key pin is read
// #define BOARD_BOOT_KEY_DRIVE 0, 4

// LED blinking while in the bootloader: L5 red = PA15 (side status LEDs
// L3-L8 = PA4, PA5, PA15, PA14, PA13, PA12, high = on). The others stay in
// the reset state (input, no pull: off; a pull-up would light them dimly).
#define BOARD_BOOT_LED_PINS {0, 15}
#define BOARD_BOOT_LED_HIGH 1 // 1 = high lights them, 0 = low

#endif
