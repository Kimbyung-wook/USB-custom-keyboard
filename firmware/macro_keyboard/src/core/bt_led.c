// Status LEDs of a wireless board (bt_led.h). Empty on boards without
// BOARD_BT_LED (the Arduino IDE compiles every file in src).
#include "bt_led.h"

#ifdef BOARD_BT_LED

static uint8_t slot, link, wired, keyFlash;
static uint8_t slotShow, keyShow, battShow, low, crit, sleepDue, held, fired;
static uint8_t battBits;
static uint32_t slotAt, linkAt, keyAt, battAt, lowAt, critAt, holdAt;

void btled_init(void) {
  slot = 0;
  link = BTLINK_IDLE;
  wired = 0;
  keyFlash = 1;
  slotShow = keyShow = battShow = low = crit = sleepDue = held = fired = 0;
}

uint8_t btled_slot_bit(uint8_t s) {
  if (s >= BTLED_SLOTS_BLE) {
    return BTLED_GREEN;
  }
  return (uint8_t)(BTLED_BLUE1 << s);
}

void btled_slot(uint8_t s, uint32_t now) {
  slot = s;
  slotShow = 1;
  slotAt = now;
}

void btled_select(uint8_t s) {
  slot = s;
  slotShow = 0;
}

void btled_link(uint8_t state, uint32_t now) {
  if (state != link) {
    link = state;
    linkAt = now;
  }
}

void btled_usb(uint8_t on) { wired = on; }

void btled_key(uint32_t now) {
  keyShow = 1;
  keyAt = now;
}

void btled_key_flash(uint8_t on) { keyFlash = on; }

void btled_battery(uint8_t isLow, uint8_t isCritical, uint32_t now) {
  if (isLow && !low) {
    lowAt = now; // first blip right away
  }
  low = isLow;
  if (isCritical && !crit) {
    crit = 1; // latched: blink, then sleep
    critAt = now;
  }
}

void btled_show_battery(uint8_t percent, uint32_t now) {
  if (percent > BTLED_BATT_3_PCT) {
    battBits = BTLED_BLUE1 | BTLED_BLUE2 | BTLED_BLUE3;
  } else if (percent > BTLED_BATT_2_PCT) {
    battBits = BTLED_BLUE1 | BTLED_BLUE2;
  } else {
    battBits = BTLED_BLUE1;
  }
  battShow = 1;
  battAt = now;
}

uint8_t btled_keys(uint16_t keys, uint32_t now) {
  if ((keys & BTLED_BATT_KEYS) != BTLED_BATT_KEYS) {
    held = 0;
    return 0;
  }
  if (!held) {
    held = 1;
    fired = 0;
    holdAt = now;
  }
  if (!fired && now - holdAt >= BTLED_BATT_HOLD_MS) {
    fired = 1;
    return 1;
  }
  return 0;
}

uint8_t btled_sleep_due(void) { return sleepDue; }

// Add LEDs one at a time while fewer than BTLED_MAX_ON are on
static uint8_t add(uint8_t bits, uint8_t want) {
  uint8_t b, n = 0;
  for (b = 1; b & 0x3F; b <<= 1) {
    n += (bits & b) != 0;
  }
  for (b = 1; b & 0x3F; b <<= 1) {
    if ((want & b) && !(bits & b) && n < BTLED_MAX_ON) {
      bits |= b;
      n++;
    }
  }
  return bits;
}

// on/off blinking: on for the first toggle time, off for the next
static uint8_t blink(uint32_t elapsed, uint32_t toggle) { return (elapsed / toggle) % 2 == 0; }

uint8_t btled_update(uint32_t now) {
  uint8_t bits = 0, s = btled_slot_bit(slot);

  // Critical battery: only red, 3 fast blinks, then sleep
  if (crit) {
    uint32_t e = now - critAt;
    if (e >= 2UL * BTLED_CRIT_BLINKS * BTLED_CRIT_TOGGLE_MS) {
      sleepDue = 1;
      return 0;
    }
    return blink(e, BTLED_CRIT_TOGGLE_MS) ? BTLED_RED : 0;
  }

  // In priority order, highest first
  if (battShow) {
    if (now - battAt < BTLED_BATT_MS) {
      bits = add(bits, battBits);
    } else {
      battShow = 0;
    }
  }
  if (slotShow) {
    if (now - slotAt < BTLED_SLOT_MS) {
      bits = add(bits, s);
    } else {
      slotShow = 0;
    }
  }
  if (wired) {
    bits = add(bits, BTLED_GREEN);
  }
  // Link state: blue for a BLE slot (also on the cable), green for the dongle
  // (not on the cable: green is the cable then); the battery check owns the blue LEDs
  if (!(wired && s == BTLED_GREEN) && !(battShow && s != BTLED_GREEN)) {
    uint32_t e = now - linkAt;
    if ((link == BTLINK_PAIRING && blink(e, BTLED_PAIR_TOGGLE_MS)) ||
        (link == BTLINK_RECONNECT && blink(e, BTLED_RECONN_TOGGLE_MS)) ||
        (link == BTLINK_CONNECTED && e < BTLED_CONNECTED_MS)) {
      bits = add(bits, s);
    }
  }
  if (low && (now - lowAt) % BTLED_LOW_PERIOD_MS < BTLED_LOW_ON_MS) {
    bits = add(bits, BTLED_RED);
  }
  if (keyShow) {
    if (now - keyAt < BTLED_KEY_MS) {
      if (keyFlash) {
        bits = add(bits, BTLED_WHITE);
      }
    } else {
      keyShow = 0;
    }
  }
  return bits;
}

#endif
