#include "link.h"
#include <string.h>

static void enter_pairing(link_t *l) {
    l->mode = LK_PAIRING;
    l->pair_until = l->has_bond ? (l->now + LK_PAIR_MS) | 1 : 0;
    l->fb_until = l->now + LK_FEEDBACK_MS;
}

static void erase(link_t *l) {
    l->actions |= LKA_ERASE;
    l->has_bond = 0;
    enter_pairing(l);
}

void link_init(link_t *l, uint8_t has_bond, uint8_t replugs, uint32_t now) {
    memset(l, 0, sizeof(*l));
    l->now = now;
    l->has_bond = has_bond;
    if (replugs >= LK_REPLUG_ERASE) erase(l);
    else if (!has_bond || replugs >= LK_REPLUG_PAIR) enter_pairing(l);
    else l->mode = LK_RECONNECT;
}

static uint8_t after(uint32_t now, uint32_t t) { return (int32_t)(now - t) >= 0; }

void link_tick(link_t *l, uint32_t now, uint8_t btn) {
    l->now = now;
    if (btn && !l->btn) {
        l->btn_since = now;
        l->btn_stage = 0;
    }
    if (btn) {
        uint32_t held = now - l->btn_since;
        if (l->btn_stage == 0 && held >= LK_BTN_PAIR_MS) {
            l->btn_stage = 1;
            enter_pairing(l);
        } else if (l->btn_stage == 1 && held >= LK_BTN_ERASE_MS) {
            l->btn_stage = 2;
            erase(l);
        }
    } else if (l->btn && l->btn_stage == 0 && l->mode == LK_PAIRING && l->has_bond) {
        l->mode = LK_RECONNECT; // short press cancels pairing
        l->pair_until = 0;
    }
    l->btn = btn;
    if (l->mode == LK_PAIRING && l->pair_until && after(now, l->pair_until)) {
        l->mode = LK_RECONNECT;
        l->pair_until = 0;
    }
}

void link_event(link_t *l, uint8_t ev) {
    switch (ev) {
    case LKE_LINK_UP: l->up = 1; break;
    case LKE_READY:
        if (l->mode == LK_PAIRING) {
            l->actions |= LKA_SAVE;
            l->has_bond = 1;
            l->pair_until = 0;
        }
        l->mode = LK_READY;
        break;
    case LKE_LINK_DOWN:
        l->up = 0;
        if (l->mode == LK_READY) l->mode = LK_RECONNECT;
        break;
    default: break;
    }
}

uint8_t link_target(const link_t *l) { return l->mode == LK_PAIRING ? LK_TARGET_ANY : LK_TARGET_BOND; }

uint8_t link_take(link_t *l, uint8_t a) {
    uint8_t had = (l->actions & a) != 0;
    l->actions &= ~a;
    return had;
}

uint8_t link_led(const link_t *l) {
    uint32_t t = l->now;
    if (!after(t, l->fb_until) && l->fb_until) return (t / 50) & 1;
    switch (l->mode) {
    case LK_PAIRING: return (t / 100) & 1;      // fast blink
    case LK_RECONNECT: return (t % 1000) < 100; // short flash each second
    default: return 1;                          // ready: on
    }
}
