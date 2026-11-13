// Host check of src/core/bt_led.c (tests/host/run.py check_bt_led).
// Prints "<name> <bits hex>" for fixed scenarios; run.py holds the expected values.
// Built with -DBOARD_HEADER naming a header that sets BOARD_BT_LED.
#include <stdio.h>
#include "core/bt_led.h"

static void at(const char *name, uint32_t t) { printf("%s %02x\n", name, btled_update(t)); }

int main(void) {
  uint32_t t;
  uint8_t fires = 0;

  // pairing on slot 2: fast blink, slot change shows 1 s first
  btled_init();
  btled_slot(1, 0);
  btled_link(BTLINK_PAIRING, 0);
  at("pair_0", 0);
  at("pair_1100", 1100); // slot window over, 1100/200 = 5 -> off
  at("pair_1200", 1200); // 6 -> on
  // reconnecting: slow blink
  btled_link(BTLINK_RECONNECT, 2000);
  at("reconn_2500", 2500);
  at("reconn_3500", 3500);
  at("reconn_4000", 4000);
  // connected: 3 s on, then off
  btled_link(BTLINK_CONNECTED, 5000);
  at("conn_7999", 7999);
  at("conn_8000", 8000);
  // key blip 30 ms
  btled_key(9000);
  at("key_9029", 9029);
  at("key_9030", 9030);
  btled_key_flash(0);
  btled_key(9100);
  at("key_off", 9110);
  btled_key_flash(1);

  // dongle: green 3 s; USB cable: green stays
  btled_init();
  btled_slot(BTLED_SLOT_DONGLE, 0);
  btled_link(BTLINK_CONNECTED, 0);
  at("dongle_2999", 2999);
  at("dongle_3000", 3000);
  btled_usb(1);
  at("usb_60000", 60000);

  // battery check: key 1 + 6 for 1 s, blue count for 2 s, at most 3 LEDs on
  btled_init();
  btled_slot(0, 0);
  btled_link(BTLINK_PAIRING, 0);
  for (t = 0; t <= 1500; t += 10) {
    fires += btled_keys(0x21, 100 + t);
  }
  fires += btled_keys(0x01, 1700);
  printf("combo_fires %02x\n", fires);
  btled_show_battery(80, 1100);
  btled_key(1100);
  btled_battery(1, 0, 1100);
  at("batt80", 1110); // 3 blue; red and white dropped
  btled_show_battery(50, 1200);
  at("batt50", 1300);
  btled_show_battery(10, 1400);
  at("batt10", 1500);
  at("batt_end", 3400); // display over, pairing blink back (3400/200 = 17 -> off)
  at("batt_end2", 3600);

  // low battery: red blip every 5 s; critical: 3 blinks, then sleep
  btled_init();
  btled_battery(1, 0, 0);
  at("low_0", 0);
  at("low_49", 49);
  at("low_50", 50);
  at("low_5000", 5000);
  btled_battery(1, 1, 6000);
  at("crit_6000", 6000);
  at("crit_6100", 6100);
  at("crit_6200", 6200);
  at("crit_6599", 6599);
  printf("sleep_before %02x\n", btled_sleep_due());
  at("crit_6600", 6600);
  printf("sleep_after %02x\n", btled_sleep_due());
  return 0;
}
