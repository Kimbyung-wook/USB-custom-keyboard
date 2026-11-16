#ifndef __MK_HAL_CH573_H__
#define __MK_HAL_CH573_H__

// CH573 implementation of src/hal/hal.h on the WCH CH573 EVT SDK. Include
// hal.h, not this file. USB: own interrupt driver in hal_ch573.c (boot
// keyboard on EP1, vendor HID on EP2, same descriptors as the CH32V203 app).
// BLE (BOARD_BLE): ble_ch573.c; kb_* go over USB while it is configured,
// else over BLE. The config channel (vendor_*) is USB only.

#include <stdint.h>

// SDCC memory spaces mean nothing on RISC-V
#define __xdata
#define __data
#define __idata
#define __code const
#define __at(x)

uint16_t hal_ms(void);
uint32_t hal_ms32(void); // the same clock, 32 bits (src/core/bt_app.c)

typedef uint16_t hal_sw_t;
hal_sw_t hal_switches(void);

// No key LEDs (status LEDs: src/core/bt_led.c)
extern const uint8_t hal_palette[][3];
#define hal_leds(color, mask, maskColor) ((void)(color), (void)(mask), (void)(maskColor))
#define hal_leds_rgb(r, g, b)
#define hal_led_key_rgb(key, r, g, b)
#define hal_leds_show()

// Config: RAM copy of BLOB_CAPACITY bytes of Data-Flash; hal_store_flush() writes it
uint8_t hal_store_read(blob_pos_t addr);
void hal_store_write(blob_pos_t addr, uint8_t value);
uint8_t hal_store_flush(void);

uint8_t kb_ready(void);
uint8_t kb_busy(void);
uint8_t kb_send(uint8_t modifiers, uint8_t keycode);
void kb_cancel(void);
uint8_t kb_caps_lock(void);
uint8_t hal_usb_ready(void); // configured by a host and not suspended

#if BOARD_BLE
// ble_ch573.c (the link policy calls are in core/bt_app.h)
void hal_ble_init(void);  // after hal_init, before bt_setup
void hal_ble_poll(void);  // every main loop pass: the BLE stack (TMOS) runs here
void hal_ble_factory_reset(void); // CMD_CLEAR_BONDS: erase all pairing, reset
uint8_t hal_ble_ready(void);
uint8_t hal_ble_busy(void);
uint8_t hal_ble_send(uint8_t modifiers, uint8_t keycode);
void hal_ble_cancel(void);
uint8_t hal_ble_caps_lock(void);
#endif

extern uint8_t hal_req[64];
extern uint8_t hal_res[64];
#define HAL_REQ hal_req
#define HAL_RES hal_res
uint8_t vendor_has_request(void);
uint8_t vendor_in_busy(void);
void vendor_send(void);

void hal_init(void);
void hal_usb_start(void);
void hal_enter_bootloader(void); // firmware/bootloader_ch573 (enter mark)
void hal_reset(void);            // plain software reset (re-init BLE for a new slot)
uint8_t hal_chip_uid(uint8_t i);
void hal_deep_sleep(void);       // LEDs off, shutdown: back with the power switch

#endif
