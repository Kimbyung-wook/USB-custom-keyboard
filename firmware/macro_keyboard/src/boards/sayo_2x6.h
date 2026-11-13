#ifndef __MK_BOARD_SAYO_2X6_H__
#define __MK_BOARD_SAYO_2X6_H__

// SAYO_2X6: 12-key (2 x 6) CH32V203C8U6 keyboard, no knob (SayoDevice, exact
// model not known yet). Name is provisional (docs/board/BOARD_SAYO_2X6.md).
// RGB LED per key (not WS2812), as SAYO_3X3/SAYO_2X4: R, G and B cathode
// lines shared by a group of keys (low = lit), each key has its own anode pin
// (high = lit). Same cathode pins as SAYO_3X3, grouped 1-2-3-7, 4-5-6-12,
// 9-10-11 (key 8 not seen, see below).
// Pins found with tests/hw/ch32v203/test_ch32v203 (keys and LEDs 2026-11-13).
// HAL: src/hal/ch32v203. The name must match gui/core/boards.py.

#define BOARD_NAME "SAYO_2X6"
#define BOARD_HAL_CH32V20X 1
#define BOARD_CHIP "CH32V203"
#define BOARD_CHIP_ID 0x19 // CH32V20x family byte, as SAYO_3X3
#define BOARD_KEYS 12
#define BOARD_KNOBS 0
#define BOARD_ROWS 2
// No knob: holding any key starts the pattern entry (src/core/input.c)
#define BOARD_FEATURES (BOARD_FEATURE_RGB_LED | BOARD_FEATURE_LOCK)

// Port 0 = A, 1 = B, 2 = C. Keys in slot order (1-6 top row, 7-12 bottom row,
// left to right), pressed = low, internal pull-up, each key on its own pin (no
// matrix). PA14/PA15 need SWJ off (hal_init). PB2 (BOOT1) reads low from
// start-up (pulled down on the board), not a key.
#define BOARD_KEY_PINS                                                         \
  {1, 0}, {0, 15}, {2, 13}, {0, 14}, {1, 8}, {2, 14},                          \
  {0, 7}, {0, 6}, {0, 5}, {0, 4}, {0, 3}, {2, 15}
// Held at power-up = our bootloader (firmware/bootloader_ch32v203): slot index
// into BOARD_KEY_PINS, key 7 = bottom left (PA7). gui/core/boards.py boot_key matches.
#define BOARD_BOOT_KEY 6

// LED cathode lines per key group {red, green, blue} as {port, bit}; low = lit.
// The HAL drives one color on every group at once, so which key sits in which
// group does not matter here. FIND mode: PA13 = blue of keys 1-2-3-7, PA8-PA10
// = keys 9-10-11, PA0-PA2 = keys 4-5-6-12. Red/green of keys 1-2-3-7 are PA11/PA12
// as on SAYO_3X3 (the test skips those pins; confirmed by the self test).
#define BOARD_LED_ROWS 3
#define BOARD_LED_PINS                                                         \
  {{0, 11}, {0, 12}, {0, 13}},                                                 \
  {{0, 8}, {0, 9}, {0, 10}},                                                   \
  {{0, 0}, {0, 1}, {0, 2}}
// Anode pin per key, in slot order; high = lit (ANODE mode, 2026-11-13).
// Key 8 lit in neither mode: its anode is PB2 (BOOT1, the one pin the test left
// alone because it reads low; a pull-down there also keeps key 8 dark in
// FIND). Confirmed by the LED self test at power-up (2026-11-13).
#define BOARD_KEY_LED_PINS                                                     \
  {1, 13}, {1, 14}, {1, 3}, {1, 4}, {1, 5}, {1, 9},                            \
  {1, 1}, {1, 2}, {1, 10}, {1, 15}, {1, 12}, {1, 11}
#define BOARD_LED_COUNT 12
#define BOARD_KEY_LEDS 1 // only the pressed key's LED turns green
#define BOARD_LED_SELF_TEST 1 // color wheel 2 x 3 s, then off 0.3 s at power-up (src/core/led.c)
// Slots clockwise around the keys from key 1, seen from the top: the power-up
// light turns along them (src/core/led.c).
#define BOARD_LED_RING 0, 1, 2, 3, 4, 5, 11, 10, 9, 8, 7, 6 // keys 1-6, 12-7
#define BOARD_LED_RING_CHASE 2 // two lights, half the ring apart (long ring); 0 = rainbow

#endif
