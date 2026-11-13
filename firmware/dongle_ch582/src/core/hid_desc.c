#include "hid_desc.h"
#include <string.h>

#define MAX_USAGES 24

const uint8_t hd_boot_kb_desc[] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x95, 0x01, 0x75, 0x08, 0x81, 0x01,
    0x95, 0x05, 0x75, 0x01, 0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x91, 0x02,
    0x95, 0x01, 0x75, 0x03, 0x91, 0x01,
    0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x26, 0xFF, 0x00, 0x05, 0x07, 0x19, 0x00, 0x2A, 0xFF, 0x00,
    0x81, 0x00, 0xC0};
const uint16_t hd_boot_kb_desc_len = sizeof(hd_boot_kb_desc);

typedef struct {
    int32_t lmin, lmax;
    uint32_t lmax_u;
    uint16_t page;
    uint8_t size, count, id;
} glob_t;

typedef struct {
    uint32_t u[MAX_USAGES]; // high 16 bits = page (0: page of the main item)
    uint8_t nu;
    uint32_t umin, umax;
    uint8_t has_min, has_max;
} local_t;

static uint8_t page_kept(uint16_t p) {
    return p == HD_PAGE_DESKTOP || p == HD_PAGE_KEYBOARD || p == HD_PAGE_LED ||
           p == HD_PAGE_BUTTON || p == HD_PAGE_CONSUMER;
}

static hd_report_t *report_of(hd_map_t *m, uint8_t id, uint8_t kind) {
    for (uint8_t i = 0; i < m->nr; i++)
        if (m->r[i].id == id && m->r[i].kind == kind) return &m->r[i];
    if (m->nr >= HD_MAX_REPORTS) return 0;
    hd_report_t *r = &m->r[m->nr++];
    r->id = id;
    r->kind = kind;
    r->bits = 0;
    return r;
}

static void emit(hd_map_t *m, const glob_t *g, uint8_t kind, uint8_t flags, uint16_t pos,
                 uint8_t count, uint16_t page, uint16_t umin, uint16_t umax) {
    if (!page_kept(page) || !count) return;
    if (m->n >= HD_MAX_FIELDS) {
        m->dropped++;
        return;
    }
    hd_field_t *f = &m->f[m->n++];
    f->lmin = g->lmin;
    f->lmax = g->lmax;
    f->page = page;
    f->umin = umin;
    f->umax = umax;
    f->bitpos = pos;
    f->id = g->id;
    f->kind = kind;
    f->flags = flags;
    f->size = g->size;
    f->count = count;
}

static uint16_t upage(const glob_t *g, uint32_t u) { return (u >> 16) ? (uint16_t)(u >> 16) : g->page; }

static void add_main(hd_map_t *m, glob_t *g, const local_t *l, uint8_t kind, uint32_t data) {
    hd_report_t *r = report_of(m, g->id, kind);
    if (!r) {
        m->dropped++;
        return;
    }
    uint16_t pos = r->bits;
    r->bits += (uint16_t)g->size * g->count;
    if ((data & 0x01) || !g->size || g->size > 32) return; // constant / padding
    glob_t gg = *g;
    if (gg.lmin >= 0 && gg.lmax < gg.lmin) gg.lmax = (int32_t)gg.lmax_u; // 25 FF style maximum
    uint8_t flags = (uint8_t)(data & (HD_VAR | HD_REL)) | (gg.lmin < 0 ? HD_SIGNED : 0);

    if (data & HD_VAR) {
        if (l->nu) {
            // one usage per element (last one repeats); merge consecutive usages
            uint8_t start = 0;
            for (uint8_t i = 1; i <= gg.count; i++) {
                uint32_t a = l->u[start < l->nu ? start : l->nu - 1];
                uint32_t prev = l->u[(i - 1) < l->nu ? (i - 1) : l->nu - 1];
                uint32_t cur = i < gg.count ? l->u[i < l->nu ? i : l->nu - 1] : 0;
                uint8_t run = i < gg.count && i < l->nu && upage(&gg, cur) == upage(&gg, prev) &&
                              (cur & 0xFFFF) == (prev & 0xFFFF) + 1;
                if (!run) {
                    uint16_t umin = a & 0xFFFF;
                    emit(m, &gg, kind, flags, pos + start * gg.size, i - start, upage(&gg, a), umin,
                         umin + (i - start) - 1);
                    start = i;
                }
            }
        } else if (l->has_min) {
            emit(m, &gg, kind, flags, pos, gg.count, upage(&gg, l->umin), l->umin & 0xFFFF,
                 l->has_max ? l->umax & 0xFFFF : (l->umin & 0xFFFF) + gg.count - 1);
        }
    } else {
        if (l->has_min)
            emit(m, &gg, kind, flags, pos, gg.count, upage(&gg, l->umin), l->umin & 0xFFFF,
                 l->has_max ? l->umax & 0xFFFF : l->umin & 0xFFFF);
        else if (l->nu)
            emit(m, &gg, kind, flags, pos, gg.count, upage(&gg, l->u[0]), l->u[0] & 0xFFFF,
                 l->u[l->nu - 1] & 0xFFFF);
    }
}

