#ifndef __MK_TEST_BOARD_BAT_H__
#define __MK_TEST_BOARD_BAT_H__

// Host-test board with a battery (BOARD_BATTERY, like SAYO_2B3) for
// CMD_BATTERY (tests/host/run.py check_battery_cmd), on the host HAL. Not in
// src/boards or gui/core/boards.py.

#define BOARD_NAME "TEST_BAT"
#define BOARD_CHIP "UNKNOWN"
#define BOARD_CHIP_ID 0x00
#define BOARD_KEYS 6
#define BOARD_KNOBS 0
#define BOARD_ROWS 2
#define BOARD_FEATURES (BOARD_FEATURE_RGB_LED | BOARD_FEATURE_LOCK) // as the other test boards
#define BOARD_BOOT_KEY 3
#define BOARD_LED_COUNT 6
#define BOARD_KEY_LEDS 1
#define BOARD_LED_SELF_TEST 1
#define BOARD_BATTERY 1

#endif
