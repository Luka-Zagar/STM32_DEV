#ifndef TASK_ACQUIRE_H
#define TASK_ACQUIRE_H

#include "ringbuf.h"
#include "record.h"

/* Samples every producer that exists so far (RTC + GPS; I2C/ADC sensors
 * join here one at a time as they're built - see project brief's build
 * order) into one record_t per tick and pushes it into the shared ring
 * buffer. Never touches SD/FatFs itself - that's task_logger's job, kept
 * separate so a slow SD write can never stall a sensor read. */
void Task_Acquire_Init(void);
void Task_Acquire(void);

/* The logger task drains this. Exposed as an accessor (not a global
 * variable) to keep ownership clear - task_acquire.c is the only
 * producer. */
ringbuf_t *Task_Acquire_GetRingbuf(void);

/* The most recently built record - a live snapshot, independent of the
 * ring buffer above (which task_logger drains and this doesn't touch).
 * For consumers that want "the current reading" rather than the logged
 * stream, e.g. task_wifi.c's MQTT publish. All-zero until the first
 * Task_Acquire() tick after boot. */
const record_t *Task_Acquire_Last_Record(void);

#endif /* TASK_ACQUIRE_H */
