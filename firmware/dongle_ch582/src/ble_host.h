// BLE HID host (HID over GATT central) on the WCH BLE library:
// scan for a HID keyboard in pairing mode, bond, discover the HID service,
// subscribe to input reports and pass them to the converter (core/hid_conv).
// The bonded device's handles and report map are cached in data flash, so a
// reconnect only re-enables notifications.
#ifndef BLE_HOST_H
#define BLE_HOST_H
#include <stdint.h>
#include "core/hid_conv.h"

typedef void (*bh_event_t)(uint8_t ev); // LKE_* (core/link.h)

// After BLE_LibInit and GAPRole_CentralInit
void bh_init(conv_emit_t emit, bh_event_t event);
uint8_t bh_has_bond(void);
// Every tick: target from link_target() (LK_TARGET_*); starts or cancels scan/connect
void bh_poll(uint8_t target);
// USB keyboard LED state -> keyboard output report
void bh_set_leds(uint8_t leds);
// Forget all bonds and the cache (drops the link)
void bh_erase_all(void);

#endif
