#ifndef __MK_BT_APP_H__
#define __MK_BT_APP_H__

#include "../config.h"

// Wireless-board extras next to the keyboard sequence (core/app.c): battery
// readings (battery.c) and the status LEDs (bt_led.c). Chip-free (tests/host):
// the chip's main loop calls bt_task() after app_loop(); it reads the supply
// through hal_battery_vdd_mv() and drives the LEDs through hal_btled_write().
//
// Stage 1 (SAYO_2B3, no radio yet): USB cable only. Key press = white blip, USB
// configured = green, battery low/critical = red, key 1 + 6 held 1 s =
// battery level in blue.
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

void bt_setup(uint32_t now);
// keys: bit n = key n+1 pressed (raw); usb: 1 = configured by a host.
// Returns 1 when the battery is critical and the LEDs are done: deep sleep.
uint8_t bt_task(uint32_t now, uint16_t keys, uint8_t usb);
uint8_t bt_demo_battery(void); // demo: 0 real, 1 low, 2 critical (tests)

#endif
#endif
