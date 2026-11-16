// Wireless-board extras (bt_app.h). Empty on other boards (the Arduino IDE
// compiles every file in src).
#include "bt_app.h"

#if defined(BOARD_BT_LED) && defined(BOARD_BATTERY)
#include "battery.h"
#include "bt_led.h"

static uint32_t scanAt, sampleAt;
static uint16_t down;  // debounced keys
static uint8_t usbWas;
#if BOARD_BLE && !BTLED_DEMO
// Link: waiting for USB after power-on, USB cable, advertising, BLE link up,
// advertising timed out
enum { LINK_START, LINK_USB, LINK_ADV, LINK_UP, LINK_OFF };
static uint8_t link, pairMode;
static uint32_t linkAt;
// Chord (slot pick / re-pair): the mask held and since when, fired once
static uint16_t chordMask;
static uint32_t chordAt;
static uint8_t chordFired;
#if BLE_DIAG
// DIAG build: show the BLE HID send state on the LEDs for 2 s after a key press
static uint32_t diagAt;
static uint8_t diagOn;
#endif
#endif
#if BTLED_DEMO
static uint16_t pressSet; // keys down since the last time all were up
static uint8_t demoSlot, demoBatt, demoDongle;
uint8_t bt_demo_battery(void) { return demoBatt; }
#else
uint8_t bt_demo_battery(void) { return 0; }
#endif

static void sample(uint32_t now) {
  battery_sample(hal_battery_vdd_mv());
  sampleAt = now;
}

void bt_setup(uint32_t now, uint16_t keys) {
  btled_init();
  hal_btled_init();
  scanAt = now;
  down = 0;
  usbWas = 0xFF;
  sample(now);
#if BOARD_BLE && !BTLED_DEMO
  link = LINK_START;
  linkAt = now;
  chordMask = 0;
  chordFired = 0;
  // Show the current slot's LED for 1 s at power-on; after a slot-change reset
  // (hal_ble_select_slot) this is the feedback that the slot switched.
  btled_slot(hal_ble_slot(), now);
  pairMode = (keys >> BT_PAIR_KEY) & 1;
  if (pairMode) {
    hal_ble_forget(); // key 5 at power-on: re-pair this slot only
  }
#else
  (void)keys;
#endif
#if BTLED_DEMO
  pressSet = 0;
  demoSlot = demoBatt = demoDongle = 0;
  btled_slot(0, now);
#endif
}

#if BTLED_DEMO
// One key released alone: set the state it stands for
static void demoKey(uint8_t key, uint32_t now) {
  switch (key) {
  case 0:
    demoSlot = (uint8_t)((demoSlot + 1) % BTLED_SLOTS_BLE);
    demoDongle = 0;
    btled_link(BTLINK_IDLE, now);
    btled_slot(demoSlot, now);
    break;
  case 1:
  case 2:
  case 3:
    if (demoDongle) {
      demoDongle = 0;
      btled_select(demoSlot);
    }
    btled_link(key == 1 ? BTLINK_PAIRING : key == 2 ? BTLINK_RECONNECT : BTLINK_CONNECTED, now);
    break;
  case 4:
    // Dongle <-> USB cable: green only, no slot blue (2026-11-13 board check)
    demoDongle = !demoDongle;
    btled_link(BTLINK_IDLE, now);
    btled_select(demoDongle ? BTLED_SLOT_DONGLE : demoSlot);
    btled_link(demoDongle ? BTLINK_CONNECTED : BTLINK_IDLE, now);
    usbWas = 0xFF; // re-send the USB state below
    break;
  case 5:
    demoBatt = (uint8_t)((demoBatt + 1) % 3);
    if (demoBatt != 2) {
      btled_battery(0, 0, now); // red pattern restarts with the new state
    }
    break;
  }
}
#endif

#if BOARD_BLE && !BTLED_DEMO
static void advertise(uint32_t now) {
  uint8_t pair = pairMode || !hal_ble_bonded();
  hal_ble_start(pair);
  link = LINK_ADV;
  linkAt = now;
  btled_link(pair ? BTLINK_PAIRING : BTLINK_RECONNECT, now);
}

// pressed: keys that went down now (debounced)
static void linkTask(uint32_t now, uint16_t pressed, uint8_t usb) {
  uint8_t st = hal_ble_state();
  if (usb) {
    if (link != LINK_USB) {
      hal_ble_stop();
      link = LINK_USB;
      btled_link(BTLINK_IDLE, now);
    }
    return;
  }
  switch (link) {
  case LINK_START:
    if (now - linkAt >= BT_USB_WAIT_MS) {
      advertise(now);
    }
    break;
  case LINK_USB: // cable out (or the host asleep)
    advertise(now);
    break;
  case LINK_ADV:
    if (st == HAL_BLE_READY) {
      link = LINK_UP;
      pairMode = 0; // bonded now: later links reconnect to this host
      btled_link(BTLINK_CONNECTED, now);
    } else if (st != HAL_BLE_LINK && now - linkAt >= BT_ADV_TIMEOUT_MS) {
      // not while a host is pairing or encrypting the link
      hal_ble_stop();
      link = LINK_OFF;
      btled_link(BTLINK_IDLE, now);
    }
    break;
  case LINK_UP:
    if (st < HAL_BLE_LINK) { // link lost: look for the host again
      advertise(now);
    }
    break;
  default: // LINK_OFF
    if (pressed) {
      advertise(now);
    }
    break;
  }
}

