// clang-format off
#include "../hal/hal.h"
#include "input.h"
#include "led.h"
#include "lock.h"
// clang-format on

#define SAVED_FLASH_MS 300
#define PRESS_FLASH_MS 200       // green at least this long after a press or knob turn
#define LOCKED_BLINK_PERIOD 3000 // locked: one short red blink per period
#define LOCKED_BLINK_MS 100
#define BLOCKED_BLINK_SHIFT 9    // blocked: blink period 2 x 512 ms
#define LED_REFRESH_MS 200
#define ENTRY_DARK_MS 150        // key LEDs: dark after a pattern step

#ifndef BOARD_LED_SELF_TEST
#define BOARD_LED_SELF_TEST 0
#endif
#if BOARD_LED_SELF_TEST
// At power-up every LED runs a smooth color wheel (red, yellow, green, cyan,
// blue, purple, red) SELF_TEST_CYCLES times (2 x 3 s), then goes off for a moment, so a dead color
// or LED is seen at once. Needs hal_leds_rgb (CH32V203 HAL).
#ifndef SELF_TEST_CYCLE_MS
#define SELF_TEST_CYCLE_MS 3000 // one turn of the wheel (-D to try others; chosen 2026-11-07)
#endif
#ifndef SELF_TEST_CYCLES
#define SELF_TEST_CYCLES 2
#endif
#define SELF_TEST_OFF_MS 300
#define SELF_TEST_SEG_MS ((SELF_TEST_CYCLE_MS + 5) / 6) // one sixth of a turn
#if SELF_TEST_CYCLE_MS * SELF_TEST_CYCLES + SELF_TEST_OFF_MS > 30000
#error self test too long for now16
#endif
static __idata uint8_t selfTestDone;

// Wheel position t (ms within a cycle) -> R, G, B in palette units (0..40)
static void wheel(uint16_t t) {
  uint8_t seg = (uint8_t)(t / SELF_TEST_SEG_MS);
  uint8_t up = (uint8_t)((uint16_t)(t % SELF_TEST_SEG_MS) * 40 / SELF_TEST_SEG_MS);
  uint8_t down = (uint8_t)(40 - up);
  switch (seg) {
  case 0: hal_leds_rgb(40, up, 0); break;   // red -> yellow
  case 1: hal_leds_rgb(down, 40, 0); break; // yellow -> green
  case 2: hal_leds_rgb(0, 40, up); break;   // green -> cyan
  case 3: hal_leds_rgb(0, down, 40); break; // cyan -> blue
  case 4: hal_leds_rgb(up, 0, 40); break;   // blue -> purple
  default: hal_leds_rgb(40, 0, seg == 5 ? down : 0); break; // purple -> red
  }
}
#endif

// R, G, B per COLOR_* value (hal.h)
__code uint8_t hal_palette[][3] = {
    {0, 0, 0},    // COLOR_OFF
    {0, 0, 40},   // COLOR_IDLE
    {0, 40, 0},   // COLOR_TYPING
    {40, 40, 40}, // COLOR_SAVED
    {40, 0, 40},  // COLOR_BOOT
    {40, 0, 0},   // COLOR_RED
};

static __idata uint8_t ledColor; // COLOR_OFF
static __idata uint16_t ledSentAt;
static __idata uint8_t savedFlash;
static __idata uint16_t savedAt;
static __idata uint8_t pressFlash;
static __idata uint16_t pressAt;
static __idata uint16_t blinkAt; // start of the current locked blink period
// Switches whose press typed (bit = switch, as input_down): green while down
static __idata hal_sw_t greenSw;
#if BOARD_KNOBS
#define SW_OF(slot)                                                            \
  ((slot) < BOARD_KEYS ? (hal_sw_t)(1u << (slot))                              \
   : (slot) == SLOT_KNOB_PRESS ? (hal_sw_t)(1u << BOARD_KEYS) : (hal_sw_t)0)
#else
#define SW_OF(slot) ((hal_sw_t)(1u << (slot)))
#endif

#if BOARD_KEY_LEDS
#if BOARD_LED_COUNT != BOARD_KEYS
#error key LEDs: one LED per key
#endif
// Key masks are hal_sw_t (8 bits on CH552, 16 on CH32V)
#define ALL_KEYS ((hal_sw_t)((1u << BOARD_KEYS) - 1))
static __idata hal_sw_t flashKeys; // LEDs of the actions in the current flash
static __idata hal_sw_t darkKeys;  // pressed in a pattern entry, dark for a moment
static __idata uint16_t darkAt;
static __idata hal_sw_t ledMask;   // LEDs shown in ledMaskColor instead of ledColor
static __idata uint8_t ledMaskColor;

