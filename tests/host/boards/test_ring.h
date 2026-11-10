#ifndef __MK_TEST_BOARD_RING_H__
#define __MK_TEST_BOARD_RING_H__

// Host-test board with the rainbow around the ring (BOARD_LED_RING without
// BOARD_LED_RING_CHASE): no real board uses that style now, this keeps it
// tested. Built with the host HAL only (tests/host/run.py); not in src/boards
// or gui/core/boards.py.

#define BOARD_NAME "TEST_RNG"
#define BOARD_CHIP "UNKNOWN"
#define BOARD_CHIP_ID 0x00
#define BOARD_KEYS 6
#define BOARD_KNOBS 0
#define BOARD_ROWS 2
#define BOARD_FEATURES (BOARD_FEATURE_RGB_LED | BOARD_FEATURE_LOCK)
#define BOARD_BOOT_KEY 3
#define BOARD_LED_COUNT 6
#define BOARD_KEY_LEDS 1
#define BOARD_LED_SELF_TEST 1
#define BOARD_LED_RING 0, 1, 2, 5, 4, 3

#endif
