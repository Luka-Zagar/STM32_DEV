#ifndef RINGBUF_H
#define RINGBUF_H

#include <stdint.h>
#include "record.h"

/* Single-producer/single-consumer ring buffer of record_t - the acquire
 * task pushes, the logger task pops, nothing else touches it. Same
 * lock-free head/tail pattern as drivers/uart.c's RX buffer: safe
 * without a mutex only because the cooperative scheduler never preempts
 * a task mid-run, and because head is written only by the producer and
 * tail only by the consumer. Never let the logger's SD write latency
 * block a producer - that's the entire reason this exists (see project
 * brief's SD write-latency gotcha). */

#define RINGBUF_CAPACITY 16

typedef struct {
    record_t buf[RINGBUF_CAPACITY];
    volatile uint16_t head;
    volatile uint16_t tail;
    volatile uint32_t overflow_count; /* pushes dropped because the buffer was full */
} ringbuf_t;

void ringbuf_init(ringbuf_t *rb);

/* Drops (does not overwrite) and counts into overflow_count if full,
 * rather than clobbering the oldest queued record - a producer running
 * ahead of a stalled logger shouldn't silently corrupt ordering. Returns
 * 1 on success, 0 if dropped. */
int ringbuf_push(ringbuf_t *rb, const record_t *rec);

/* Returns 1 and fills *rec, or 0 if the buffer was empty. */
int ringbuf_pop(ringbuf_t *rb, record_t *rec);

uint32_t ringbuf_count(const ringbuf_t *rb);

#endif /* RINGBUF_H */
