#ifndef __MK_HAL_AC6321A_H__
#define __MK_HAL_AC6321A_H__

// AC6321A implementation of src/hal/hal.h on the JieLi fw-AC63_BT_SDK
// (cpu/bd19), UNVERIFIED on a chip. Include hal.h, not this file.
// The SDK owns main(), USB and BLE; main_ac6321a.c registers the app and runs
// the keyboard sequence (core/app.c + core/bt_app.c) in one RTOS task.
//   hal_ac6321a.c   time, keys (matrix), knobs, config store (VM), chip ID,
//                   bootloader (ROM UBOOT)
//   usb_ac6321a.c   USB device: boot keyboard (interface 0) + vendor HID
//                   (interface 1, 64 B), 1209:C55D, the CH32V203 descriptors
//   ble_ac6321a.c   BLE HID keyboard (BOARD_BLE), the SDK's HOGP profile
//   led_ac6321a.c   status LEDs (core/bt_led.c bits -> the 3 RGB LEDs)
// kb_* go over USB while it is configured, else over BLE. The config channel
// (vendor_*) is USB only.

#include <stdint.h>

// SDCC memory spaces mean nothing here
#define __xdata
#define __data
#define __idata
#define __code const
#define __at(x)

uint16_t hal_ms(void);
uint32_t hal_ms32(void); // the same clock, 32 bits (src/core/bt_app.c)

// Keys 0-11, then the knob switches 12 (knob 0) and 13 (knob 1)
typedef uint16_t hal_sw_t;
hal_sw_t hal_switches(void);
uint8_t hal_encoder(void); // knob n: bit 2n+1 = A, bit 2n = B, 1 = high

// No key LEDs (status LEDs: src/core/bt_led.c)
extern const uint8_t hal_palette[][3];
#define hal_leds(color, mask, maskColor) ((void)(color), (void)(mask), (void)(maskColor))
#define hal_leds_rgb(r, g, b)
#define hal_led_key_rgb(key, r, g, b)
#define hal_leds_show()

// Config: RAM copy of BLOB_CAPACITY bytes, kept in the SDK's VM area
// (syscfg items); hal_store_flush() writes the changed parts
uint8_t hal_store_read(blob_pos_t addr);
void hal_store_write(blob_pos_t addr, uint8_t value);
uint8_t hal_store_flush(void);

uint8_t kb_ready(void);
uint8_t kb_busy(void);
uint8_t kb_send(uint8_t modifiers, uint8_t keycode);
void kb_cancel(void);
uint8_t kb_caps_lock(void);
uint8_t hal_usb_ready(void); // configured by a host

// usb_ac6321a.c (used by kb_* in hal_ac6321a.c)
uint8_t hal_usb_kb_busy(void);
uint8_t hal_usb_kb_send(uint8_t modifiers, uint8_t keycode);
void hal_usb_kb_cancel(void);
uint8_t hal_usb_caps_lock(void);
// Set by usb_ac6321a.c on the factory firmware's 03 EF EF: main loop -> UBOOT
extern volatile uint8_t hal_uboot_request;

#if BOARD_BLE
// ble_ac6321a.c (the link policy calls are in core/bt_app.h)
void hal_ble_init(void); // after hal_init, before bt_setup (starts the BLE stack)
uint8_t hal_ble_ready(void);
uint8_t hal_ble_busy(void);
uint8_t hal_ble_send(uint8_t modifiers, uint8_t keycode);
void hal_ble_cancel(void);
uint8_t hal_ble_caps_lock(void);
void hal_ble_poll(void);              // main loop: sends a waiting report
void hal_ble_on_init_ok(void);        // SDK event BT_STATUS_INIT_OK (main_ac6321a.c)
void hal_ble_on_status(uint8_t st);   // SDK event SYS_BT_EVENT_BLE_STATUS (ble_state_e)
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
void hal_enter_bootloader(void); // ROM UBOOT (USB disk 4C4A:3442): go_mask_usb_updata()
uint8_t hal_chip_uid(uint8_t i);
void hal_deep_sleep(void);       // battery critical: soft power off
void hal_idle(void);             // main loop: watchdog, and the RTOS gets its turn

#endif
