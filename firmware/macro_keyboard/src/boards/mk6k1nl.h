#ifndef __MK_BOARD_MK6K1NL_H__
#define __MK_BOARD_MK6K1NL_H__

// MK6K1NL: 6-key + 1-knob CH552G keyboard with a WS2812 under every key
// (docs/PINMAP.md 3). Keys, knob and LED pin are the same as on MK6K1N
// (confirmed with tests/hw/mk6k1nl/test_mk6k1nl). The name must match gui/core/boards.py.

#define BOARD_NAME "MK6K1NL" // up to 8 ASCII characters
#define BOARD_LED_COUNT 6    // chain position i = key i (A-F), GRB
#define BOARD_KEY_LEDS 1
#define BOARD_LED_SELF_TEST 1 // color wheel 2 x 3 s, then off 0.3 s at power-up (src/core/led.c)
// Slots clockwise around the keys from key 1 (A), seen from the top: the power-up
// color wheel turns along them (src/core/led.c)
#define BOARD_LED_RING 0, 1, 2, 5, 4, 3 // keys A B C F E D
#define BOARD_LED_RING_CHASE 1 // one LED at a time; 0 = rainbow around the ring

#include "ch552_6k1n.h"

#endif
