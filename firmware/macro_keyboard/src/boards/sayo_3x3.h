#ifndef __MK_BOARD_SAYO_3X3_H__
#define __MK_BOARD_SAYO_3X3_H__

// SAYO_3X3: 9-key (3 x 3) CH32V203C8U6 keyboard, no knob. RGB LED per key
// (not WS2812): each row has R, G and B cathode lines shared by its 3 keys
// (low = lit), each key has its own anode pin (high = lit).
// Pins confirmed with tests/hw/ch32v203/test_ch32v203 (2026-11-05). HAL: src/hal/ch32v203.
// The name must match gui/core/boards.py.

#define BOARD_NAME "SAYO_3X3"
#define BOARD_HAL_CH32V20X 1
#define BOARD_CHIP "CH32V203"
// wchisp info (2026-11-08): CH32V203C8U6[0x3019] = chip 0x30, family 0x19. HELLO
// and gui/core/chips.py use the family byte (CH55x: the chip, 0x52)
#define BOARD_CHIP_ID 0x19
#define BOARD_KEYS 9
#define BOARD_KNOBS 0
#define BOARD_ROWS 3
// No knob: holding any key starts the pattern entry (src/core/input.c)
#define BOARD_FEATURES (BOARD_FEATURE_RGB_LED | BOARD_FEATURE_LOCK)

// Port 0 = A, 1 = B. Keys in slot order (row-major, 1-9), pressed = low,
// internal pull-up. PA14/PA15 need SWJ off (hal_init). Key 7 is on PB2 (BOOT1).
#define BOARD_KEY_PINS                                                         \
  {1, 15}, {0, 14}, {0, 4}, {1, 10}, {0, 15}, {0, 3}, {1, 2}, {1, 11}, {1, 1}
// Held at power-up = our bootloader (firmware/bootloader_ch32v203): slot index
// into BOARD_KEY_PINS, key 7 = bottom left. gui/core/boards.py boot_key matches.
#define BOARD_BOOT_KEY 6

// LED cathode lines per row {red, green, blue} as {port, bit}; low = lit.
// Row 1 red/green are PA11/PA12, the pins of the USBD controller (unused). USB
// runs on USBFS = PB6/PB7 (datasheet 48-pin table; PB6 reads low = D- idle).
#define BOARD_LED_ROWS 3
#define BOARD_LED_PINS                                                         \
  {{0, 11}, {0, 12}, {0, 13}},                                                 \
  {{0, 8}, {0, 9}, {0, 10}},                                                   \
  {{0, 0}, {0, 1}, {0, 2}}
// Anode pin per key, in slot order; high = lit. Found with the PWR mode of
// tests/hw/ch32v203/test_ch32v203 (2026-11-07): pulled down, only that key went dark.
#define BOARD_KEY_LED_PINS \
  {1, 3}, {1, 4}, {1, 9}, {1, 14}, {1, 5}, {1, 8}, {1, 13}, {1, 12}, {1, 0}
#define BOARD_LED_COUNT 9
#define BOARD_KEY_LEDS 1 // only the pressed key's LED turns green
#define BOARD_LED_SELF_TEST 1 // color wheel 2 x 3 s, then off 0.3 s at power-up (src/core/led.c)
// Slots clockwise around the keys from key 1, seen from the top: the power-up
// color wheel turns along them (src/core/led.c). Key 5, the middle, is off the ring
#define BOARD_LED_RING 0, 1, 2, 5, 8, 7, 6, 3 // keys 1 2 3 6 9 8 7 4
#define BOARD_LED_RING_CHASE 1 // one LED at a time; 0 = rainbow around the ring

#endif
