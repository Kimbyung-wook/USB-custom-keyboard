// Dongle connection state machine (no chip code): pairing mode, reconnect,
// bond erase, button and quick-replug gestures, status LED pattern.
//
//   no bond                 -> PAIRING (no time limit)
//   bond                    -> RECONNECT -> READY (HID set up) -> link lost -> RECONNECT
//   button 3 s / 3 replugs  -> PAIRING for 60 s (old bond kept until a new one is ready)
//   button 10 s / 5 replugs -> erase bonds -> PAIRING
//   short press in PAIRING with a bond -> back to RECONNECT
#ifndef LINK_H
#define LINK_H
#include <stdint.h>

#define LK_PAIRING 1
#define LK_RECONNECT 2
#define LK_READY 3

#define LK_TARGET_ANY 1  // scan for a new HID device in pairing mode
#define LK_TARGET_BOND 2 // connect to the bonded device

#define LKE_LINK_UP 1
#define LKE_READY 2      // encrypted, reports subscribed
#define LKE_LINK_DOWN 3

#define LKA_ERASE 0x01   // erase all bonds and the cache
#define LKA_SAVE 0x02    // the device that just became ready is the new bond

#define LK_PAIR_MS 60000u
#define LK_BTN_PAIR_MS 3000u
#define LK_BTN_ERASE_MS 10000u
#define LK_REPLUG_PAIR 3
#define LK_REPLUG_ERASE 5
#define LK_FEEDBACK_MS 600u

typedef struct {
    uint32_t now;
    uint32_t pair_until; // 0 = no limit
    uint32_t btn_since;
    uint32_t fb_until;   // LED feedback flicker
    uint8_t has_bond, mode, up;
    uint8_t btn, btn_stage;
    uint8_t actions;
} link_t;

void link_init(link_t *l, uint8_t has_bond, uint8_t replugs, uint32_t now);
void link_tick(link_t *l, uint32_t now, uint8_t button_down);
void link_event(link_t *l, uint8_t ev);
uint8_t link_target(const link_t *l);
uint8_t link_take(link_t *l, uint8_t action); // 1 if the action was pending (clears it)
uint8_t link_led(const link_t *l);            // 1 = LED on, at l->now

#endif
