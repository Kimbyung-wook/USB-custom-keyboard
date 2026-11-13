// PC tests for the dongle core (no chip code). Build: python run.py
#include <stdio.h>
#include <string.h>
#include "hid_desc.h"
#include "hid_conv.h"
#include "hogp.h"
#include "link.h"
#include "rq.h"

static int fails, checks;
#define CHECK(c)                                                         \
    do {                                                                 \
        checks++;                                                        \
        if (!(c)) {                                                      \
            fails++;                                                     \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);          \
        }                                                                \
    } while (0)

// ---- emit capture ----
static uint8_t ems[32][9];
static int nem;
static void emit(void *ctx, uint8_t iface, const uint8_t *r, uint8_t len) {
    (void)ctx;
    if (nem < 32) {
        ems[nem][0] = iface;
        memcpy(&ems[nem][1], r, len);
        nem++;
    }
}

// Keyboard (ID 1, modifiers + 6 key array, LED output), consumer (ID 2, 2 x 16-bit array)
static const uint8_t combo[] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01,       // keyboard, ID 1
    0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x08, 0x81, 0x01,                     // reserved
    0x95, 0x05, 0x75, 0x01, 0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x91, 0x02, // LEDs
    0x95, 0x01, 0x75, 0x03, 0x91, 0x01,                     // LED padding
    0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0xFF, 0x05, 0x07, 0x19, 0x00, 0x29, 0xFF, 0x81, 0x00,
    0xC0,
    0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x85, 0x02,       // consumer, ID 2
    0x15, 0x00, 0x26, 0xFF, 0x03, 0x19, 0x00, 0x2A, 0xFF, 0x03, 0x75, 0x10, 0x95, 0x02, 0x81, 0x00,
    0xC0,
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, 0x03,       // mouse, ID 3
    0x09, 0x01, 0xA1, 0x00, 0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01, 0x95, 0x03,
    0x75, 0x01, 0x81, 0x02, 0x95, 0x01, 0x75, 0x05, 0x81, 0x01, 0x05, 0x01, 0x09, 0x30, 0x09, 0x31,
    0x09, 0x38, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x03, 0x81, 0x06, 0xC0,
    0xC0,
};

// NKRO bitmap keyboard without report IDs
static const uint8_t nkro[] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01,
    0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
    0x19, 0x00, 0x29, 0x77, 0x95, 0x78, 0x81, 0x02, // usages 0..0x77 as bits
    0xC0,
};

static void test_parse(void) {
    static hd_map_t m;
    CHECK(hd_parse(&m, combo, sizeof(combo)) == 0);
    CHECK(m.uses_ids == 1);
    CHECK(hd_has_keyboard(&m));
    CHECK(hd_report_bits(&m, 1, HD_IN) == 64);
    CHECK(hd_report_bits(&m, 1, HD_OUT) == 8);
    CHECK(hd_report_bits(&m, 2, HD_IN) == 32);
    CHECK(hd_report_bits(&m, 3, HD_IN) == 32);
    CHECK(m.dropped == 0);
    // 0x25 0xFF logical max on an 8-bit array: unsigned 255
    int found = 0;
    for (int i = 0; i < m.n; i++)
        if (m.f[i].id == 1 && m.f[i].kind == HD_IN && !(m.f[i].flags & HD_VAR)) {
            CHECK(m.f[i].lmax == 255);
            CHECK(m.f[i].bitpos == 16);
            found = 1;
        }
    CHECK(found);
    // signed mouse X
    uint8_t d[4] = {0x00, 0xFE, 0x05, 0x00};
    for (int i = 0; i < m.n; i++)
        if (m.f[i].id == 3 && m.f[i].page == HD_PAGE_DESKTOP && m.f[i].umin == 0x30) {
            CHECK(m.f[i].flags & HD_SIGNED);
            CHECK(hd_get(&m.f[i], 0, d, 4) == -2);
            CHECK(hd_get(&m.f[i], 1, d, 4) == 5);
        }
    // truncated descriptor
    static hd_map_t t;
    uint8_t bad[] = {0x05, 0x01, 0x26, 0xFF};
    CHECK(hd_parse(&t, bad, sizeof(bad)) == -1);
    // boot descriptor parses
    CHECK(hd_parse(&t, hd_boot_kb_desc, hd_boot_kb_desc_len) == 0);
    CHECK(hd_has_keyboard(&t) && !t.uses_ids);
}