int hd_parse(hd_map_t *m, const uint8_t *d, uint16_t len) {
    glob_t g, stack[2];
    local_t l;
    uint8_t sp = 0;
    memset(m, 0, sizeof(*m));
    memset(&g, 0, sizeof(g));
    memset(&l, 0, sizeof(l));
    uint16_t i = 0;
    while (i < len) {
        uint8_t b = d[i++];
        if (b == 0xFE) { // long item
            if (i + 2 > len) return -1;
            i += 2 + d[i];
            continue;
        }
        uint8_t sz = b & 3;
        if (sz == 3) sz = 4;
        if (i + sz > len) return -1;
        uint32_t v = 0;
        for (uint8_t k = 0; k < sz; k++) v |= (uint32_t)d[i + k] << (8 * k);
        int32_t sv = sz == 1 ? (int8_t)v : sz == 2 ? (int16_t)v : (int32_t)v;
        i += sz;
        uint8_t type = (b >> 2) & 3, tag = b >> 4;
        if (type == 0) { // main
            if (tag == 0x8) add_main(m, &g, &l, HD_IN, v);
            else if (tag == 0x9) add_main(m, &g, &l, HD_OUT, v);
            memset(&l, 0, sizeof(l));
        } else if (type == 1) { // global
            switch (tag) {
            case 0x0: g.page = (uint16_t)v; break;
            case 0x1: g.lmin = sv; break;
            case 0x2: g.lmax = sv; g.lmax_u = v; break;
            case 0x7: g.size = (uint8_t)v; break;
            case 0x8: g.id = (uint8_t)v; m->uses_ids = 1; break;
            case 0x9: g.count = (uint8_t)(v > 255 ? 255 : v); break;
            case 0xA: if (sp < 2) stack[sp++] = g; break;
            case 0xB: if (sp) g = stack[--sp]; break;
            default: break;
            }
        } else if (type == 2) { // local
            uint32_t u = sz == 4 ? v : (v & 0xFFFF);
            if (tag == 0x0) {
                if (l.nu < MAX_USAGES) l.u[l.nu++] = u;
            } else if (tag == 0x1) {
                l.umin = u;
                l.has_min = 1;
            } else if (tag == 0x2) {
                l.umax = u;
                l.has_max = 1;
            }
        }
    }
    return 0;
}

uint16_t hd_report_bits(const hd_map_t *m, uint8_t id, uint8_t kind) {
    for (uint8_t i = 0; i < m->nr; i++)
        if (m->r[i].id == id && m->r[i].kind == kind) return m->r[i].bits;
    return 0;
}

int32_t hd_get(const hd_field_t *f, uint8_t i, const uint8_t *data, uint16_t len) {
    uint32_t bit = f->bitpos + (uint32_t)i * f->size;
    if (bit + f->size > (uint32_t)len * 8) return 0;
    uint32_t v = 0;
    for (uint8_t k = 0; k < f->size; k++, bit++)
        if (data[bit >> 3] & (1u << (bit & 7))) v |= 1u << k;
    if ((f->flags & HD_SIGNED) && f->size < 32 && (v & (1u << (f->size - 1)))) v |= ~0u << f->size;
    return (int32_t)v;
}

uint8_t hd_has_keyboard(const hd_map_t *m) {
    for (uint8_t i = 0; i < m->n; i++)
        if (m->f[i].kind == HD_IN && m->f[i].page == HD_PAGE_KEYBOARD) return 1;
    return 0;
}
