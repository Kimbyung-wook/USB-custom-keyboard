/*
  test_ch32v203: hardware test for any CH32V203C8 (48-pin) macro keyboard:
  the 9-key SAYO_3X3 and 8-key SAYO_2X4 so far, the 12-key SAYO_2X6 the
  same way. Finds the key pins and the LED pins of a board whose pin map is
  unknown; it knows no key count or boot key, it walks all 31 usable pins.
  The LEDs found so far are R/G/B cathode lines per row or group (low = lit)
  plus one anode pin per key (high = lit), not a WS2812 chain. A WS2812 data
  pin would show nothing in FIND/ANODE (it needs a timed signal).

  Portable logic only. The chip access is in ch32v203_hal.h (the SDK layer).
  Build and flash: see README.md.

  A short press is a press released within 1 s; holding any key for 1 s is a
  long press. Every mode change is typed as "MODE: ...":
                     KEYS -> FIND -> ANODE -> KEYS ...
      KEYS : (start) every press types the pin name, "PA00", and every release
             "PA00 up". Press every key once here first, in order 1, 2, ...:
             FIND and ANODE never touch those pins. A key wired between two
             pins (a row/column matrix, no ground) types "M PA00 PB01" and
             "M PA00 PB01 up" instead (see matrixScan).
      FIND : (cathode lines) every other pin is driven low for 1 s ("L"), then
             high for 1 s ("H"), then released; all other pins stay pulled up.
             A cathode line glows dimly during L (current from the pulled-up
             anodes). "LED? PB03" is typed first.
      ANODE: (per-key anodes) every other pin is pulled down weakly; each in
             turn is driven high for 2 s ("HI PB03"). An anode lights its key
             (dimly, through the pulled-down cathodes).
  Report which pin lights which key, the color, and L or H.
  PB6/PB7 (USB D-/D+) and PA11/PA12 (USBD) are never driven.
*/

#include <stdint.h>
#include "ch32v203_hal.h"

// Usable pins of the CH32V203C8U6 (QFN-48): PA0-PA10, PA13-PA15, PB0-PB5, PB8-PB15,
// PC13-PC15. Left out: PA11/PA12 (USBD), PB6/PB7 (USBFS D-/D+, this firmware's USB).
// PA13-PA15 and PB3/PB4 (SWD/JTAG) are included because the HAL turns SWJ off.
#define NUM_PINS 31
static const uint8_t pinPort[NUM_PINS] = {
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // PA0-PA10
  0, 0,                             // PA13, PA14 (SWD pins, free: SWJ is off)
  0,                                // PA15
  1, 1, 1, 1, 1, 1,                 // PB0-PB5
  1, 1, 1, 1, 1, 1, 1, 1,           // PB8-PB15
  2, 2, 2,                          // PC13-PC15
};
static const uint8_t pinBit[NUM_PINS] = {
  0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
  13, 14,
  15,
  0, 1, 2, 3, 4, 5,
  8, 9, 10, 11, 12, 13, 14, 15,
  13, 14, 15,
};

#define NO_PIN 0xFF
#define DEBOUNCE_MS 20
#define LONG_MS 1000

static uint8_t inputStable[NUM_PINS];
static uint8_t inputLast[NUM_PINS];
static uint32_t inputChangedAt[NUM_PINS];
static uint8_t seenKey[NUM_PINS]; // pressed at least once in KEYS
static uint8_t stuck[NUM_PINS];   // already low at start: left alone (never driven)

static uint8_t heldIndex = NO_PIN;
static uint32_t heldAt = 0;
static uint8_t heldLong = 0;

#define MODE_KEYS 0
#define MODE_FIND 1
#define MODE_ANODE 2
#define NUM_MODES 3
static const char *modeNames[NUM_MODES] = {"KEYS", "FIND", "ANODE"};

#define ANODE_MS 2000
#define ANODE_GAP_MS 500

#define PHASE_MS 1000

static uint8_t mode = MODE_KEYS;
static uint8_t findIndex = NO_PIN; // pin being driven in FIND
static uint8_t findHigh = 0;      // 0 = driven low, 1 = driven high
static uint32_t stepAt = 0;
static uint8_t anodeLast = NO_PIN; // ANODE: last pin driven

