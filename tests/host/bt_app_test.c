// Host check of src/core/bt_app.c with bt_led.c and battery.c (tests/host/run.py
// check_bt_app). Stubs the CH573 HAL calls; prints "<name> <LED bits hex> <sleep>".
// Built twice: -DBTLED_DEMO=0 (normal) and 1 (LED check build), with
// -DBOARD_HEADER naming a header that sets BOARD_BT_LED and BOARD_BATTERY.
#include <stdio.h>
#include "core/battery.h"
#include "core/bt_app.h"
#include "core/bt_led.h"

static int16_t vdd = 3517; // battery about 4.17 V
static uint8_t leds;
static uint32_t t;

int16_t hal_battery_vdd_mv(void) { return vdd; }
void hal_btled_init(void) { leds = 0; }
void hal_btled_write(uint8_t bits) { leds = bits; }

// Run until time `until` with these keys and USB state; 1 if bt_task asked for sleep
static int run(uint32_t until, uint16_t keys, uint8_t usb) {
  int sleep = 0;
  for (; t < until; t++) {
    sleep |= bt_task(t, keys, usb);
  }
  return sleep;
}

static void show(const char *name, int sleep) { printf("%s %02x %d\n", name, leds, sleep); }

// Press a key for 50 ms, then release for 50 ms
__attribute__((unused)) static void tap(uint16_t keys, uint8_t usb) {
  run(t + 50, keys, usb);
  run(t + 50, 0, usb);
}

// LEDs lit at any time in the next n ms (USB on, no keys)
__attribute__((unused)) static uint8_t seen(int n) {
  uint8_t s = 0;
  for (; n > 0; n--) {
    run(t + 1, 0, 1);
    s |= leds;
  }
  return s;
}

int main(void) {
  t = 0;
  bt_setup(t);
#if !BTLED_DEMO
  show("usb_off", run(100, 0, 0));          // nothing lit on battery, full cell
  show("usb_on", run(200, 0, 1));           // green
  show("key_down", run(215, 0x02, 1));      // white blip + green
  show("key_after", run(300, 0x02, 1));     // held: blip over
  run(400, 0, 1);
  show("combo_early", run(1300, 0x21, 1));  // held 0.9 s: nothing yet
  show("combo_fired", run(1450, 0x21, 1));  // 3 blue (green dropped: 3 LEDs at most)
  show("combo_end", run(3500, 0, 1));       // 2 s later: green again
  // Low cell: the reading smooths over a few samples (10 s apart)
  vdd = 2600; // battery 3250 mV
  // Samples at 10, 20, ... s: 3938, 3766, 3637, 3541, 3469, 3415, 3374 (low at 70 s)
  show("low_soon", run(69000, 0, 0));
  run(75000, 0, 0);
  {
    int lit = 0, n;
    for (n = 0; n < 5000; n++) {
      run(t + 1, 0, 0);
      lit += leds == BTLED_RED;
    }
    printf("low_red_ms %d\n", lit); // 50 ms of red per 5 s
  }
  // Critical on battery: 3 red blinks, then sleep; on the cable never
  vdd = 2300; // battery 2950 mV
  // Samples 3268, 3189, 3130, 3085 (critical at 110 s)
  show("crit_usb", run(115000, 0, 1));
  show("crit_batt", run(116000, 0, 0));
#else
  show("demo_start", run(100, 0, 1));       // green (USB)
  tap(0x01, 1);                             // key 1: slot 2
  show("demo_slot2", run(t + 10, 0, 1));    // blue 2 + green
  show("demo_slot_end", run(t + 1200, 0, 1));
  tap(0x02, 1);                             // key 2: pairing on slot 2
  {
    uint8_t seen = 0;
    int n;
    for (n = 0; n < 1000; n++) {
      run(t + 1, 0, 1);
      seen |= leds;
    }
    printf("demo_pair_seen %02x\n", seen); // blue 2 blinks, green on
  }
  tap(0x10, 1);                             // key 5: dongle, USB green hidden
  printf("demo_dongle_seen %02x\n", seen(3000)); // green only, no slot blue
  show("demo_dongle_end", run(t + 200, 0, 1));
  tap(0x10, 1);                             // key 5 again: back to USB
  printf("demo_usb_back_seen %02x\n", seen(1100)); // green only (board check 2026-11-13)
  show("demo_usb_back", run(t + 10, 0, 1));
  tap(0x20, 1);                             // key 6: low
  printf("demo_batt1 %d\n", bt_demo_battery());
  tap(0x20, 1);                             // key 6: critical (blinks, no sleep)
  printf("demo_batt2 %d\n", bt_demo_battery());
  show("demo_crit_done", run(t + 1000, 0, 1));
  printf("demo_batt_after %d\n", bt_demo_battery());
  run(t + 100, 0x01, 1);                    // key 1 + 6: battery level only
  show("demo_combo", run(t + 1100, 0x21, 1));
  run(t + 100, 0, 1);
  printf("demo_combo_batt %d\n", bt_demo_battery());
  show("demo_combo_slot", run(t + 3000, 0, 1)); // slot still 2, nothing else lit
#endif
  return 0;
}
