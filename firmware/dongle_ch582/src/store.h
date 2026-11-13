// Data flash records (offsets from 0x70000; the BLE stack's bond SNV is at 0x7E00):
//   0x7000 (1 KB)  bonded device: address + HID handles + raw report map
//   0x7800 (256 B) quick replug counter
#ifndef STORE_H
#define STORE_H
#include <stdint.h>
#include "core/hogp.h"

#define STORE_MAP_MAX 512

typedef struct {
    uint32_t magic;
    uint8_t addr_type, addr[6];
    uint8_t boot;            // 1 = map unusable, boot protocol used
    hogp_t hogp;
    uint16_t map_len;
    uint8_t map[STORE_MAP_MAX];
    uint32_t crc;
} store_dev_t;

uint8_t store_load(store_dev_t *d);        // 1 = valid record
void store_save(store_dev_t *d);           // fills magic and crc
void store_clear(void);

uint8_t store_replug_count(void);          // power-ups counted so far, then +1
void store_replug_reset(void);             // after the dongle stayed plugged in
#endif
