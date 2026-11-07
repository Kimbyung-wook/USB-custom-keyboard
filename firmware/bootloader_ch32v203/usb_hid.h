#ifndef __BL_USB_HID_H__
#define __BL_USB_HID_H__
#include <stdint.h>

void usb_init(void);
void usb_stop(void);          // detach (pull-up off)
void usb_poll(void);          // call often from the main loop
int usb_recv(uint8_t *buf);   // 1 = a 64-byte OUT report was copied
void usb_send(const uint8_t *buf); // queue a 64-byte IN report (waits if busy)
int usb_tx_busy(void);

#endif