// The key LED of an action; a knob action stands for all of them
#define KEY_LEDS_OF(slot) ((slot) < BOARD_KEYS ? (hal_sw_t)(1u << (slot)) : ALL_KEYS)

static void showLeds(uint8_t color, hal_sw_t mask, uint8_t maskColor) {
  // Resend periodically even without a change, so a frame lost at power-up
  // does not leave the LED dark
  if (color == ledColor && mask == ledMask && maskColor == ledMaskColor &&
      (uint16_t)(now16 - ledSentAt) < LED_REFRESH_MS) {
    return;
  }
  ledColor = color;
  ledMask = mask;
  ledMaskColor = maskColor;
  hal_leds(color, mask, maskColor);
  ledSentAt = now16;
}

static void showColor(uint8_t color) { showLeds(color, 0, COLOR_OFF); }

void led_entered(uint8_t slot) {
  darkKeys = KEY_LEDS_OF(slot);
  darkAt = now16;
}
#else
static void showColor(uint8_t color) {
  // Resend periodically even without a change, so a frame lost at power-up
  // does not leave the LED dark
  if (color == ledColor && (uint16_t)(now16 - ledSentAt) < LED_REFRESH_MS) {
    return;
  }
  ledColor = color;
  hal_leds(color, 0, COLOR_OFF);
  ledSentAt = now16;
}
#endif

void led_typed(uint8_t slot) {
#if BOARD_KEY_LEDS
  if (!pressFlash) {
    flashKeys = 0;
  }
  flashKeys |= KEY_LEDS_OF(slot);
#endif
  pressFlash = 1;
  pressAt = now16;
  greenSw |= SW_OF(slot);
}

void led_saved(void) {
  savedFlash = 1;
  savedAt = now16;
}

void led_boot(void) { hal_leds(COLOR_BOOT, 0, COLOR_OFF); }

void led_task(void) {
#if BOARD_LED_SELF_TEST
  if (!selfTestDone) {
    // now16 counts from power-up; the test ends before it can wrap
    if (now16 < SELF_TEST_CYCLE_MS * SELF_TEST_CYCLES) {
      wheel(now16 % SELF_TEST_CYCLE_MS);
      return;
    }
    if (now16 < SELF_TEST_CYCLE_MS * SELF_TEST_CYCLES + SELF_TEST_OFF_MS) {
      showColor(COLOR_OFF);
      return;
    }
    selfTestDone = 1;
  }
#endif
  if (savedFlash) {
    if ((uint16_t)(now16 - savedAt) < SAVED_FLASH_MS) {
      showColor(COLOR_SAVED);
      return;
    }
    savedFlash = 0;
  }
  if (pressFlash && (uint16_t)(now16 - pressAt) >= PRESS_FLASH_MS) {
    pressFlash = 0;
  }
  greenSw &= input_down; // released: back to blue
  if (lock_state() == LOCK_BLOCKED) {
    showColor((now16 >> BLOCKED_BLINK_SHIFT) & 1 ? COLOR_OFF : COLOR_RED);
  } else if (lock_entering()) {
#if BOARD_KEY_LEDS
    // The key just entered goes dark for a moment (a knob step: all)
    if ((uint16_t)(now16 - darkAt) >= ENTRY_DARK_MS) {
      darkKeys = 0;
    }
    showLeds(COLOR_RED, darkKeys, COLOR_OFF);
#else
    showColor(COLOR_RED);
#endif
  } else if (lock_state() == LOCK_LOCKED) {
    // 16-bit period counter instead of now % period (32-bit modulo)
    uint16_t sinceBlink = now16 - blinkAt;
    if (sinceBlink >= LOCKED_BLINK_PERIOD) {
      blinkAt += sinceBlink; // restart the period now, also after a long gap
      sinceBlink = 0;
    }
    showColor(sinceBlink < LOCKED_BLINK_MS ? COLOR_RED : COLOR_OFF);
  } else {
    // Open: green from the press until the release (at least
    // PRESS_FLASH_MS, also for a knob turn), blue otherwise. Typing that
    // goes on after the release shows blue.
#if BOARD_KEY_LEDS
    hal_sw_t green = greenSw & ALL_KEYS;
#if BOARD_KNOBS
    if (greenSw >> BOARD_KEYS) {
      green = ALL_KEYS; // the knob switch stands for all LEDs
    }
#endif
    if (pressFlash) {
      green |= flashKeys;
    }
    if (green) {
      showLeds(COLOR_IDLE, green, COLOR_TYPING);
    } else {
      showColor(COLOR_IDLE);
    }
#else
    showColor(pressFlash || greenSw ? COLOR_TYPING : COLOR_IDLE);
#endif
  }
}