// Matrix keys: a key between two GPIO pins instead of a pin and ground. Every
// MATRIX_MS each candidate pin in turn is driven low for a few microseconds;
// another pin that follows it low is joined to it through a pressed key.
// Pairs that read low with nothing pressed (an LED from a pulled-up pin, a
// resistor) are learned at start (matrixLearn) and ignored. In KEYS every free
// pin is tried; a pair once pressed becomes key pins (seenKey, matrixPin), and
// in FIND/ANODE only those are scanned, so a matrix key can change the mode.
// Pins: bit j of mask[i] (i < j) = pins i and j joined.
#define MATRIX_MS 20
static uint8_t matrixPin[NUM_PINS];
static uint32_t matrixBase[NUM_PINS];   // joined with nothing pressed: ignored
static uint32_t matrixLast[NUM_PINS];   // last scan
static uint32_t matrixStable[NUM_PINS]; // debounced (two equal scans)
static uint32_t matrixAt = 0;

static void typePin(uint8_t i) {
  hal_hid_write('P');
  hal_hid_write('A' + pinPort[i]);
  hal_hid_write(pinBit[i] >= 10 ? '1' : '0');
  hal_hid_write('0' + pinBit[i] % 10);
}

static void typeHex(uint32_t v) {
  for (int k = 28; k >= 0; k -= 4) {
    uint8_t d = (v >> k) & 15;
    hal_hid_write(d < 10 ? '0' + d : 'a' + d - 10);
  }
}

// Drive a pin as an output, low first
static void claimPinAt(uint8_t port, uint8_t bit) {
  hal_gpio_output(port, bit);
  hal_gpio_write(port, bit, 0);
}

static void claimPin(uint8_t i) { claimPinAt(pinPort[i], pinBit[i]); }

static void releasePin(uint8_t i) {
  hal_gpio_input_pullup(pinPort[i], pinBit[i]);
  inputStable[i] = 0;
  inputLast[i] = 0;
}

// FIND: from the pin after findIndex, the next one that was not a key
static void findNext(void) {
  uint8_t i = findIndex;

  if (findIndex != NO_PIN) {
    releasePin(findIndex);
  }
  for (uint8_t n = 0; n < NUM_PINS; n++) {
    if (i == NO_PIN || i + 1 == NUM_PINS) {
      i = 0;
      if (findIndex != NO_PIN) {
        hal_hid_print("LOOP\n");
      }
    } else {
      i++;
    }
    if (!seenKey[i]) { // a stuck pin may be an LED line (pulled low), so it is tried too
      findIndex = i;
      // Announce first: if driving this pin resets the chip, the name is already typed
      hal_hid_print("LED? ");
      typePin(i);
      hal_hid_write('\n');
      hal_delay_ms(60);
      claimPin(i);
      findHigh = 0;
      hal_hid_print("L\n");
      stepAt = hal_millis();
      return;
    }
  }
  findIndex = NO_PIN;
  hal_hid_print("NO FREE PIN\n");
}

// ANODE: the pin after findIndex that was not a key or stuck; NO_PIN at the end
static uint8_t candidateAfter(uint8_t i) {
  for (i = i == NO_PIN ? 0 : i + 1; i < NUM_PINS; i++) {
    if (!seenKey[i] && !stuck[i]) {
      return i;
    }
  }
  return NO_PIN;
}

static void anodeAllDown(void) {
  for (uint8_t i = 0; i < NUM_PINS; i++) {
    if (!seenKey[i] && !stuck[i]) {
      hal_gpio_input_pulldown(pinPort[i], pinBit[i]);
    }
  }
}

// ANODE step: the driven pin goes back to a pull-down, after a gap the next is driven
static void anodeTask(void) {
  uint32_t now = hal_millis();

  if (findIndex == NO_PIN) {
    if (now - stepAt < ANODE_GAP_MS) {
      return;
    }
    findIndex = candidateAfter(anodeLast);
    if (findIndex == NO_PIN) {
      hal_hid_print("LOOP\n");
      anodeLast = NO_PIN;
      stepAt = now;
      return;
    }
    hal_hid_print("HI ");
    typePin(findIndex);
    hal_hid_write('\n');
    hal_gpio_output(pinPort[findIndex], pinBit[findIndex]);
    hal_gpio_write(pinPort[findIndex], pinBit[findIndex], 1);
    stepAt = hal_millis();
  } else if (now - stepAt >= ANODE_MS) {
    hal_gpio_input_pulldown(pinPort[findIndex], pinBit[findIndex]);
    anodeLast = findIndex;
    findIndex = NO_PIN;
    stepAt = hal_millis();
  }
}

