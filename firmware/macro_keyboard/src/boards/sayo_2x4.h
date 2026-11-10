#ifndef __MK_BOARD_SAYO_2X4_H__
#define __MK_BOARD_SAYO_2X4_H__

// SAYO_2X4: 8-key (2 x 4) CH32V203C8U6 keyboard, no knob (SayoDevice Keyb T8B7).
// RGB LED per key (not WS2812): R, G and B cathode lines shared by a group of
// keys (low = lit), each key has its own anode pin (high = lit). Same cathode
// pins as SAYO_3X3, grouped 1-2-3, 5-6-7, 4-8.
// Pins found with tests/hw/ch32v203/test_ch32v203 (2026-11-07). HAL: src/hal/ch32v203.
// The name must match gui/core/boards.py.

#define BOARD_NAME "SAYO_2X4"
#define BOARD_HAL_CH32V20X 1
#define BOARD_CHIP "CH32V203"
#define BOARD_CHIP_ID 0x19 // CH32V20x family byte, as SAYO_3X3
#define BOARD_KEYS 8
#define BOARD_KNOBS 0
#define BOARD_ROWS 2
// No knob: holding any key starts the pattern entry (src/core/input.c)
#define BOARD_FEATURES (BOARD_FEATURE_RGB_LED | BOARD_FEATURE_LOCK)

// Port 0 = A, 1 = B. Keys in slot order (1-4 top row, 5-8 bottom row, left to
// right), pressed = low, internal pull-up. PA15 needs SWJ off (hal_init).
// PB2 (BOOT1) reads low from start-up (pulled down on the board), not a key.
#define BOARD_KEY_PINS                                                         \
  {0, 6}, {0, 15}, {1, 5}, {1, 9}, {0, 5}, {0, 7}, {1, 0}, {0, 4}
// Held at power-up = our bootloader (firmware/bootloader_ch32v203): slot index
// into BOARD_KEY_PINS, key 5 = bottom left (PA5). gui/core/boards.py boot_key matches.
#define BOARD_BOOT_KEY 4

// LED cathode lines per key group {red, green, blue} as {port, bit}; low = lit.
// FIND mode: PA13 = blue of keys 1-3, PA8-PA10 = keys 5-7, PA0-PA2 = keys 4/8.
// Red/green of keys 1-3 are taken to be PA11/PA12 as on SAYO_3X3 (the test
// skips those pins; check with the LED self test at power-up).
#define BOARD_LED_ROWS 3
#define BOARD_LED_PINS                                                         \
  {{0, 11}, {0, 12}, {0, 13}},                                                 \
  {{0, 8}, {0, 9}, {0, 10}},                                                   \
  {{0, 0}, {0, 1}, {0, 2}}
// Anode pin per key, in slot order; high = lit (ANODE mode, 2026-11-07)
#define BOARD_KEY_LED_PINS \
  {1, 14}, {1, 3}, {1, 4}, {1, 8}, {1, 13}, {1, 15}, {1, 12}, {1, 11}
#define BOARD_LED_COUNT 8
#define BOARD_KEY_LEDS 1 // only the pressed key's LED turns green
#define BOARD_LED_SELF_TEST 1 // color wheel 2 x 3 s, then off 0.3 s at power-up (src/core/led.c)
// Slots clockwise around the keys from key 1, seen from the top: the power-up
// color wheel turns along them (src/core/led.c).
#define BOARD_LED_RING 0, 1, 2, 3, 7, 6, 5, 4 // keys 1 2 3 4 8 7 6 5
#define BOARD_LED_RING_CHASE 1 // one LED at a time; 0 = rainbow around the ring

#endif
