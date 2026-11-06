#ifndef __MK_BOARD_3X3V_RGB_H__
#define __MK_BOARD_3X3V_RGB_H__

// 3X3V_RGB: 9-key (3 x 3) CH32V203C8U6 keyboard, no knob. Each row of three
// keys has one R, G and B LED line shared by the row (not WS2812), active low.
// Pins confirmed with tests/hw/ch32v203/test_ch32v203 (2026-11-05). HAL: src/hal/ch32v203.
// The name must match gui/core/boards.py.

#define BOARD_NAME "3X3V_RGB"
#define BOARD_HAL_CH32V20X 1
#define BOARD_CHIP "CH32V203"
#define BOARD_CHIP_ID 0x19 // unverified: WCH chip id of the CH32V20x family
#define BOARD_KEYS 9
#define BOARD_KNOBS 0
#define BOARD_ROWS 3
#define BOARD_FEATURES BOARD_FEATURE_RGB_LED // no lock: entry is a knob hold

// Port 0 = A, 1 = B. Keys in slot order (row-major, 1-9), pressed = low,
// internal pull-up. PA14/PA15 need SWJ off (hal_init). Key 7 is on PB2 (BOOT1).
#define BOARD_KEY_PINS                                                         \
  {1, 15}, {0, 14}, {0, 4}, {1, 10}, {0, 15}, {0, 3}, {1, 2}, {1, 11}, {1, 1}

// LED lines per row {red, green, blue} as {port, bit}; low = lit.
// Row 1 red/green are PA11/PA12 = USB D-/D+: unused (port 0xFF) until USB+LED
// sharing is checked, so row 1 shows blue only.
#define BOARD_LED_ROWS 3
#define BOARD_LED_PINS                                                         \
  {{0xFF, 0}, {0xFF, 0}, {0, 13}},                                                 \
  {{0, 8}, {0, 9}, {0, 10}},                                                   \
  {{0, 0}, {0, 1}, {0, 2}}
#define BOARD_KEY_LEDS 0 // LEDs are per row, not per key

#endif
