#include "scheduler.h"
#include "stm32g474xx.h"
#include "system_clock.h"
#include "systick.h"

#define CYCLES_PER_US (SYSTEM_CLOCK_HZ / 1000000UL)

typedef struct {
    sch_task_fn_t fn;
    uint32_t period_ms;
    uint32_t next_run_ms;
    uint32_t run_count;
    uint32_t last_duration_cycles;
    uint8_t active;
} sch_task_t;

static sch_task_t tasks[SCH_MAX_TASKS];
static uint8_t task_count = 0;

void SCH_Init(void) {
    task_count = 0;
    for (uint8_t i = 0; i < SCH_MAX_TASKS; i++) {
        tasks[i].active = 0;
    }

    /* Enable the DWT cycle counter so tasks can be timed. */
    DEMCR |= DEMCR_TRCENA;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA;
}

int SCH_Add_Task(sch_task_fn_t fn, uint32_t delay_ms, uint32_t period_ms) {
    if (task_count >= SCH_MAX_TASKS || fn == 0 || period_ms == 0) return -1;

    sch_task_t *t = &tasks[task_count];
    t->fn = fn;
    t->period_ms = period_ms;
    t->next_run_ms = SysTick_GetMillis() + delay_ms;
    t->run_count = 0;
    t->last_duration_cycles = 0;
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

            uint32_t start = DWT->CYCCNT;
            t->fn();
            t->last_duration_cycles = DWT->CYCCNT - start;
            t->run_count++;
        }
    }
}

uint32_t SCH_Get_Period(int task_id) {
    if (task_id < 0 || task_id >= task_count) return 0;
    return tasks[task_id].period_ms;
}

uint32_t SCH_Get_Run_Count(int task_id) {
    if (task_id < 0 || task_id >= task_count) return 0;
    return tasks[task_id].run_count;
}

uint32_t SCH_Get_Last_Duration_us(int task_id) {
    if (task_id < 0 || task_id >= task_count) return 0;
    return tasks[task_id].last_duration_cycles / CYCLES_PER_US;
}
