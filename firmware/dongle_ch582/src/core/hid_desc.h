// HID report descriptor (BLE "Report Map") parser. No chip code.
// Keeps the fields the dongle forwards: Generic Desktop, Keyboard, LED,
// Button and Consumer pages; constant (padding) fields only move the bit position.
#ifndef HID_DESC_H
#define HID_DESC_H
#include <stdint.h>

#define HD_MAX_FIELDS 48
#define HD_MAX_REPORTS 16

#define HD_IN 0
#define HD_OUT 1

#define HD_VAR 0x02    // variable (bit or value per usage), else array
#define HD_REL 0x04    // relative
#define HD_SIGNED 0x80 // logical minimum < 0: sign-extend

#define HD_PAGE_DESKTOP 0x01
#define HD_PAGE_KEYBOARD 0x07
#define HD_PAGE_LED 0x08
#define HD_PAGE_BUTTON 0x09
#define HD_PAGE_CONSUMER 0x0C

typedef struct {
    int32_t lmin, lmax;
    uint16_t page;
    uint16_t umin, umax; // variable: element i has usage umin + i (up to umax)
                         // array: value v means usage umin + (v - lmin)
    uint16_t bitpos;     // bit offset in the report (after the report ID byte)
    uint8_t id;          // report ID, 0 = descriptor uses no IDs
    uint8_t kind;        // HD_IN / HD_OUT
    uint8_t flags;
    uint8_t size;        // bits per element, 1..32
    uint8_t count;       // elements
} hd_field_t;

typedef struct {
    uint8_t id, kind;
    uint16_t bits;
} hd_report_t;

typedef struct {
    hd_field_t f[HD_MAX_FIELDS];
    hd_report_t r[HD_MAX_REPORTS];
    uint8_t n, nr;
    uint8_t uses_ids;
    uint8_t dropped; // fields that did not fit
} hd_map_t;

// 0 = ok, -1 = malformed (fields parsed so far stay)
int hd_parse(hd_map_t *m, const uint8_t *desc, uint16_t len);
uint16_t hd_report_bits(const hd_map_t *m, uint8_t id, uint8_t kind);
// Element `i` of field f in report data (data excludes the report ID byte)
int32_t hd_get(const hd_field_t *f, uint8_t i, const uint8_t *data, uint16_t len);
// 1 if the map has keyboard input fields
uint8_t hd_has_keyboard(const hd_map_t *m);

// Standard boot keyboard descriptor (HID 1.11 appendix B.1), for the
// Boot Keyboard Input/Output characteristics when the map is unusable
extern const uint8_t hd_boot_kb_desc[];
extern const uint16_t hd_boot_kb_desc_len;

#endif
