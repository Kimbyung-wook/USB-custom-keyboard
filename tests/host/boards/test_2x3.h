#ifndef __MK_TEST_BOARD_2X3_H__
#define __MK_TEST_BOARD_2X3_H__

// Host-test stand-in for the 6-key (2 x 3) Bluetooth keyboard planned as
// BT_2X3 (chip unknown). Only what src/core needs, with the small CH55x-size
// config (128 B) so the tight case is covered. Built with the host HAL only
// (tests/host/run.py); not in src/boards or gui/core/boards.py.

#define BOARD_NAME "TEST_2X3"
#define BOARD_CHIP "UNKNOWN"
#define BOARD_CHIP_ID 0x00
#define BOARD_KEYS 6
#define BOARD_KNOBS 0
#define BOARD_ROWS 2
#define BOARD_FEATURES (BOARD_FEATURE_RGB_LED | BOARD_FEATURE_LOCK)
#define BOARD_BOOT_KEY 3 // bottom left of 2 x 3
#define BOARD_LED_COUNT 6
#define BOARD_KEY_LEDS 1
#define BOARD_LED_SELF_TEST 1

#endif
