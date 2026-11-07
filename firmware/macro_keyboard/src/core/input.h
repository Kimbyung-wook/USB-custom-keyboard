#ifndef __MK_INPUT_H__
#define __MK_INPUT_H__

#include <stdint.h>

// Keys and knob: debounces the switches (hal_switches), decodes the knob
// (hal_encoder) and routes each action: a pattern step during a pattern
// entry, typed when open, ignored when locked. Holding the knob (any key on
// a board without a knob) for LOCK_HOLD_MS starts a pattern entry (lock.h).

extern __data uint16_t now16; // this loop's hal_ms(), set by loop() first
// Debounced switches down: bit i = switch i (keys, then the knob switch).
// hal_sw_t comes from hal.h, included before this file.
extern __idata hal_sw_t input_down;

void input_init(void); // after hal_init(): the knob's resting lines
void input_task(void); // from loop()
void input_action(uint8_t slot); // one key/knob action (also used by tests)

#endif
