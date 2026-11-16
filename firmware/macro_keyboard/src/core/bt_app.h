#ifndef __MK_BT_APP_H__
#define __MK_BT_APP_H__

#include "../config.h"

// Wireless-board extras next to the keyboard sequence (core/app.c): battery
// readings (battery.c) and the status LEDs (bt_led.c). Chip-free (tests/host):
// the chip's main loop calls bt_task() after app_loop(); it reads the supply
// through hal_battery_vdd_mv() and drives the LEDs through hal_btled_write().
//
// Key press = white blip, USB configured = green, battery low/critical = red,
// key 1 + 6 held 1 s = battery level in blue.
// BLE (BOARD_BLE, SAYO_2B3 stage 1: one bonded host, HID over GATT):
//   - key 5 held at power-on: forget the bond, advertise to anyone (pairing,
//     the slot's blue LED blinks 0.2 s); also when nothing is bonded yet
//   - else advertise to the bonded host only (reconnecting, slot LED blinks 1 s)
//   - connected: the slot LED on for 3 s; keys type over BLE
//   - USB configured: BLE link dropped, no advertising; cable out: advertise
//   - advertising stops after BT_ADV_TIMEOUT_MS (pairing too); then any key
//     starts it again (that key is not typed: no host is ready)
//   - key 5 at power-on re-pairs the CURRENT slot only; key 6 at power-on is
//     free; key 4 = bootloader. Slot picking/re-pairing while running is the
//     BT_CHORD_* chords below (3 blue slot LEDs; slot 1/2/3 = L8/L7/L6)
// BTLED_DEMO=1 (build option, LED check on the board): keys do not type,
// they set the BLE/dongle states that have no radio behind them yet:
//   key 1 next BLE slot (1 -> 2 -> 3 -> 1)   key 2 pairing   key 3 reconnecting
//   key 4 connected   key 5 dongle connected / back to USB
//   key 6 battery: real -> low -> critical (3 blinks, no sleep) -> real
// A key acts when released, and only if no other key went down meanwhile
// (so key 1 + 6 shows the battery level and nothing else).

#if defined(BOARD_BT_LED) && defined(BOARD_BATTERY)

#ifndef BTLED_DEMO
#define BTLED_DEMO 0
#endif
#define BT_KEY_SCAN_MS 10 // key sampling (also the debounce)

#if BOARD_BLE
#define BT_ADV_TIMEOUT_MS 60000UL // advertising stops (any key restarts it)
#define BT_USB_WAIT_MS 1500       // after power-on: time for USB to come up first
#define BT_PAIR_KEY 4             // key 5 held at power-on: re-pair the current slot

// Stage 2 chords (keyed on the debounced keys, exactly those bits down):
//   key 6 + key 1/2/3 held BT_CHORD_MS -> pick BLE slot 1/2/3
//   key 5 + key 6 held BT_CHORD_MS -> re-pair the current slot (forget its host)
// key 6 + key 1 also shows the battery level at BTLED_BATT_HOLD_MS (1 s) on the
// way to slot 1 at 2 s (bt_led.c owns that blue count). A chord does nothing
// while any other key is down. The keys still type on press like every other
// key (user decision 2026-11-16: no key is held off), so a chord types its keys
// once on the host before it acts -- an accepted side effect.
#define BT_CHORD_MS 2000UL
#define BT_KEY6 (1u << 5)
#define BT_CHORD_SLOT1 (BT_KEY6 | (1u << 0))
#define BT_CHORD_SLOT2 (BT_KEY6 | (1u << 1))
#define BT_CHORD_SLOT3 (BT_KEY6 | (1u << 2))
#define BT_CHORD_REPAIR ((1u << 4) | BT_KEY6)

// BLE link states (hal_ble_state)
#define HAL_BLE_IDLE 0      // not advertising, no link
#define HAL_BLE_ADV 1       // advertising
#define HAL_BLE_LINK 2      // link up, not ready yet (pairing, encryption)
#define HAL_BLE_READY 3     // encrypted, reports enabled: kb_ready() over BLE

// From the chip HAL (CH573: src/hal/ch573/ble_ch573.c)
void hal_ble_start(uint8_t pair); // advertise: 1 = to anyone, 0 = current slot's host only
void hal_ble_stop(void);          // drop the link, stop advertising
uint8_t hal_ble_state(void);
uint8_t hal_ble_bonded(void);     // the current slot has a bonded host
void hal_ble_forget(void);        // forget the current slot's host (re-pair it)
// Stage 2: three slots (0..2), one bonded host each, only the current one
// advertises/connects; the choice is kept in Data-Flash across power-off.
uint8_t hal_ble_slot(void);          // the current slot (0..2)
void hal_ble_select_slot(uint8_t s); // switch slot: reconnect its host, or pair if none
#if BLE_DIAG
// Diagnostic build (Makefile.ch573 DIAG=1): LED bits showing the result of the
// last real key report (flush(), no probe). BTLED_* bits map to physical LEDs
// after the blue-pin reversal: WHITE=L3 connected, GREEN=L4 encrypted,
// RED=L5 last send failed, BLUE3=L6 SUCCESS (sent), BLUE2=L7 bleNoResources
// (CCCD notify off), BLUE1=L8 bleNotReady (not connected/secure).
uint8_t hal_ble_diag_bits(void);
// Second phase: SNV bond count (L3/L4/L5 = 3-bit binary) + L6 encryption
// reached since connect + L7 security request sent since connect.
uint8_t hal_ble_diag_bond(void);
#endif
#endif

// keys: held at power-on (BLE: key 5 = pairing)
void bt_setup(uint32_t now, uint16_t keys);
// keys: bit n = key n+1 pressed (raw); usb: 1 = configured by a host.
// Returns 1 when the battery is critical and the LEDs are done: deep sleep.
uint8_t bt_task(uint32_t now, uint16_t keys, uint8_t usb);
uint8_t bt_demo_battery(void); // demo: 0 real, 1 low, 2 critical (tests)

#endif
#endif
