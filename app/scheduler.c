#include "scheduler.h"
#include "systick.h"

typedef struct {
    sch_task_fn_t fn;
    uint32_t period_ms;
    uint32_t next_run_ms;
    uint8_t active;
} sch_task_t;

static sch_task_t tasks[SCH_MAX_TASKS];
static uint8_t task_count = 0;

void SCH_Init(void) {
    task_count = 0;
    for (uint8_t i = 0; i < SCH_MAX_TASKS; i++) {
        tasks[i].active = 0;
    }
}

int SCH_Add_Task(sch_task_fn_t fn, uint32_t delay_ms, uint32_t period_ms) {
    if (task_count >= SCH_MAX_TASKS || fn == 0 || period_ms == 0) return -1;

    sch_task_t *t = &tasks[task_count];
    t->fn = fn;
    t->period_ms = period_ms;
    t->next_run_ms = SysTick_GetMillis() + delay_ms;
    t->active = 1;

    return task_count++;
}

void SCH_Set_Period(int task_id, uint32_t period_ms) {
    if (task_id < 0 || task_id >= task_count || period_ms == 0) return;
    tasks[task_id].period_ms = period_ms;
}

void SCH_Dispatch(void) {
    uint32_t now = SysTick_GetMillis();

    for (uint8_t i = 0; i < task_count; i++) {
        sch_task_t *t = &tasks[i];
        /* Signed subtraction so this still works across ms_ticks wraparound. */
        if (t->active && (int32_t)(now - t->next_run_ms) >= 0) {
            t->next_run_ms += t->period_ms;
            t->fn();
        }
    }
}
