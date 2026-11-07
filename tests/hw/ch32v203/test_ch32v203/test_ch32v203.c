/*
  test_ch32v203: hardware test for the 8-key (2 x 4) and 9-key macro keyboards.
  Both use CH32V203C8U6 (RISC-V, USB full speed, 64 KB flash). The LEDs of the
  9-key board are plain R/G/B lines, one pin per color per row of 3 keys, not a
  WS2812 chain, so the LED search drives each pin directly.

  Portable logic only. The chip access is in ch32v203_hal.h (the SDK layer).
  Build and flash: see README.md.

  A short press is a press released within 1 s; holding any key for 1 s is a
  long press. Every mode change is typed as "MODE: ...":
                     KEYS -> PWR -> FIND -> DUMP -> BOOT -> KEYS ...
      BOOT : ISP bootloader without the BOOT bridge. Press key 1: jump
             from the application, key 2: RAM mark + reset, the reset handler
             jumps, key 3: FLASH_STATR bit 14 + reset ("FLASH before after").
      KEYS : (start) every press types the pin name, "PA0", and every release
             "PA0 up". Press every key once here first: FIND skips those pins.
      PWR  : (3X3V_RGB) looks for the pins that power the LEDs. Every row is
             lit blue. Step 1: each free pin in turn is pulled down for 3 s
             ("DOWN PB05"), then back up. Step 2: all free pins pulled down
             ("ALL DOWN"), then each alone pulled up for 3 s ("UP PB05").
             Report which keys go dark (step 1) or light (step 2) for each
             pin. Weak pulls only, never driven. Repeats until a long press.
      FIND : every pin that was not a key is driven low for 1 s ("L" typed),
             then high for 1 s ("H" typed), then released. "LED? PB3" is
             typed first. Report which pin lights which key, color, and
             whether it is lit during L or H.
*/

#include <stdint.h>
#include "ch32v203_hal.h"

