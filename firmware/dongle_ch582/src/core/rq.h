// Report queue: fixed slots of up to RQ_DATA bytes, FIFO, no chip code.
// One producer and one consumer in the same loop (not for interrupts).
#ifndef RQ_H
#define RQ_H
#include <stdint.h>

#define RQ_SLOTS 32 // power of two
#define RQ_DATA 8

typedef struct {
    uint8_t data[RQ_SLOTS][RQ_DATA];
    uint8_t len[RQ_SLOTS];
    uint8_t head, tail; // head: next write, tail: next read
    uint16_t dropped;   // pushes refused because full
} rq_t;

void rq_init(rq_t *q);
uint8_t rq_count(const rq_t *q);
uint8_t rq_push(rq_t *q, const uint8_t *d, uint8_t len); // 0 = full (counted in dropped)
const uint8_t *rq_peek(const rq_t *q, uint8_t *len);     // NULL = empty
void rq_pop(rq_t *q);

#endif