static void test_conv(void) {
    static hd_map_t m;
    static conv_t c;
    hd_parse(&m, combo, sizeof(combo));
    conv_init(&c, &m, emit, 0);
    nem = 0;
    uint8_t k1[8] = {0x02, 0, 0x04, 0x05, 0, 0, 0, 0}; // shift + a b
    conv_input(&c, 1, k1, 8);
    CHECK(nem == 1 && ems[0][0] == CONV_IF_KB);
    CHECK(ems[0][1] == 0x02 && ems[0][3] == 0x04 && ems[0][4] == 0x05 && ems[0][5] == 0);
    conv_input(&c, 1, k1, 8); // same: no report
    CHECK(nem == 1);
    uint8_t roll[8] = {0x02, 0, 1, 1, 1, 1, 1, 1}; // rollover: keep state
    conv_input(&c, 1, roll, 8);
    CHECK(nem == 1);
    uint8_t cons[4] = {0xE9, 0x00, 0, 0}; // volume up
    conv_input(&c, 2, cons, 4);
    CHECK(nem == 2 && ems[1][0] == CONV_IF_EXT && ems[1][1] == USB_ID_CONSUMER);
    CHECK(ems[1][2] == 0xE9 && ems[1][3] == 0x00);
    uint8_t mouse[4] = {0x01, 0x03, 0xFD, 0x01}; // left, x 3, y -3, wheel 1
    conv_input(&c, 3, mouse, 4);
    CHECK(nem == 3 && ems[2][1] == USB_ID_MOUSE && ems[2][2] == 0x01);
    CHECK(ems[2][3] == 3 && ems[2][4] == 0 && ems[2][5] == 0xFD && ems[2][6] == 0xFF && ems[2][7] == 1);
    nem = 0;
    conv_release_all(&c);
    CHECK(nem == 3); // kb, consumer, mouse buttons (system unchanged)
    CHECK(ems[0][0] == CONV_IF_KB && ems[0][1] == 0 && ems[0][3] == 0);
    // LED: caps -> bit1 of ID 1 output
    uint8_t id = 0, out[8];
    CHECK(conv_led_report(&m, 0x02, &id, out, 8) == 1);
    CHECK(id == 1 && out[0] == 0x02);

    // NKRO bitmap, more than 6 keys -> ErrorRollOver
    static hd_map_t n;
    CHECK(hd_parse(&n, nkro, sizeof(nkro)) == 0);
    conv_init(&c, &n, emit, 0);
    nem = 0;
    uint8_t b[16] = {0};
    b[1] = 0xF0; b[2] = 0x07; // usages 4..10 (7 keys)
    conv_input(&c, 0, b, 16);
    CHECK(nem == 1 && ems[0][3] == 1 && ems[0][8] == 1);
    b[2] = 0x01; // usages 4..8 (5 keys)
    conv_input(&c, 0, b, 16);
    CHECK(nem == 2 && ems[1][3] == 4 && ems[1][7] == 8 && ems[1][8] == 0);
}

static void test_hogp(void) {
    uint8_t adv[] = {2, 0x01, 0x05, 3, 0x03, 0x12, 0x18, 3, 0x19, 0xC1, 0x03};
    CHECK(adv_parse(adv, sizeof(adv)) == (ADV_HID | ADV_DISCOVERABLE));
    uint8_t adv2[] = {2, 0x01, 0x04, 3, 0x19, 0xC1, 0x03}; // not discoverable, keyboard appearance
    CHECK(adv_parse(adv2, sizeof(adv2)) == ADV_HID);
    uint8_t bad[] = {5, 0x03, 0x12};
    CHECK(adv_parse(bad, sizeof(bad)) == 0);

    static hogp_t h;
    hogp_reset(&h, 0x10, 0x30);
    hogp_add_char(&h, 0x12, UUID_PROTOCOL_MODE);
    hogp_add_char(&h, 0x14, UUID_REPORT_MAP);
    hogp_add_char(&h, 0x16, UUID_REPORT); // input ID 1
    hogp_add_char(&h, 0x1A, UUID_REPORT); // output ID 1
    hogp_add_char(&h, 0x1D, UUID_BOOT_KB_IN);
    hogp_add_desc(&h, 0x17, UUID_CCCD);
    hogp_add_desc(&h, 0x18, UUID_REPORT_REF);
    hogp_add_desc(&h, 0x1B, UUID_REPORT_REF);
    hogp_add_desc(&h, 0x1E, UUID_CCCD);
    hogp_set_ref(&h, 0, 1, HOGP_REP_INPUT);
    hogp_set_ref(&h, 1, 1, HOGP_REP_OUTPUT);
    CHECK(h.map == 0x14 && h.proto == 0x12 && h.nrep == 2);
    CHECK(h.rep[0].cccd == 0x17 && h.rep[0].ref == 0x18);
    CHECK(h.rep[1].cccd == 0 && h.rep[1].ref == 0x1B);
    CHECK(h.boot_in == 0x1D && h.boot_in_cccd == 0x1E);
    CHECK(hogp_find(&h, 0x16) == 0 && hogp_find(&h, 0x99) == -1);
    CHECK(hogp_find_output(&h, 1) == 1 && hogp_find_output(&h, 2) == -1);
}

