#include "hogp.h"
#include <string.h>

uint8_t adv_parse(const uint8_t *d, uint8_t len) {
    uint8_t out = 0, i = 0;
    while (i < len) {
        uint8_t n = d[i];
        if (!n || i + 1 + n > len) break;
        uint8_t type = d[i + 1];
        const uint8_t *v = d + i + 2;
        uint8_t vlen = n - 1;
        if (type == 0x01 && vlen >= 1 && (v[0] & 0x03)) out |= ADV_DISCOVERABLE;
        if ((type == 0x02 || type == 0x03) && vlen >= 2)
            for (uint8_t k = 0; k + 1 < vlen; k += 2)
                if ((v[k] | (v[k + 1] << 8)) == UUID_HID_SERVICE) out |= ADV_HID;
        if (type == 0x19 && vlen >= 2) {
            uint16_t a = v[0] | (v[1] << 8);
            if ((a >> 6) == 0x0F) out |= ADV_HID; // appearance category 15: HID (0x03C0..0x03FF)
        }
        i += 1 + n;
    }
    return out;
}

void hogp_reset(hogp_t *h, uint16_t start, uint16_t end) {
    memset(h, 0, sizeof(*h));
    h->start = start;
    h->end = end;
}

void hogp_add_char(hogp_t *h, uint16_t val, uint16_t uuid) {
    if (h->nch < HOGP_MAX_CHARS) {
        h->ch_val[h->nch] = val;
        h->ch_uuid[h->nch] = uuid;
        h->nch++;
    }
    switch (uuid) {
    case UUID_REPORT_MAP: if (!h->map) h->map = val; break;
    case UUID_PROTOCOL_MODE: if (!h->proto) h->proto = val; break;
    case UUID_BOOT_KB_IN: if (!h->boot_in) h->boot_in = val; break;
    case UUID_BOOT_KB_OUT: if (!h->boot_out) h->boot_out = val; break;
    case UUID_REPORT:
        if (h->nrep < HOGP_MAX_REPORTS) {
            memset(&h->rep[h->nrep], 0, sizeof(h->rep[0]));
            h->rep[h->nrep].val = val;
            h->nrep++;
        }
        break;
    default: break;
    }
}

void hogp_add_desc(hogp_t *h, uint16_t handle, uint16_t uuid) {
    if (uuid != UUID_CCCD && uuid != UUID_REPORT_REF) return;
    // owner: the characteristic with the largest value handle below this one
    int8_t own = -1;
    for (uint8_t i = 0; i < h->nch; i++)
        if (h->ch_val[i] < handle && (own < 0 || h->ch_val[i] > h->ch_val[own])) own = i;
    if (own < 0) return;
    uint16_t val = h->ch_val[own];
    if (h->ch_uuid[own] == UUID_BOOT_KB_IN && uuid == UUID_CCCD) {
        h->boot_in_cccd = handle;
        return;
    }
    for (uint8_t i = 0; i < h->nrep; i++)
        if (h->rep[i].val == val) {
            if (uuid == UUID_CCCD) h->rep[i].cccd = handle;
            else h->rep[i].ref = handle;
        }
}

void hogp_set_ref(hogp_t *h, uint8_t i, uint8_t id, uint8_t type) {
    if (i < h->nrep) {
        h->rep[i].id = id;
        h->rep[i].type = type;
    }
}

int8_t hogp_find(const hogp_t *h, uint16_t val) {
    for (uint8_t i = 0; i < h->nrep; i++)
        if (h->rep[i].val == val) return i;
    return -1;
}

int8_t hogp_find_output(const hogp_t *h, uint8_t id) {
    for (uint8_t i = 0; i < h->nrep; i++)
        if (h->rep[i].type == HOGP_REP_OUTPUT && h->rep[i].id == id) return i;
    return -1;
}
