// USB HID device for CH58x: boot keyboard (interface 0) + consumer/system/mouse
// with report IDs (interface 1). Runs on both USB ports at once (USB1 PB10/PB11,
// USB2 PB12/PB13): the one the PC configures is used, so it works whichever
// port the dongle board wires.
#ifndef USB_HID_H
#define USB_HID_H
#include <stdint.h>

#define USB_KB_LEN 8   // boot keyboard report
#define USB_EXT_MAX 8  // report ID + data (usb_report.h)

void usb_hid_init(const char *product);
uint8_t usb_hid_ready(void);       // the PC configured a port
uint8_t usb_hid_port(void);        // 0 none, 1 USB1, 2 USB2
uint8_t usb_kb_busy(void);
uint8_t usb_kb_send(const uint8_t *report);             // 1 = taken
uint8_t usb_ext_busy(void);
uint8_t usb_ext_send(const uint8_t *report, uint8_t len); // report[0] = report ID
uint8_t usb_leds(void);            // keyboard LED output report (bit0 Num, 1 Caps, 2 Scroll)
uint8_t usb_leds_take(void);       // 1 once after the LED report changed
uint8_t usb_suspended(void);

#endif
