#include "ringbuf.h"

void ringbuf_init(ringbuf_t *rb) {
    rb->head = 0;
    rb->tail = 0;
    rb->overflow_count = 0;
}

int ringbuf_push(ringbuf_t *rb, const record_t *rec) {
    uint16_t next = (uint16_t)((rb->head + 1) % RINGBUF_CAPACITY);
    if (next == rb->tail) {
        rb->overflow_count++;
        return 0;
    }
    rb->buf[rb->head] = *rec;
    rb->head = next;
    return 1;
}

int ringbuf_pop(ringbuf_t *rb, record_t *rec) {
    if (rb->tail == rb->head) return 0;
    *rec = rb->buf[rb->tail];
    rb->tail = (uint16_t)((rb->tail + 1) % RINGBUF_CAPACITY);
    return 1;
}

uint32_t ringbuf_count(const ringbuf_t *rb) {
    return (uint32_t)((rb->head + RINGBUF_CAPACITY - rb->tail) % RINGBUF_CAPACITY);
}