// Usable pins of the CH32V203C8U6 (QFN-48): PA0-PA10, PA13-PA15, PB0-PB15, PC13-PC15.
// PA11/PA12 are USB D-/D+ and are not in the list. PA13/PA14 (SWD) are included because
// the HAL turns SWJ off.
#define NUM_PINS 33
static const uint8_t pinPort[NUM_PINS] = {
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // PA0-PA10
  0, 0,                             // PA13, PA14 (SWD pins, free: SWJ is off)
  0,                                // PA15
  1, 1, 1, 1, 1, 1, 1, 1,           // PB0-PB7
  1, 1, 1, 1, 1, 1, 1, 1,           // PB8-PB15
  2, 2, 2,                          // PC13-PC15
};
static const uint8_t pinBit[NUM_PINS] = {
  0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
  13, 14,
  15,
  0, 1, 2, 3, 4, 5, 6, 7,
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
#define MODE_PWR 1
#define MODE_FIND 2
#define MODE_DUMP 3
#define MODE_BOOT 4
#define NUM_MODES 5
static const char *modeNames[NUM_MODES] = {"KEYS", "PWR", "FIND", "DUMP", "BOOT"};

// PWR (3X3V_RGB): the free pins, i.e. not a key, LED line or USB (PB6/PB7)
#define NUM_PWR 15
static const uint8_t pwrPort[NUM_PWR] = {0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2};
static const uint8_t pwrBit[NUM_PWR] = {5, 6, 7, 0, 3, 4, 5, 8, 9, 12, 13, 14, 13, 14, 15};
// Blue line of each row: lit during PWR
#define NUM_BLUE 3
static const uint8_t bluePort[NUM_BLUE] = {0, 0, 0};
static const uint8_t blueBit[NUM_BLUE] = {13, 10, 2};
#define PWR_STEP_MS 3000
#define PWR_GAP_MS 1000
static uint8_t pwrStage; // 0 step 1, 1 step 2
static uint8_t pwrIndex;
static uint8_t pwrActive; // pin under test is pulled (1) or in the gap (0)

#define PHASE_MS 1000

static uint8_t mode = MODE_KEYS;
static uint8_t findIndex = NO_PIN; // pin being driven in FIND
static uint8_t findHigh = 0;      // 0 = driven low, 1 = driven high
static uint32_t stepAt = 0;

static void typePin(uint8_t i) {
  hal_hid_write('P');
  hal_hid_write('A' + pinPort[i]);
  hal_hid_write(pinBit[i] >= 10 ? '1' : '0');
  hal_hid_write('0' + pinBit[i] % 10);
}

static void typePort(uint8_t port, uint8_t bit) {
  hal_hid_write('P');
  hal_hid_write('A' + port);
  hal_hid_write(bit >= 10 ? '1' : '0');
  hal_hid_write('0' + bit % 10);
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

// Type system flash words (ISP bootloader code) as "address: w w w w" lines
static void dumpWords(uint32_t from, uint32_t count) {
  for (uint32_t a = from; a < from + count * 4; a += 16) {
    typeHex(a);
    hal_hid_write(':');
    for (uint32_t k = 0; k < 4 && a + k * 4 < from + count * 4; k++) {
      hal_hid_write(' ');
      typeHex(*(volatile uint32_t *)(a + k * 4));
    }
    hal_hid_write('\n');
  }
}

static void pwrAll(uint8_t up) {
  for (uint8_t k = 0; k < NUM_PWR; k++) {
    if (up) {
      hal_gpio_input_pullup(pwrPort[k], pwrBit[k]);
    } else {
      hal_gpio_input_pulldown(pwrPort[k], pwrBit[k]);
    }
  }
}

static void pwrBlue(uint8_t on) {
  for (uint8_t k = 0; k < NUM_BLUE; k++) {
    if (on) {
      claimPinAt(bluePort[k], blueBit[k]);
    } else {
      hal_gpio_input_pullup(bluePort[k], blueBit[k]);
    }
  }
}

// Start step `stage` with its first pin
static void pwrStart(uint8_t stage) {
  pwrStage = stage;
  pwrIndex = 0;
  pwrActive = 0;
  pwrAll(stage == 0);
  hal_hid_print(stage == 0 ? "STEP 1: DOWN one by one\n" : "STEP 2: ALL DOWN, UP one by one\n");
  stepAt = hal_millis();
}

static void pwrTask(void) {
  uint32_t now = hal_millis();

  if (now - stepAt < (pwrActive ? PWR_STEP_MS : PWR_GAP_MS)) {
    return;
  }
  if (pwrActive) { // end of a pin: back to the stage's resting pull
    if (pwrStage == 0) {
      hal_gpio_input_pullup(pwrPort[pwrIndex], pwrBit[pwrIndex]);
    } else {
      hal_gpio_input_pulldown(pwrPort[pwrIndex], pwrBit[pwrIndex]);
    }
    pwrActive = 0;
    if (++pwrIndex == NUM_PWR) {
      pwrStart(pwrStage ^ 1);
      return;
    }
  } else {
    hal_hid_print(pwrStage == 0 ? "DOWN " : "UP ");
    typePort(pwrPort[pwrIndex], pwrBit[pwrIndex]);
    hal_hid_write('\n');
    if (pwrStage == 0) {
      hal_gpio_input_pulldown(pwrPort[pwrIndex], pwrBit[pwrIndex]);
    } else {
      hal_gpio_input_pullup(pwrPort[pwrIndex], pwrBit[pwrIndex]);
    }
    pwrActive = 1;
  }
  stepAt = hal_millis();
}

static void setMode(uint8_t m) {
  if (mode == MODE_FIND && findIndex != NO_PIN) {
    releasePin(findIndex);
    findIndex = NO_PIN;
  }
  if (mode == MODE_PWR) {
    pwrBlue(0);
    pwrAll(1);
  }
  mode = m;
  stepAt = hal_millis();

  hal_hid_print("MODE: ");
  hal_hid_print(modeNames[m]);
  hal_hid_write('\n');
  if (m == MODE_PWR) {
    pwrBlue(1);
    pwrStart(0);
  } else if (m == MODE_FIND) {
    findNext();
  } else if (m == MODE_DUMP) {
    dumpWords(0x1FFF8000u, 16);
    dumpWords(0x1FFFAC80u, 32);
    hal_hid_print("END\n");
  } else if (m == MODE_BOOT) {
    // Experiments: start the ISP bootloader without the BOOT bridge, one per key
    hal_hid_print("1 JUMP, 2 RESET, 3 FLASH\n");
  }
}

// BOOT mode, press of key 1/2/3 (3X3V_RGB pins PB15, PA14, PA4)
static void bootTry(uint8_t i) {
  uint8_t port = pinPort[i], bit = pinBit[i];

  if (port == 1 && bit == 15) {
    hal_hid_print("JUMP\n");
    hal_delay_ms(300);
    hal_jump_bootloader();
  } else if (port == 0 && bit == 14) {
    hal_hid_print("RESET\n");
    hal_delay_ms(300);
    hal_reset_to_bootloader();
  } else if (port == 0 && bit == 4) {
    uint32_t before, after = hal_flash_boot_mode(&before);
    hal_hid_print("FLASH ");
    typeHex(before);
    hal_hid_write(' ');
    typeHex(after);
    hal_hid_write('\n');
    hal_delay_ms(300);
    hal_flash_boot_reset();
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
  if (mode == MODE_PWR) {
    for (uint8_t k = 0; k < NUM_PWR; k++) {
      if (pinPort[i] == pwrPort[k] && pinBit[i] == pwrBit[k]) {
        return 1;
      }
    }
    for (uint8_t k = 0; k < NUM_BLUE; k++) {
      if (pinPort[i] == bluePort[k] && pinBit[i] == blueBit[k]) {
        return 1;
      }
    }
  }
  return 0;
}

static void scanInputs(void) {
  uint32_t now = hal_millis();

  for (uint8_t i = 0; i < NUM_PINS; i++) {
    uint8_t pressed;

    if (isOutput(i) || stuck[i]) {
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
      if (mode == MODE_BOOT) {
        bootTry(i); // on press: keys 1-3 leave the mode at once, 4-9 still go on by a long press
      }
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
  setMode(MODE_KEYS);

  while (1) {
    scanInputs();
    updateLeds();
    if (mode == MODE_PWR) {
      pwrTask();
    }
  }
}
