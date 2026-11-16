#ifndef __MK_BT_LED_H__
#define __MK_BT_LED_H__

#include "../config.h"

// Status LEDs of a wireless board (SAYO_2B3: docs/board/BOARD_SAYO_2X3B_RGB.md 4,
// "LED 의미"). Chip-free (tests/host): the app reports events, btled_update()
// returns which LEDs are on, the chip HAL drives the pins
// (CH573: src/hal/ch573/led_ch573.c). Only built with BOARD_BT_LED.
//
// Links: 3 BLE slots (blue L8/L7/L6 = slot 1/2/3) + the USB dongle (green L4).
// At most BTLED_MAX_ON LEDs light together (more dimmed the LEDs in the pin
// test); the lowest-priority ones are dropped.

#ifdef BOARD_BT_LED

// LED bits (btled_update() result, HAL order)
#define BTLED_WHITE 0x01 // L3 PA4: key press
#define BTLED_GREEN 0x02 // L4 PA5: dongle / USB cable
#define BTLED_RED 0x04   // L5 PA15: battery
#define BTLED_BLUE1 0x08 // L8 PA12: BLE slot 1
#define BTLED_BLUE2 0x10 // L7 PA13: BLE slot 2
#define BTLED_BLUE3 0x20 // L6 PA14: BLE slot 3

// Slots: 0..2 = BLE devices 1..3, 3 = dongle
#define BTLED_SLOTS_BLE 3
#define BTLED_SLOT_DONGLE 3

// Link states of the selected slot
#define BTLINK_IDLE 0      // nothing (LED off)
#define BTLINK_PAIRING 1   // advertising for a new device: fast blink
#define BTLINK_RECONNECT 2 // looking for the known device: slow blink
#define BTLINK_CONNECTED 3 // on for BTLED_CONNECTED_MS, then off

// ---- times (ms) and counts ----
#define BTLED_MAX_ON 3
#define BTLED_PAIR_TOGGLE_MS 200    // pairing: on 0.2 s, off 0.2 s
#define BTLED_RECONN_TOGGLE_MS 1000 // reconnecting: on 1 s, off 1 s
#define BTLED_CONNECTED_MS 3000     // connected: on, then off
#define BTLED_SLOT_MS 1000          // slot changed: that slot's LED
#define BTLED_KEY_MS 30             // white blip per key press
#define BTLED_LOW_PERIOD_MS 5000    // low battery: red blip every 5 s
#define BTLED_LOW_ON_MS 50
#define BTLED_CRIT_TOGGLE_MS 100    // critical: 3 fast red blinks, then sleep
#define BTLED_CRIT_BLINKS 3
#define BTLED_BATT_MS 2000          // battery check: blue count shown
#define BTLED_BATT_HOLD_MS 1000     // keys held this long to start it
#define BTLED_BATT_3_PCT 66         // > this: 3 blue
#define BTLED_BATT_2_PCT 33         // > this: 2 blue, else 1
// Battery check keys: key 1 + key 6 (bit n = key n+1)
#define BTLED_BATT_KEYS ((1u << 0) | (1u << 5))

void btled_init(void);
void btled_slot(uint8_t slot, uint32_t now);  // selected slot changed: its LED 1 s
// Same slot change without the 1 s LED: the link moved between the dongle
// and the USB cable (only green shows then), not a slot picked by the user
void btled_select(uint8_t slot);
void btled_link(uint8_t state, uint32_t now); // link state of the selected slot
void btled_usb(uint8_t wired);                // 1 = working over the USB cable: green on
void btled_key(uint32_t now);                 // a key went down: white blip
void btled_key_flash(uint8_t on);             // white blip on/off (default on, later a setting)
void btled_battery(uint8_t low, uint8_t critical, uint32_t now); // battery_low()/critical()
void btled_show_battery(uint8_t percent, uint32_t now);         // blue count for 2 s
// Key state (bit n = key n+1) for the key 1 + 6 hold; 1 once when it fires
uint8_t btled_keys(uint16_t keys, uint32_t now);
uint8_t btled_update(uint32_t now); // LED bits to show now
uint8_t btled_sleep_due(void);      // 1 = critical blinks done: go to deep sleep
uint8_t btled_slot_bit(uint8_t slot); // blue bit of a BLE slot, green for the dongle

// From the chip HAL
void hal_btled_init(void);          // LED pins: output low (off)
void hal_btled_write(uint8_t bits); // BTLED_* bits on, the rest off

#endif
#endif
