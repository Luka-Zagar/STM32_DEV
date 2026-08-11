#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdint.h>

#define SCH_MAX_TASKS 16 /* 11 currently registered (main.c) - headroom for whatever's still ahead (WiFi/MQTT work) */

typedef void (*sch_task_fn_t)(void);

void SCH_Init(void);

/* Registers a periodic task, first run at (now + delay_ms), then every
 * period_ms after that. Returns a task id (>= 0) for use with
 * SCH_Set_Period, or -1 if the task table is full. */
int SCH_Add_Task(sch_task_fn_t fn, uint32_t delay_ms, uint32_t period_ms);

/* Changes an already-registered task's period; takes effect after its
 * next scheduled run. */
void SCH_Set_Period(int task_id, uint32_t period_ms);

/* Stops a task from being dispatched at all (run count stops advancing)
 * until SCH_Resume_Task is called. */
void SCH_Pause_Task(int task_id);

/* Resumes a paused task, scheduling its next run one period from now
 * (not immediately, and not a burst of catch-up runs for time missed
 * while paused). */
void SCH_Resume_Task(int task_id);

/* Call repeatedly from the main loop. Runs any task that has come due.
 * Never blocks; never call from an ISR. */
void SCH_Dispatch(void);

/* Introspection, e.g. for a status/telemetry task. Return 0 for an
 * invalid task_id. */
uint32_t SCH_Get_Period(int task_id);
uint32_t SCH_Get_Run_Count(int task_id);
uint32_t SCH_Get_Last_Duration_us(int task_id);

#endif /* SCHEDULER_H */
