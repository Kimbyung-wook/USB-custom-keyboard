// BLE HID input reports -> the dongle's fixed USB reports (core/usb_report.h),
// and the USB keyboard LED byte -> the keyboard's BLE output report. No chip code.
// Keys held in one BLE report stay held until that same report ID changes them,
// so keyboards that split keys over several report IDs work too.
#ifndef HID_CONV_H
#define HID_CONV_H
#include <stdint.h>
#include "hid_desc.h"
#include "usb_report.h"

#define CONV_MAX_IDS 8
#define CONV_CONS 4 // consumer usages kept per report ID

#define CONV_IF_KB 0  // emit: boot keyboard report (8 bytes)
#define CONV_IF_EXT 1 // emit: report-ID report (usb_report.h)

typedef void (*conv_emit_t)(void *ctx, uint8_t iface, const uint8_t *report, uint8_t len);

typedef struct {
    uint8_t id;
    uint8_t keys[32];            // keyboard usages 0x00..0xFF held (bitmap)
    uint16_t cons[CONV_CONS];    // consumer usages held
    uint8_t sys;                 // bit0 power, bit1 sleep, bit2 wake
    uint8_t buttons;
} conv_src_t;

typedef struct {
    const hd_map_t *map;
    conv_src_t src[CONV_MAX_IDS];
    uint8_t nsrc;
    uint8_t kb[8];                     // last sent
    uint8_t cons[USB_CONSUMER_LEN];
    uint8_t sys[USB_SYSTEM_LEN];
    uint8_t buttons;
    conv_emit_t emit;
    void *ctx;
} conv_t;

void conv_init(conv_t *c, const hd_map_t *map, conv_emit_t emit, void *ctx);
// One BLE input report: id from the Report Reference (0 if the map has no IDs),
// data without the ID byte
void conv_input(conv_t *c, uint8_t id, const uint8_t *data, uint16_t len);
// Release everything (link lost): emits release reports for what was held
void conv_release_all(conv_t *c);
// USB LED byte (bit0 Num, 1 Caps, 2 Scroll, 3 Compose, 4 Kana) -> BLE output
// report data in out (max `max` bytes). Returns length, 0 if the map has no LEDs.
uint8_t conv_led_report(const hd_map_t *map, uint8_t leds, uint8_t *id, uint8_t *out, uint8_t max);

#endif
