#ifndef __MK_BOARD_SAYO_2B3_H__
#define __MK_BOARD_SAYO_2B3_H__

// SAYO_2B3: 6-key (2 x 3) wireless keyboard on a CH573F, no knob
// (docs/board/BOARD_SAYO_2X3B_RGB.md). USB cable keyboard + BLE HID keyboard
// (one bonded host, src/core/bt_app.h) + status LEDs + battery reading.
// Kept beside its HAL, not in src/boards (config.h picks only those):
// Makefile.ch573 passes -DBOARD_HEADER; tools/repo_facts.py reads it here too.

#define BOARD_NAME "SAYO_2B3"
#define BOARD_HAL_CH573 1
#define BOARD_CHIP "CH573"
#define BOARD_CHIP_ID 0x73 // as the bootloader's HELLO [45]
#define BOARD_KEYS 6
#define BOARD_KNOBS 0
#define BOARD_ROWS 2
// No knob: holding any key starts the pattern entry (src/core/input.c)
#define BOARD_FEATURES BOARD_FEATURE_LOCK

// Port 0 = A, 1 = B. Keys in slot order (1-3 top row, 4-6 bottom row), each on
// its own pin, pressed = low, internal pull-up (CH573 pin test, 2026-11-13).
#define BOARD_KEY_PINS {1, 14}, {1, 13}, {1, 4}, {0, 9}, {0, 10}, {0, 11}
// Held at power-on = our bootloader (firmware/bootloader_ch573): key 4 (PA9,
// bottom row left, as on the other boards)
#define BOARD_BOOT_KEY 3

// No key LEDs: six single-color status LEDs (src/core/bt_led.c,
// src/hal/ch573/led_ch573.c), high = on
#define BOARD_LED_COUNT 0
#define BOARD_KEY_LEDS 0
#define BOARD_BT_LED 1
// {port, bit} in BTLED_* bit order (src/core/bt_led.h): L3 L4 L5 L6 L7 L8,
// all on port A (led_ch573.c)
#define BOARD_BT_LED_PINS {0, 4}, {0, 5}, {0, 15}, {0, 14}, {0, 13}, {0, 12}
// Li-Po cell -> diode -> VDD, read on the internal VBAT channel (src/core/battery.c)
#define BOARD_BATTERY 1
// One RAM copy of the config (src/core/storage.c): the BLE stack needs the RAM
#define BOARD_STORE_ONE_BUFFER 1
// BLE HID keyboard (src/hal/ch573/ble_ch573.c); not in the LED check build
// (BTLED_DEMO=1), where the keys set the link states by hand
#if !BTLED_DEMO
#define BOARD_BLE 1
#endif

#endif