static void setMode(uint8_t m) {
  if (mode == MODE_FIND && findIndex != NO_PIN) {
    releasePin(findIndex);
  }
  if (mode == MODE_ANODE) { // every candidate back to a pull-up
    for (uint8_t i = 0; i < NUM_PINS; i++) {
      if (!seenKey[i] && !stuck[i]) {
        releasePin(i);
      }
    }
  }
  findIndex = NO_PIN;
  mode = m;
  stepAt = hal_millis();

  hal_hid_print("MODE: ");
  hal_hid_print(modeNames[m]);
  hal_hid_write('\n');
  if (m == MODE_FIND) {
    findNext();
  } else if (m == MODE_ANODE) {
    anodeLast = NO_PIN;
    anodeAllDown();
  }
}

static void updateLeds(void) {
  uint32_t now = hal_millis();

  if (mode != MODE_FIND || findIndex == NO_PIN || now - stepAt < PHASE_MS) {
    return;
  }
  if (!findHigh) {
    findHigh = 1;
    hal_gpio_write(pinPort[findIndex], pinBit[findIndex], 1);
    hal_hid_print("H\n");
    stepAt = hal_millis();
  } else {
    findNext();
  }
}

// Pins this mode pulls or drives itself: never read as keys
static uint8_t isOutput(uint8_t i) {
  if (i == findIndex) {
    return 1;
  }
  if (mode == MODE_ANODE && !seenKey[i]) {
    return 1;
  }
  return 0;
}

static void settle(void) {
  for (volatile uint16_t n = 0; n < 200; n++) {
  }
}

// Pins the matrix scan may drive or read now
static uint8_t matrixCandidate(uint8_t i) {
  if (stuck[i] || isOutput(i)) {
    return 0;
  }
  return mode == MODE_KEYS ? 1 : matrixPin[i];
}

// One scan: out[i] bit j (i < j) = j read low while i was driven low, or the
// reverse (a key with a diode conducts one way only). Pins already low with
// nothing driven (a pressed direct key) are left out.
static void matrixScan(uint32_t out[NUM_PINS]) {
  uint8_t idleLow[NUM_PINS];

  for (uint8_t i = 0; i < NUM_PINS; i++) {
    out[i] = 0;
    idleLow[i] = matrixCandidate(i) && !hal_gpio_read(pinPort[i], pinBit[i]);
  }
  for (uint8_t i = 0; i < NUM_PINS; i++) {
    if (!matrixCandidate(i) || idleLow[i]) {
      continue;
    }
    claimPin(i);
    settle();
    for (uint8_t j = 0; j < NUM_PINS; j++) {
      if (j != i && matrixCandidate(j) && !idleLow[j] && !hal_gpio_read(pinPort[j], pinBit[j])) {
        if (i < j) {
          out[i] |= 1u << j;
        } else {
          out[j] |= 1u << i;
        }
      }
    }
    hal_gpio_write(pinPort[i], pinBit[i], 1); // pull the line back up fast
    hal_gpio_input_pullup(pinPort[i], pinBit[i]);
    settle();
  }
}

// At start, nothing pressed: every pair found now is a fixed connection
static void matrixLearn(void) {
  uint32_t m[NUM_PINS];

  for (uint8_t n = 0; n < 5; n++) {
    matrixScan(m);
    for (uint8_t i = 0; i < NUM_PINS; i++) {
      matrixBase[i] |= m[i];
    }
    hal_delay_ms(2);
  }
}

static void typePair(uint8_t i, uint8_t j) {
  hal_hid_print("M ");
  typePin(i);
  hal_hid_write(' ');
  typePin(j);
}

