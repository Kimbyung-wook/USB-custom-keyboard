#ifndef __MK_BOARD_MK6K1N_H__
#define __MK_BOARD_MK6K1N_H__

// MK6K1N: the 6-key + 1-knob CH552G mini keyboard with one LED
// (docs/PINMAP.md 2). The name must match gui/core/boards.py.

#define BOARD_NAME "MK6K1N" // up to 8 ASCII characters
// One WS2812 is fitted, at a chain position not known: every color is sent
// to 8 positions (as test_pinmap did)
#define BOARD_LED_COUNT 8
#define BOARD_KEY_LEDS 0 // 1: LED i sits under key i
#define BOARD_LED_SELF_TEST 1 // color wheel 2 x 3 s, then off 0.3 s at power-up (src/core/led.c)

#include "ch552_6k1n.h"

#endif
