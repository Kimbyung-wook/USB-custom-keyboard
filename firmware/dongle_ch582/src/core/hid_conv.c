#include "hid_conv.h"
#include <string.h>

void conv_init(conv_t *c, const hd_map_t *map, conv_emit_t emit, void *ctx) {
    memset(c, 0, sizeof(*c));
    c->map = map;
    c->emit = emit;
    c->ctx = ctx;
    c->cons[0] = USB_ID_CONSUMER;
    c->sys[0] = USB_ID_SYSTEM;
}

static conv_src_t *src_of(conv_t *c, uint8_t id) {
    for (uint8_t i = 0; i < c->nsrc; i++)
        if (c->src[i].id == id) return &c->src[i];
    if (c->nsrc >= CONV_MAX_IDS) return 0;
    conv_src_t *s = &c->src[c->nsrc++];
    memset(s, 0, sizeof(*s));
    s->id = id;
    return s;
}

static void send_kb(conv_t *c) {
    uint8_t r[8] = {0}, n = 0, over = 0;
    for (uint8_t i = 0; i < c->nsrc; i++) {
        r[0] |= c->src[i].keys[0xE0 >> 3]; // E0..E7 = modifier bits 0..7
    }
    for (uint16_t u = 0x04; u < 0xE0; u++) {
        uint8_t held = 0;
        for (uint8_t i = 0; i < c->nsrc; i++) held |= c->src[i].keys[u >> 3] & (1 << (u & 7));
        if (!held) continue;
        if (n < 6) r[2 + n++] = (uint8_t)u;
        else over = 1;
    }
    if (over) memset(r + 2, 0x01, 6); // ErrorRollOver
    if (memcmp(r, c->kb, 8)) {
        memcpy(c->kb, r, 8);
        c->emit(c->ctx, CONV_IF_KB, r, 8);
    }
}

static void send_cons(conv_t *c) {
    uint8_t r[USB_CONSUMER_LEN] = {USB_ID_CONSUMER}, n = 0;
    for (uint8_t i = 0; i < c->nsrc; i++)
        for (uint8_t k = 0; k < CONV_CONS; k++) {
            uint16_t u = c->src[i].cons[k];
            if (!u || n >= USB_CONSUMER_SLOTS) continue;
            r[1 + 2 * n] = u & 0xFF;
            r[2 + 2 * n] = u >> 8;
            n++;
        }
    if (memcmp(r, c->cons, sizeof(r))) {
        memcpy(c->cons, r, sizeof(r));
        c->emit(c->ctx, CONV_IF_EXT, r, sizeof(r));
    }
}

static void send_sys(conv_t *c) {
    uint8_t r[USB_SYSTEM_LEN] = {USB_ID_SYSTEM, 0};
    for (uint8_t i = 0; i < c->nsrc; i++) r[1] |= c->src[i].sys;
    if (memcmp(r, c->sys, sizeof(r))) {
        memcpy(c->sys, r, sizeof(r));
        c->emit(c->ctx, CONV_IF_EXT, r, sizeof(r));
    }
}

static int32_t clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

static void send_mouse(conv_t *c, int32_t x, int32_t y, int32_t wheel, int32_t pan) {
    uint8_t b = 0;
    for (uint8_t i = 0; i < c->nsrc; i++) b |= c->src[i].buttons;
    if (b == c->buttons && !x && !y && !wheel && !pan) return;
    c->buttons = b;
    x = clamp(x, -32767, 32767);
    y = clamp(y, -32767, 32767);
    uint8_t r[USB_MOUSE_LEN] = {USB_ID_MOUSE, b, x & 0xFF, (x >> 8) & 0xFF, y & 0xFF, (y >> 8) & 0xFF,
                                (uint8_t)(int8_t)clamp(wheel, -127, 127), (uint8_t)(int8_t)clamp(pan, -127, 127)};
    c->emit(c->ctx, CONV_IF_EXT, r, sizeof(r));
}

static void add_cons(uint16_t *list, uint8_t *n, uint16_t u) {
    if (u && *n < CONV_CONS) list[(*n)++] = u;
}