static void matrixTask(void) {
  uint32_t now = hal_millis();
  uint32_t m[NUM_PINS];

  if (now - matrixAt < MATRIX_MS) {
    return;
  }
  matrixAt = now;
  matrixScan(m);

  for (uint8_t i = 0; i < NUM_PINS; i++) {
    uint32_t cur = m[i] & ~matrixBase[i];
    // A pair changes state after two equal scans
    uint32_t settled = ~(cur ^ matrixLast[i]);
    uint32_t flip = (cur ^ matrixStable[i]) & settled;
    matrixLast[i] = cur;
    if (!flip) {
      continue;
    }
    for (uint8_t j = i + 1; j < NUM_PINS; j++) {
      uint8_t pressed;

      if (!(flip >> j & 1)) {
        continue;
      }
      pressed = cur >> j & 1;
      matrixStable[i] ^= 1u << j;
      if (mode == MODE_KEYS) {
        if (pressed) {
          seenKey[i] = seenKey[j] = 1;
          matrixPin[i] = matrixPin[j] = 1;
        }
        typePair(i, j);
        hal_hid_print(pressed ? "\n" : " up\n");
      }
      // Long press to change the mode, as for a direct key (held by pin i)
      if (pressed && heldIndex == NO_PIN) {
        heldIndex = i;
        heldAt = hal_millis();
        heldLong = 0;
      } else if (!pressed && i == heldIndex) {
        heldIndex = NO_PIN;
      }
    }
  }
}

static void scanInputs(void) {
  uint32_t now = hal_millis();

  for (uint8_t i = 0; i < NUM_PINS; i++) {
    uint8_t pressed;

    if (isOutput(i) || stuck[i] || matrixPin[i]) {
      continue;
    }
    pressed = !hal_gpio_read(pinPort[i], pinBit[i]);

    if (pressed != inputLast[i]) {
      inputLast[i] = pressed;
      inputChangedAt[i] = now;
      continue;
    }
    if (pressed == inputStable[i] || now - inputChangedAt[i] < DEBOUNCE_MS) {
      continue;
    }
    inputStable[i] = pressed;

    if (mode == MODE_KEYS) {
      if (pressed) {
        seenKey[i] = 1;
      }
      typePin(i);
      hal_hid_print(pressed ? "\n" : " up\n");
    }
    if (pressed && heldIndex == NO_PIN) {
      heldIndex = i;
      heldAt = now;
      heldLong = 0;
    } else if (!pressed && i == heldIndex) {
      heldIndex = NO_PIN;
    }
  }

  if (heldIndex != NO_PIN && !heldLong && now - heldAt >= LONG_MS) {
    heldLong = 1;
    setMode(mode + 1 == NUM_MODES ? 0 : mode + 1);
  }
}

int main(void) {
  hal_init();

  for (uint8_t i = 0; i < NUM_PINS; i++) {
    hal_gpio_input_pullup(pinPort[i], pinBit[i]);
  }

  // Give the host time to set up the keyboard before typing the first mode
  hal_delay_ms(1500);

  // Why did the chip start? (N pin, O power-on, S software/fault, I/W watchdog, L low power)
  {
    uint8_t c = hal_reset_cause();
    hal_hid_print("RST ");
    if (c & 1) hal_hid_write('N');
    if (c & 2) hal_hid_write('O');
    if (c & 4) hal_hid_write('S');
    if (c & 8) hal_hid_write('I');
    if (c & 16) hal_hid_write('W');
    if (c & 32) hal_hid_write('L');
    hal_hid_write('\n');
  }
  {
    uint32_t f[3];
    if (hal_fault_read(f)) { // the last start came from a CPU exception
      hal_hid_print("FLT cause "); typeHex(f[0]);
      hal_hid_print(" pc "); typeHex(f[1]);
      hal_hid_print(" val "); typeHex(f[2]);
      hal_hid_write('\n');
    }
  }
  // A pin that is low with nothing pressed is wired low: report it, never drive it
  for (uint8_t i = 0; i < NUM_PINS; i++) {
    if (!hal_gpio_read(pinPort[i], pinBit[i])) {
      stuck[i] = 1;
      hal_hid_print("STUCK ");
      typePin(i);
      hal_hid_write('\n');
    }
  }
  matrixLearn();
  setMode(MODE_KEYS);

  while (1) {
    matrixTask();
    scanInputs();
    updateLeds();
    if (mode == MODE_ANODE) {
      anodeTask();
    }
  }
}
