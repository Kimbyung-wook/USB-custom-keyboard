// clang-format off
#include "../hal/hal.h"
#include "input.h"
#include "led.h"
#include "lock.h"
#include "macro.h"
#include "storage.h"
// clang-format on

// hal_ms() read once per loop() (a 32-bit millis() read on CH55x)
__data uint16_t now16;

// Switches: the keys (switch i types slot i), then the knob switch (if any)
#define NUM_SWITCHES (BOARD_KEYS + BOARD_KNOBS)
#define KNOB_SWITCH BOARD_KEYS // index of the knob switch

#define DEBOUNCE_MS 20

static __xdata uint8_t keyStable[NUM_SWITCHES];
static __xdata uint8_t keyLast[NUM_SWITCHES];
static __xdata uint16_t keyChangedAt[NUM_SWITCHES];

#if BOARD_KNOBS
// Quadrature decoder: index = (prev << 2) | curr, value = step direction.
// On this board a clockwise turn counts negative (checked with test_pinmap).
static __code int8_t encTable[16] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};
// Small state sits in indirect internal RAM (__idata): shorter code than
// xdata, and __data is already used by macro.c and lock.c
static __idata uint8_t encPrev;
static __idata int8_t encCount;
#define ENC_MIN_STEPS 2 // quadrature steps that make a detent (of 4)
#endif

// Hold switch: holding it for LOCK_HOLD_MS starts a pattern entry. It is the
// knob switch, or any key on a board without a knob. With a lock pattern
// stored, its press is typed on release so that a hold can start the entry
// instead.
#if BOARD_KNOBS
#define IS_HOLD_SWITCH(i) ((i) == KNOB_SWITCH)
#define SWITCH_SLOT(i) ((i) == KNOB_SWITCH ? SLOT_KNOB_PRESS : (i))
#else
#define IS_HOLD_SWITCH(i) 1
#define SWITCH_SLOT(i) (i)
#endif
#define NO_SWITCH 0xFF
static __idata uint8_t heldSwitch; // press waiting for release or the hold time
static __idata uint16_t heldAt;

void input_init(void) {
  heldSwitch = NO_SWITCH;
#if BOARD_KNOBS
  encPrev = hal_encoder();
#endif
}

// A key or knob action: a pattern step during a pattern entry, typed when
// open, ignored when locked
void input_action(uint8_t slot) {
  lock_activity();
  if (lock_entering()) {
    led_entered(slot);
    lock_input(slot);
  } else if (lock_state() == LOCK_OPEN) {
    macro_enqueue(slot);
    led_typed(slot); // green even for an empty slot
  }
}

// A press still waiting is a short press: act on it now
static void releaseHeld(void) {
  __data uint8_t sw = heldSwitch;
  if (sw != NO_SWITCH) {
    heldSwitch = NO_SWITCH;
    input_action(SWITCH_SLOT(sw));
  }
}

static void switchDown(__data uint8_t i) {
  if (!IS_HOLD_SWITCH(i) || lock_entering() || !storage_lock_len()) {
    input_action(SWITCH_SLOT(i));
    return;
  }
  releaseHeld(); // another key still down from before was pressed first
  heldSwitch = i;
  heldAt = now16;
}

static void switchUp(__data uint8_t i) {
  if (heldSwitch == i) {
    releaseHeld();
  }
}

static void scanSwitches(void) {
  __data hal_sw_t switches = hal_switches();

  for (uint8_t i = 0; i < NUM_SWITCHES; i++) {
    uint8_t pressed = switches & 1;
    switches >>= 1;

    if (pressed != keyLast[i]) {
      keyLast[i] = pressed;
      keyChangedAt[i] = now16;
    } else if (pressed != keyStable[i] &&
               (uint16_t)(now16 - keyChangedAt[i]) >= DEBOUNCE_MS) {
      keyStable[i] = pressed;
      if (pressed) {
        switchDown(i);
      } else {
        switchUp(i);
      }
    }
  }
  if (heldSwitch != NO_SWITCH && (uint16_t)(now16 - heldAt) >= LOCK_HOLD_MS) {
    heldSwitch = NO_SWITCH;
    lock_start_entry();
  }
}

#if BOARD_KNOBS
static void scanEncoder(void) {
  uint8_t curr = hal_encoder();

  if (curr == encPrev) {
    return;
  }
  encCount += encTable[(encPrev << 2) | curr];
  encPrev = curr;

  // One action per detent, when both lines are back at rest (high). A
  // detent is 4 steps the same way (11 10 00 01 11); 2 or more count, so a
  // missed sample still works. A knob left just off a detent (e.g. at 10)
  // chatters 10 <-> 11 when the board shakes, e.g. on pressing the key next
  // to it: that is +1/-1 and must not count (it typed CCW on MK6K1NL).
  if (curr == 3) {
    if (encCount >= ENC_MIN_STEPS || encCount <= -ENC_MIN_STEPS) {
      input_action(encCount < 0 ? SLOT_KNOB_CW : SLOT_KNOB_CCW);
    }
    encCount = 0;
  }
}
#endif

void input_task(void) {
#if BOARD_KNOBS
  scanEncoder();
#endif
  scanSwitches();
}
