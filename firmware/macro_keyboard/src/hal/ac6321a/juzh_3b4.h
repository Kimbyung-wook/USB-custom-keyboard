#ifndef __MK_BOARD_JUZH_3B4_H__
#define __MK_BOARD_JUZH_3B4_H__

// JUZH_3B4: 12 keys (3 x 4) + 2 knobs (left of the keys, one above the
// other), BLE wireless + USB, on a JieLi AC6321A (BD19, AC632N family)
// (docs/board/BOARD_KEY12_JUZHEN_V01.md). UNVERIFIED: no pin is known yet.
// Kept beside its HAL, not in src/boards: the board is still in
// gui/core/boards.py PREPARING, and config.h picks it with -DBOARD_JUZH_3B4
// (jl_build_ac6321a.sh).
//
// TODO(pins): every pin below is PIN_NONE until the pin test / continuity
// check (board doc 2, checklist 17). A PIN_NONE pin is never touched: the
// key, knob or LED on it stays off, so a build with them is safe on the chip.
// Pins to keep in mind: PA9 = long press reset (held low resets the chip),
// PB2 = VCM reset unless VCM_DET_EN(0) (hal_init does it), PB3 = MCLR,
// USB0 (DP0/DM0, pins 15/14) = our USB, USB1 (29/28) = GPIO after usb1_iomode(1).

#define BOARD_NAME "JUZH_3B4"
#define BOARD_HAL_AC6321A 1
#define BOARD_CHIP "AC6321A"
// HELLO chip ID: not decided (gui/core/boards.py PREPARING has 0 = unknown).
// TODO(PM): pick one with the GUI session (gui/core/chips.py)
#define BOARD_CHIP_ID 0x00
#define BOARD_KEYS 12
#define BOARD_KNOBS 2
#define BOARD_ROWS 3
// Holding a knob starts the pattern entry (src/core/input.c); the auto-lock
// settings slot as on every board (gui/core/boards.py PREPARING)
#define BOARD_FEATURES (BOARD_FEATURE_LOCK | BOARD_FEATURE_AUTO_LOCK)

// Pins are {port, bit}: port 0 = A (PA0-PA9), 1 = B (PB0-PB9), 2 = USB1
// (bit 0 = USB1DP, 1 = USB1DM). PIN_NONE = not connected / not known yet.
#define PIN_NONE {0xFF, 0xFF}

// Keys: a matrix is likely (21 GPIO for 12 keys + 6 knob lines + button +
// LEDs, board doc 2). Rows driven low one at a time, columns read with
// pull-up, pressed = low (the diode direction is checked in the pin test;
// BOARD_MATRIX_COL2ROW 1 would swap the roles). Key k = row k / 4, column
// k % 4 (keys 1-4 top row, from the left).
// TODO(pins): rows and columns
#define BOARD_MATRIX_ROWS 3
#define BOARD_MATRIX_COLS 4
#define BOARD_ROW_PINS PIN_NONE, PIN_NONE, PIN_NONE
#define BOARD_COL_PINS PIN_NONE, PIN_NONE, PIN_NONE, PIN_NONE

// Knobs: A, B, switch of knob 0 (top, factory key IDs 0x10-0x12) and knob 1
// (bottom, 0x13-0x15); inputs with pull-up, switch pressed = low.
// TODO(pins): all six lines and both switches; which way counts clockwise
#define BOARD_KNOB_PINS PIN_NONE, PIN_NONE, PIN_NONE, PIN_NONE, PIN_NONE, PIN_NONE
#define BOARD_KNOB_SW_PINS PIN_NONE, PIN_NONE

// The button next to the knob LEDs (factory: next connection slot).
// TODO(pins); not used by the firmware yet (BLE slots: core/bt_app.h)
#define BOARD_SLOT_BUTTON_PIN PIN_NONE

// LEDs: no key LEDs driven yet (their kind is not known, board doc 4)
#define BOARD_LED_COUNT 0
#define BOARD_KEY_LEDS 0
// Status LEDs: the 3 LEDs between the knobs = BLE slot LEDs 1, 2, 3
// (user's plan). They look RGB; taken as R, G, B pins each, high = on.
// TODO(pins): LED kind (RGB with 3 pins, or WS2812-like) and pins. How the
// BTLED_* bits map to them: src/hal/ac6321a/led_ac6321a.c. The charge LED
// is driven by the charger, not by the chip.
#define BOARD_BT_LED 1
#define BOARD_BT_LED_ACTIVE_HIGH 1
#define BOARD_BT_LED_PINS                                                      \
  PIN_NONE, PIN_NONE, PIN_NONE, /* LED 1: R, G, B */                           \
  PIN_NONE, PIN_NONE, PIN_NONE, /* LED 2 */                                    \
  PIN_NONE, PIN_NONE, PIN_NONE  /* LED 3 */

// Battery: read on the chip's internal VBAT channel (1/4, src/hal/ac6321a)
#define BOARD_BATTERY 1

// BLE HID keyboard (src/hal/ac6321a/ble_ac6321a.c); jl_build_ac6321a.sh BLE=0
// builds USB only
#ifndef MK_BLE
#define MK_BLE 1
#endif
#define BOARD_BLE MK_BLE

#endif
