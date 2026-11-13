// HID over GATT client bookkeeping (no chip code):
//  - advertising data: is it a HID device, is it in pairing (discoverable) mode
//  - HID service discovery results: which handle is which report
#ifndef HOGP_H
#define HOGP_H
#include <stdint.h>

#define UUID_HID_SERVICE 0x1812
#define UUID_REPORT_MAP 0x2A4B
#define UUID_REPORT 0x2A4D
#define UUID_BOOT_KB_IN 0x2A22
#define UUID_BOOT_KB_OUT 0x2A32
#define UUID_PROTOCOL_MODE 0x2A4E
#define UUID_CCCD 0x2902
#define UUID_REPORT_REF 0x2908

#define ADV_HID 0x01          // HID service UUID or HID appearance
#define ADV_DISCOVERABLE 0x02 // flags: LE limited or general discoverable
// Bits from one advertising or scan response payload
uint8_t adv_parse(const uint8_t *data, uint8_t len);

#define HOGP_MAX_CHARS 24
#define HOGP_MAX_REPORTS 8
#define HOGP_REP_INPUT 1 // Report Reference type
#define HOGP_REP_OUTPUT 2
#define HOGP_REP_FEATURE 3
#define HOGP_ID_BOOT 0xFF // pseudo report: boot keyboard characteristics

typedef struct {
    uint16_t val, cccd, ref; // handles (0 = none)
    uint8_t id, type;        // from the Report Reference
} hogp_report_t;

typedef struct {
    uint16_t start, end;     // HID service
    uint16_t map, proto;     // Report Map, Protocol Mode value handles
    uint16_t boot_in, boot_in_cccd, boot_out;
    hogp_report_t rep[HOGP_MAX_REPORTS];
    uint8_t nrep;
    // discovery only
    uint16_t ch_val[HOGP_MAX_CHARS], ch_uuid[HOGP_MAX_CHARS];
    uint8_t nch;
} hogp_t;

void hogp_reset(hogp_t *h, uint16_t start, uint16_t end);
void hogp_add_char(hogp_t *h, uint16_t val, uint16_t uuid);
void hogp_add_desc(hogp_t *h, uint16_t handle, uint16_t uuid);
// Report Reference value read for report index i
void hogp_set_ref(hogp_t *h, uint8_t i, uint8_t id, uint8_t type);
// Report index of a notified value handle, -1 = none
int8_t hogp_find(const hogp_t *h, uint16_t val);
// Output report index for a report ID, -1 = none
int8_t hogp_find_output(const hogp_t *h, uint8_t id);

#endif
