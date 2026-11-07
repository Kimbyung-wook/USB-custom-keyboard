#ifndef __MK_HAL_CH32V203_H__
#define __MK_HAL_CH32V203_H__

// CH32V203 implementation of src/hal/hal.h on the WCH EVT SDK. Include hal.h,
// not this file. USB driver: src/hal/ch32v203/usb (patched SDK copies).

#include <stdint.h>

// SDCC memory spaces mean nothing on RISC-V
#define __xdata
#define __data
#define __idata
#define __code const
#define __at(x)

uint16_t hal_ms(void);

typedef uint16_t hal_sw_t; // 9 keys do not fit in 8 bits
hal_sw_t hal_switches(void);

extern const uint8_t hal_palette[][3];
// Every key LED in color, the keys in mask (bit i = key i) in maskColor
void hal_leds(uint8_t color, hal_sw_t mask, uint8_t maskColor);
void hal_leds_rgb(uint8_t r, uint8_t g, uint8_t b); // every LED, palette units

// Config: RAM copy of the last BLOB_CAPACITY bytes of flash; hal_store_flush() writes it
uint8_t hal_store_read(blob_pos_t addr);
void hal_store_write(blob_pos_t addr, uint8_t value);
uint8_t hal_store_flush(void);

uint8_t kb_ready(void);
uint8_t kb_busy(void);
uint8_t kb_send(uint8_t modifiers, uint8_t keycode);
void kb_cancel(void);
extern volatile uint8_t KB_LED_Cur_Status; // usb/ch32v20x_usbfs_device.c
#define kb_caps_lock() (KB_LED_Cur_Status & 0x02)

// Vendor HID: the request lands in the EP2 OUT buffer, the response goes
// out of a separate buffer
extern uint8_t USBFS_EP2_Buf[];
extern uint8_t hal_res[64];
extern volatile uint8_t USBFS_Vendor_Req;
extern volatile uint8_t USBFS_Endp_Busy[];
#define HAL_REQ USBFS_EP2_Buf
#define HAL_RES hal_res
#define vendor_has_request() (USBFS_Vendor_Req)
#define vendor_in_busy() (USBFS_Endp_Busy[2])
void vendor_send(void);

void hal_init(void);
#define hal_boot_held() 0 // no knob; the bootloader needs the BOOT bridge
void hal_usb_start(void);
// No software entry to the ROM bootloader (docs/PORT_CH32V203.md): restarts
void hal_enter_bootloader(void);
uint8_t hal_chip_uid(uint8_t i);

#endif