static void test_link(void) {
    link_t l;
    link_init(&l, 0, 0, 0);
    CHECK(l.mode == LK_PAIRING && l.pair_until == 0 && link_target(&l) == LK_TARGET_ANY);
    link_tick(&l, 100000, 0);
    CHECK(l.mode == LK_PAIRING); // no bond: no time limit
    link_event(&l, LKE_LINK_UP);
    link_event(&l, LKE_READY);
    CHECK(l.mode == LK_READY && link_take(&l, LKA_SAVE) && !link_take(&l, LKA_SAVE));
    CHECK(link_led(&l) == 1);
    link_event(&l, LKE_LINK_DOWN);
    CHECK(l.mode == LK_RECONNECT && link_target(&l) == LK_TARGET_BOND);

    link_init(&l, 1, 0, 1000);
    CHECK(l.mode == LK_RECONNECT);
    link_init(&l, 1, 3, 1000);
    CHECK(l.mode == LK_PAIRING && l.pair_until);
    link_tick(&l, 1000 + LK_PAIR_MS + 10, 0);
    CHECK(l.mode == LK_RECONNECT); // pairing timed out
    link_init(&l, 1, 5, 1000);
    CHECK(l.mode == LK_PAIRING && link_take(&l, LKA_ERASE) && !l.has_bond);

    // button: 3 s -> pairing, short press -> cancel, 10 s -> erase
    link_init(&l, 1, 0, 0);
    uint32_t t = 0;
    for (; t <= 3000; t += 10) link_tick(&l, t, 1);
    CHECK(l.mode == LK_PAIRING);
    link_tick(&l, t, 0);
    CHECK(l.mode == LK_PAIRING); // releasing the long press keeps pairing
    link_tick(&l, t + 10, 1);
    link_tick(&l, t + 100, 0);
    CHECK(l.mode == LK_RECONNECT); // short press cancels
    link_init(&l, 1, 0, 0);
    for (t = 0; t <= 10000; t += 10) link_tick(&l, t, 1);
    CHECK(link_take(&l, LKA_ERASE) && l.mode == LK_PAIRING && !l.has_bond);
    link_tick(&l, t, 0);
    link_tick(&l, t + 10, 1);
    link_tick(&l, t + 50, 0);
    CHECK(l.mode == LK_PAIRING); // no bond: short press does not cancel
    // timer wrap
    link_init(&l, 1, 3, 0xFFFFF000u);
    link_tick(&l, 0x00001000u, 0);
    CHECK(l.mode == LK_PAIRING);
    link_tick(&l, 0xFFFFF000u + LK_PAIR_MS + 1, 0);
    CHECK(l.mode == LK_RECONNECT);
}

static void test_rq(void) {
    static rq_t q;
    rq_init(&q);
    uint8_t d[8] = {1, 2, 3}, len;
    CHECK(rq_peek(&q, &len) == 0);
    for (int i = 0; i < RQ_SLOTS - 1; i++) {
        d[0] = (uint8_t)i;
        CHECK(rq_push(&q, d, 3));
    }
    CHECK(!rq_push(&q, d, 3) && q.dropped == 1);
    CHECK(rq_count(&q) == RQ_SLOTS - 1);
    const uint8_t *p = rq_peek(&q, &len);
    CHECK(p && p[0] == 0 && len == 3);
    rq_pop(&q);
    p = rq_peek(&q, &len);
    CHECK(p && p[0] == 1);
}

int main(void) {
    test_parse();
    test_conv();
    test_hogp();
    test_link();
    test_rq();
    printf("%s: %d checks, %d failed\n", fails ? "FAIL" : "OK", checks, fails);
    return fails ? 1 : 0;
}
