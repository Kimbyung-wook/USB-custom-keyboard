#include "rq.h"
#include <string.h>

void rq_init(rq_t *q) { memset(q, 0, sizeof(*q)); }

uint8_t rq_count(const rq_t *q) { return (uint8_t)((q->head - q->tail) & (RQ_SLOTS - 1)); }

uint8_t rq_push(rq_t *q, const uint8_t *d, uint8_t len) {
    if (len > RQ_DATA || rq_count(q) == RQ_SLOTS - 1) {
        q->dropped++;
        return 0;
    }
    memcpy(q->data[q->head], d, len);
    q->len[q->head] = len;
    q->head = (q->head + 1) & (RQ_SLOTS - 1);
    return 1;
}

const uint8_t *rq_peek(const rq_t *q, uint8_t *len) {
    if (q->head == q->tail) return 0;
    *len = q->len[q->tail];
    return q->data[q->tail];
}

void rq_pop(rq_t *q) {
    if (q->head != q->tail) q->tail = (q->tail + 1) & (RQ_SLOTS - 1);
}
