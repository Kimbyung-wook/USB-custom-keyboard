// RISC-V (CH32V20x) only: the Arduino (CH552, SDCC) build compiles every file under src/
#ifdef __riscv
/********************************** (C) COPYRIGHT *******************************
 * File Name          : usbd_desc.c
 * Author             : WCH
 * Version            : V1.0.0
 * Date               : 2022/08/18
 * Description        : All descriptors for the keyboard and mouse composite device.
*********************************************************************************
* Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
* Attention: This software (modified or not) and binary are used for 
* microcontroller manufactured by Nanjing Qinheng Microelectronics.
*******************************************************************************/


/*******************************************************************************/
/* Header File */
#include "usbd_desc.h"

/*******************************************************************************/
/* Device Descriptor */
const uint8_t MyDevDescr[ ] =
{
    0x12,                                                   // bLength
    0x01,                                                   // bDescriptorType
    0x00, 0x02,                                             // bcdUSB
    0x00,                                                   // bDeviceClass
    0x00,                                                   // bDeviceSubClass
    0x00,                                                   // bDeviceProtocol
    DEF_USBD_UEP0_SIZE,                                     // bMaxPacketSize0
    (uint8_t)DEF_USB_VID, (uint8_t)( DEF_USB_VID >> 8 ),    // idVendor
    (uint8_t)DEF_USB_PID, (uint8_t)( DEF_USB_PID >> 8 ),    // idProduct
    0x00, DEF_IC_PRG_VER,                                   // bcdDevice
    0x01,                                                   // iManufacturer
    0x02,                                                   // iProduct
    0x03,                                                   // iSerialNumber
    0x01,                                                   // bNumConfigurations
};

/* Configuration Descriptor Set */
const uint8_t MyCfgDescr[ ] =
{
    0x09, 0x02, 0x42, 0x00, 0x02, 0x01, 0x00, 0xA0, 0x32,   // config: 66 bytes, 2 interfaces
    // interface 0: boot keyboard, EP1 IN
    0x09, 0x04, 0x00, 0x00, 0x01, 0x03, 0x01, 0x01, 0x00,
    0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22, 0x3E, 0x00,
    0x07, 0x05, 0x81, 0x03, 0x08, 0x00, 0x0A,
    // interface 1: vendor HID (config GUI), EP2 IN + OUT, 64 bytes
    0x09, 0x04, 0x01, 0x00, 0x02, 0x03, 0x00, 0x00, 0x00,
    0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22, 0x22, 0x00,
    0x07, 0x05, 0x82, 0x03, 0x40, 0x00, 0x01,
    0x07, 0x05, 0x02, 0x03, 0x40, 0x00, 0x01
};

/* Keyboard Report Descriptor */
const uint8_t KeyRepDesc[ ] =
{
    0x05, 0x01,                                             // Usage Page (Generic Desktop)
    0x09, 0x06,                                             // Usage (Keyboard)
    0xA1, 0x01,                                             // Collection (Application)
    0x05, 0x07,                                             // Usage Page (Key Codes)
    0x19, 0xE0,                                             // Usage Minimum (224)
    0x29, 0xE7,                                             // Usage Maximum (231)
    0x15, 0x00,                                             // Logical Minimum (0)
    0x25, 0x01,                                             // Logical Maximum (1)
    0x75, 0x01,                                             // Report Size (1)
    0x95, 0x08,                                             // Report Count (8)
    0x81, 0x02,                                             // Input (Data,Variable,Absolute)
    0x95, 0x01,                                             // Report Count (1)
    0x75, 0x08,                                             // Report Size (8)
    0x81, 0x01,                                             // Input (Constant)
    0x95, 0x03,                                             // Report Count (3)
    0x75, 0x01,                                             // Report Size (1)
    0x05, 0x08,                                             // Usage Page (LEDs)
    0x19, 0x01,                                             // Usage Minimum (1)
    0x29, 0x03,                                             // Usage Maximum (3)
    0x91, 0x02,                                             // Output (Data,Variable,Absolute)
    0x95, 0x05,                                             // Report Count (5)
    0x75, 0x01,                                             // Report Size (1)
    0x91, 0x01,                                             // Output (Constant,Array,Absolute)
    0x95, 0x06,                                             // Report Count (6)
    0x75, 0x08,                                             // Report Size (8)
    0x26, 0xFF, 0x00,                                       // Logical Maximum (255)
    0x05, 0x07,                                             // Usage Page (Key Codes)
    0x19, 0x00,                                             // Usage Minimum (0)
    0x29, 0x91,                                             // Usage Maximum (145)
    0x81, 0x00,                                             // Input(Data,Array,Absolute)
    0xC0                                                    // End Collection
};

/* Mouse Report Descriptor */
const uint8_t VendorRepDesc[ ] =
{
    0x06, 0x00, 0xFF, 0x09, 0x01, 0xA1, 0x01,
    0x09, 0x02, 0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x40, 0x81, 0x02,
    0x09, 0x03, 0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x40, 0x91, 0x02,
    0xC0
};

/* Qualifier Descriptor */
const uint8_t  MyQuaDesc[ ] =
{
    0x0A,                                                   // bLength
    0x06,                                                   // bDescriptorType
    0x00, 0x02,                                             // bcdUSB
    0x00,                                                   // bDeviceClass
    0x00,                                                   // bDeviceSubClass
    0x00,                                                   // bDeviceProtocol
    0x40,                                                   // bMaxPacketSize0
    0x01,                                                   // bNumConfigurations
    0x00                                                    // bReserved
};

/* Language Descriptor */
const uint8_t MyLangDescr[ ] =
{
    0x04,
    0x03,
    0x09,
    0x04
};

/* Manufacturer Descriptor */
const uint8_t MyManuInfo[ ] =
{
    0x0E, 0x03, 'M', 0, 'K', 0, '3', 0, 'X', 0, '3', 0, 'V', 0
};

/* Product Information */
const uint8_t MyProdInfo[ ]  =
{
    0x1E, 0x03, 'M', 0, 'i', 0, 'n', 0, 'i', 0, ' ', 0, 'M', 0, 'a', 0, 'c', 0, 'r', 0, 'o', 0, ' ', 0, 'K', 0, 'b', 0, 'd', 0
};

/* Serial Number Information: the board name (8 characters, e.g. "2X4V_RGB"),
   so Windows keeps the 8-key and 9-key boards apart */
#include "config.h"
_Static_assert(sizeof(BOARD_NAME) == 9, "serial string expects an 8-character board name");
#define SN_CH(i) (uint8_t)BOARD_NAME[i], 0
const uint8_t  MySerNumInfo[ ] =
{
    0x12, 0x03, SN_CH(0), SN_CH(1), SN_CH(2), SN_CH(3), SN_CH(4), SN_CH(5), SN_CH(6), SN_CH(7)
};

#endif // __riscv
