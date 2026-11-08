#ifndef __MK_TEST_BOARD_2X6_H__
#define __MK_TEST_BOARD_2X6_H__

// Host-test stand-in for the 12-key (2 x 6) keyboard planned as SAYO_2X6
// (chip, pins and LEDs not identified yet). Only what src/core needs; no
// pins, so it builds with the host HAL only (tests/host/run.py). Not in
// src/boards: gui/core/boards.py and the cross checks do not list it.

#define BOARD_NAME "TEST_2X6"
#define BOARD_HAL_CH32V20X 1 // a CH32V203 is likely: its config size (4096 B)
#define BOARD_CHIP "CH32V203"
#define BOARD_CHIP_ID 0x19
#define BOARD_KEYS 12
#define BOARD_KNOBS 0
#define BOARD_ROWS 2
#define BOARD_FEATURES (BOARD_FEATURE_RGB_LED | BOARD_FEATURE_LOCK)
#define BOARD_BOOT_KEY 6 // bottom left of 2 x 6
#define BOARD_LED_COUNT 12
#define BOARD_KEY_LEDS 1
#define BOARD_LED_SELF_TEST 1

#endif
