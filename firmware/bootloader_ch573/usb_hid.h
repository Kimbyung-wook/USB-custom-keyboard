#ifndef __BL_USB_HID_H__
#define __BL_USB_HID_H__
#include <stdint.h>

void usb_init(void);          // attach; USB interrupt on
void usb_stop(void);          // USB interrupt off, detach (pull-up off)
int usb_recv(uint8_t *buf);   // 1 = a 64-byte OUT report was copied
void usb_send(const uint8_t *buf); // queue a 64-byte IN report (waits if busy)
int usb_tx_busy(void);

// Diagnostic build only (BL_USB_DIAG=1): stages reached, set by the handler
#define USB_DIAG_STALL 0x01   // a request was stalled
#define USB_DIAG_SETUP 0x02   // a SETUP packet came
#define USB_DIAG_ADDRESS 0x04 // SET_ADDRESS applied
#define USB_DIAG_CONFIG 0x08  // SET_CONFIGURATION came
#define USB_DIAG_REPORT 0x10  // report descriptor asked for
extern volatile uint8_t usb_diag;

// Event log: 16-byte header ("USBLOG", variant, count lo/hi, record size), then 24-byte
// records: kind, result, time in us (4 bytes, little-endian), INT_FG at handler
// entry, INT_ST, MIS_ST, UEP0_CTRL, RX_LEN, DEV_AD, UEP0_T_LEN, handler entries
// (mod 256), INT_FG now, 0, then the SETUP packet as received (kind SETUP;
// result = bytes answered, 0xFF = STALL). Registers read after the handling.
#define USB_LOG_SIZE 2048
#define USB_LOG_REC 24
#define USB_LOG_MAX ((USB_LOG_SIZE - 16) / USB_LOG_REC)
#define USB_LOG_SETUP 1
#define USB_LOG_IN0 2   // EP0 IN done (data or status stage)
#define USB_LOG_OUT0 3  // EP0 OUT done
#define USB_LOG_RESET 4 // bus reset
#define USB_LOG_OTHER 5 // other flag (result = INT_FG: 0x04 = suspend)
extern uint8_t usb_log[USB_LOG_SIZE];
extern volatile uint16_t usb_log_n;
extern volatile uint32_t usb_log_ms;
extern uint8_t usb_variant;

#endif
