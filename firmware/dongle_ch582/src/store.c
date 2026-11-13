#include "CH58x_common.h"
#include "store.h"
#include <stddef.h>
#include <string.h>

#define DEV_ADDR 0x7000
#define DEV_SIZE 1024
#define REPLUG_ADDR 0x7800
#define REPLUG_SIZE 256
#define MAGIC 0x31474E44u // "DNG1"

_Static_assert(sizeof(store_dev_t) <= DEV_SIZE, "store_dev_t too big");

static uint32_t crc32(const uint8_t *p, uint32_t n) {
    uint32_t c = 0xFFFFFFFFu;
    while (n--) {
        c ^= *p++;
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & -(c & 1));
    }
    return ~c;
}

uint8_t store_load(store_dev_t *d) {
    EEPROM_READ(DEV_ADDR, d, sizeof(*d));
    return d->magic == MAGIC && d->map_len <= STORE_MAP_MAX &&
           d->crc == crc32((const uint8_t *)d, offsetof(store_dev_t, crc));
}

void store_save(store_dev_t *d) {
    d->magic = MAGIC;
    d->crc = crc32((const uint8_t *)d, offsetof(store_dev_t, crc));
    EEPROM_ERASE(DEV_ADDR, DEV_SIZE);
    EEPROM_WRITE(DEV_ADDR, d, sizeof(*d));
}

void store_clear(void) { EEPROM_ERASE(DEV_ADDR, DEV_SIZE); }

// Each power-up writes one 0x00 byte; the count is the number of 0x00 bytes
uint8_t store_replug_count(void) {
    uint8_t b[32];
    EEPROM_READ(REPLUG_ADDR, b, sizeof(b));
    uint8_t n = 0;
    while (n < sizeof(b) && b[n] != 0xFF) n++;
    if (n < sizeof(b)) {
        uint8_t z = 0;
        EEPROM_WRITE(REPLUG_ADDR + n, &z, 1);
    }
    return n;
}

void store_replug_reset(void) {
    uint8_t b;
    EEPROM_READ(REPLUG_ADDR, &b, 1);
    if (b != 0xFF) EEPROM_ERASE(REPLUG_ADDR, REPLUG_SIZE);
}
