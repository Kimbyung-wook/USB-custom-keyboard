#ifndef __MK_PROTOCOL_H__
#define __MK_PROTOCOL_H__

// Vendor HID protocol v2 (docs/PROTOCOL.md)

#define PROTO_VERSION 2
#define PROTO_MAX_CHUNK 56

#define CMD_HELLO 0x01
#define CMD_READ 0x02
#define CMD_WRITE 0x03
#define CMD_COMMIT 0x04
#define CMD_TEST 0x05
#define CMD_BOOT 0x06
#define CMD_BATTERY 0x07 // boards with BOARD_BATTERY only (others: ST_BAD_CMD)
#define CMD_CLEAR_BONDS 0x08 // BLE boards only (others: ST_BAD_CMD): erase all
                             // pairing (SNV bonds, slot hosts, whitelist), bump
                             // the slot generations, then reset. OK while locked

#define ST_OK 0
#define ST_BAD_CMD 1
#define ST_BAD_ARG 2
#define ST_BAD_CRC 3
#define ST_TOO_BIG 4
#define ST_FLASH_ERR 5
#define ST_BUSY 6
#define ST_LOCKED 7 // READ/TEST/BOOT refused until the lock pattern is entered

#define PROTO_EVT_NONE 0
#define PROTO_EVT_BOOT 1    // host asked to jump to the bootloader
#define PROTO_EVT_SAVED 2   // a new config was committed
#define PROTO_EVT_FACTORY 3 // CMD_CLEAR_BONDS: clear BLE pairing and reset (BLE boards)

// Handle one pending request, if any; returns a PROTO_EVT_* value
uint8_t protocol_poll(void);

#endif
