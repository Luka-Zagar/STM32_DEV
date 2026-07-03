#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdint.h>

#define SCH_MAX_TASKS 8

typedef void (*sch_task_fn_t)(void);

void SCH_Init(void);

/* Registers a periodic task, first run at (now + delay_ms), then every
 * period_ms after that. Returns a task id (>= 0) for use with
 * SCH_Set_Period, or -1 if the task table is full. */
int SCH_Add_Task(sch_task_fn_t fn, uint32_t delay_ms, uint32_t period_ms);

/* Changes an already-registered task's period; takes effect after its
 * next scheduled run. */
void SCH_Set_Period(int task_id, uint32_t period_ms);

/* Call repeatedly from the main loop. Runs any task that has come due.
 * Never blocks; never call from an ISR. */
void SCH_Dispatch(void);

#endif /* SCHEDULER_H */
