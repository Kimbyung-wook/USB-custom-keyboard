// USB report formats of the dongle (descriptors in usb_hid.c). No chip code.
#ifndef USB_REPORT_H
#define USB_REPORT_H

// Interface 0: boot keyboard, 8 bytes: modifiers, 0, keys[6]. No report ID
// Interface 1: report IDs
#define USB_ID_CONSUMER 1 // id, usage0 (16 bit LE), usage1  -> 5 bytes
#define USB_ID_MOUSE 2    // id, buttons, x (16 bit), y (16 bit), wheel, pan -> 8 bytes
#define USB_ID_SYSTEM 3   // id, bit0 power, bit1 sleep, bit2 wake -> 2 bytes
#define USB_CONSUMER_LEN 5
#define USB_MOUSE_LEN 8
#define USB_SYSTEM_LEN 2
#define USB_CONSUMER_SLOTS 2

#endif