// Switch to slot s. On the chip hal_ble_select_slot resets to re-init the radio
// with the new slot's address and does not return; after the reset bt_setup
// shows the slot LED and advertising starts fresh. (Off-chip, in the host test,
// the stub returns and the slot LED is shown here instead.)
static void selectSlot(uint8_t s, uint32_t now) {
  btled_slot(s, now); // blue slot LED for 1 s (shown only in the host test)
  hal_ble_select_slot(s);
}

// Re-pair the current slot: forget its host. On the chip hal_ble_forget bumps
// the slot's generation and resets (new address, then pairing from scratch), so
// it does not return; the advertise below runs only off-chip (the host test).
static void repairCurrent(uint32_t now) {
  hal_ble_forget();
  pairMode = 1;
  advertise(now);
}

// Slot chords: exactly key 6 + key 1/2/3 (pick slot), or key 5 + 6 (re-pair),
// held BT_CHORD_MS. Exact match, so another key down cancels it. Fires once.
static void chordTask(uint16_t keys, uint32_t now) {
  uint8_t valid = keys == BT_CHORD_SLOT1 || keys == BT_CHORD_SLOT2 ||
                  keys == BT_CHORD_SLOT3 || keys == BT_CHORD_REPAIR;
  if (!valid) {
    chordMask = 0;
    chordFired = 0;
    return;
  }
  if (keys != chordMask) {
    chordMask = keys;
    chordAt = now;
    chordFired = 0;
    return;
  }
  if (!chordFired && now - chordAt >= BT_CHORD_MS) {
    chordFired = 1;
    if (keys == BT_CHORD_REPAIR) {
      repairCurrent(now);
    } else {
      selectSlot(keys == BT_CHORD_SLOT1 ? 0 : keys == BT_CHORD_SLOT2 ? 1 : 2, now);
    }
  }
}
#endif

uint8_t bt_task(uint32_t now, uint16_t keys, uint8_t usb) {
  uint8_t low, crit;
  uint16_t pressed = 0;

  // Keys: sampled every BT_KEY_SCAN_MS
  if (now - scanAt >= BT_KEY_SCAN_MS) {
    scanAt = now;
    pressed = (uint16_t)(keys & ~down);
#if BTLED_DEMO
    if (pressed) {
      pressSet |= pressed;
    }
    if (!keys && down) { // all released
      uint8_t k;
      for (k = 0; k < 16; k++) {
        if (pressSet == (1u << k)) {
          demoKey(k, now);
        }
      }
      pressSet = 0;
    }
#endif
    if (pressed) {
      btled_key(now);
    }
    down = keys;
    if (btled_keys(down, now)) {
      btled_show_battery(battery_percent(), now);
    }
#if BOARD_BLE && !BTLED_DEMO
    chordTask(down, now);
#endif
  }

  // USB cable
#if BTLED_DEMO
  if (demoDongle) {
    usb = 0;
  }
#endif
  if (usb != usbWas) {
    usbWas = usb;
    btled_usb(usb);
    battery_usb(usb); // readings on the cable run high (charging)
  }
#if BOARD_BLE && !BTLED_DEMO
  linkTask(now, pressed, usb);
#else
  (void)pressed;
#endif

  // Battery (no radio yet: any time is quiet)
  if (now - sampleAt >= BATTERY_SAMPLE_MS) {
    sample(now);
  }
  low = battery_low();
  // On the cable the cell is charging: never sleep, the red blink is enough
  crit = usb ? 0 : battery_critical();
#if BTLED_DEMO
  if (demoBatt) { // shown on the cable too (the demo does not sleep)
    low = 1;
    crit = demoBatt == 2;
  }
#endif
  btled_battery(low, crit, now);

#if BOARD_BLE && !BTLED_DEMO && BLE_DIAG
  // After a key press, show two steady phases on the LEDs (keys still type; this
  // only overrides the display), with a dark gap between so they are easy to
  // tell apart:
  //   phase 1 (0..2.0 s): that key's real HID report result (hal_ble_diag_bits)
  //     L3 connected, L4 encrypted, L5 send failed, L6 sent, L7 CCCD off, L8 not secure
  //   gap    (2.0..2.3 s): all off
  //   phase 2 (2.3..4.3 s): SNV bond state (hal_ble_diag_bond)
  //     L3/L4/L5 = bond count (3-bit binary), L6 encryption reached, L7 security req sent
  if (pressed) {
    diagAt = now;
    diagOn = 1;
  }
  if (diagOn) {
    uint32_t e = now - diagAt;
    if (e < 2000) {
      hal_btled_write(hal_ble_diag_bits());
      return 0;
    } else if (e < 2300) {
      hal_btled_write(0); // dark gap
      return 0;
    } else if (e < 4300) {
      hal_btled_write(hal_ble_diag_bond());
      return 0;
    }
    diagOn = 0;
  }
#endif
  hal_btled_write(btled_update(now));
  if (btled_sleep_due()) {
#if BTLED_DEMO
    btled_init(); // demo: back to the real reading, no sleep
    demoBatt = 0;
    demoDongle = 0;
    usbWas = 0xFF;
    btled_select(demoSlot);
    return 0;
#else
    return 1;
#endif
  }
  return 0;
}

#endif