void conv_input(conv_t *c, uint8_t id, const uint8_t *data, uint16_t len) {
    const hd_map_t *m = c->map;
    conv_src_t *s = src_of(c, id);
    if (!s || !m) return;
    uint8_t keys[32] = {0}, sys = 0, buttons = 0, ncons = 0;
    uint16_t cons[CONV_CONS] = {0};
    uint8_t has_kb = 0, has_cons = 0, has_sys = 0, has_mouse = 0, rollover = 0;
    int32_t x = 0, y = 0, wheel = 0, pan = 0;

    for (uint8_t fi = 0; fi < m->n; fi++) {
        const hd_field_t *f = &m->f[fi];
        if (f->id != id || f->kind != HD_IN) continue;
        uint8_t var = f->flags & HD_VAR;
        for (uint8_t e = 0; e < f->count; e++) {
            int32_t v = hd_get(f, e, data, len);
            uint32_t u; // usage of this element, 0 = none
            if (var) {
                u = f->umin + e;
                if (u > f->umax) break;
            } else {
                if (v < f->lmin || v > f->lmax) continue;
                u = f->umin + (uint32_t)(v - f->lmin);
                if (!u) continue;
            }
            switch (f->page) {
            case HD_PAGE_KEYBOARD:
                has_kb = 1;
                if (!var && u <= 3) rollover = 1; // error codes: keep the old state
                else if ((!var || v) && u <= 0xFF) keys[u >> 3] |= 1 << (u & 7);
                break;
            case HD_PAGE_CONSUMER:
                if (u == 0x238 && (f->flags & HD_REL)) { // AC Pan
                    has_mouse = 1;
                    pan += v;
                } else {
                    has_cons = 1;
                    if (!var || v) add_cons(cons, &ncons, (uint16_t)u);
                }
                break;
            case HD_PAGE_DESKTOP:
                if (u >= 0x81 && u <= 0x83) {
                    has_sys = 1;
                    if (!var || v) sys |= 1 << (u - 0x81);
                } else if (var && (f->flags & HD_REL) && (u == 0x30 || u == 0x31 || u == 0x38)) {
                    has_mouse = 1;
                    if (u == 0x30) x += v;
                    else if (u == 0x31) y += v;
                    else wheel += v;
                }
                break;
            case HD_PAGE_BUTTON:
                has_mouse = 1;
                if ((!var || v) && u >= 1 && u <= 8) buttons |= 1 << (u - 1);
                break;
            default:
                break;
            }
        }
    }
    if (has_kb && !rollover) {
        memcpy(s->keys, keys, sizeof(keys));
        send_kb(c);
    }
    if (has_cons) {
        memcpy(s->cons, cons, sizeof(cons));
        send_cons(c);
    }
    if (has_sys) {
        s->sys = sys;
        send_sys(c);
    }
    if (has_mouse) {
        s->buttons = buttons;
        send_mouse(c, x, y, wheel, pan);
    }
}

void conv_release_all(conv_t *c) {
    for (uint8_t i = 0; i < c->nsrc; i++) {
        memset(c->src[i].keys, 0, sizeof(c->src[i].keys));
        memset(c->src[i].cons, 0, sizeof(c->src[i].cons));
        c->src[i].sys = 0;
        c->src[i].buttons = 0;
    }
    send_kb(c);
    send_cons(c);
    send_sys(c);
    send_mouse(c, 0, 0, 0, 0);
}

uint8_t conv_led_report(const hd_map_t *m, uint8_t leds, uint8_t *id, uint8_t *out, uint8_t max) {
    int8_t first = -1;
    for (uint8_t i = 0; i < m->n; i++)
        if (m->f[i].kind == HD_OUT && m->f[i].page == HD_PAGE_LED) {
            first = i;
            break;
        }
    if (first < 0) return 0;
    *id = m->f[first].id;
    uint8_t len = (uint8_t)((hd_report_bits(m, *id, HD_OUT) + 7) / 8);
    if (!len || len > max) return 0;
    memset(out, 0, len);
    for (uint8_t i = 0; i < m->n; i++) {
        const hd_field_t *f = &m->f[i];
        if (f->kind != HD_OUT || f->page != HD_PAGE_LED || f->id != *id || !(f->flags & HD_VAR)) continue;
        for (uint8_t e = 0; e < f->count; e++) {
            uint32_t u = f->umin + e;
            if (u < 1 || u > 5 || !(leds & (1 << (u - 1)))) continue;
            uint32_t bit = f->bitpos + (uint32_t)e * f->size; // value 1 = lowest bit of the element
            if (bit < (uint32_t)len * 8) out[bit >> 3] |= 1 << (bit & 7);
        }
    }
    return len;
}
