/**
 ******************************************************************************
 * @file        scheduler.c
 * @brief       Simple Cooperative Task Scheduler (RTOS) Implementation.
 ******************************************************************************
 */

#include "scheduler.h"
#include "stm32g474xx.h"

/* The internal list of tasks */
static Task_t Task_List[MAX_TASKS] = {0};

/**
 * @brief SysTick Interrupt Handler - The OS Heartbeat.
 * This runs in Interrupt Context (High Priority).
 */
void SysTick_Handler(void) {
    for (int i = 0; i < MAX_TASKS; i++) {
        if (Task_List[i].pTask) {
            if (Task_List[i].delay == 0) {
                Task_List[i].run_me = 1;
                Task_List[i].delay = Task_List[i].period - 1;
            } else {
                Task_List[i].delay--;
            }
        }
    }
}

void SCH_Init(uint32_t sys_clock_hz) {
    /* Configure SysTick for 1ms interrupts */
    SYSTICK->LOAD = (sys_clock_hz / 1000) - 1;
    SYSTICK->VAL = 0;
    SYSTICK->CTRL = SYSTICK_CTRL_CLKSOURCE | SYSTICK_CTRL_TICKINT | SYSTICK_CTRL_ENABLE;
    
    /* Clear the task list */
    for (int i = 0; i < MAX_TASKS; i++) {
        Task_List[i].pTask = 0;
    }
}

void SCH_Add_Task(void (*pTask)(void), uint32_t delay, uint32_t period) {
    for (int i = 0; i < MAX_TASKS; i++) {
        if (Task_List[i].pTask == 0) {
            Task_List[i].pTask = pTask;
            Task_List[i].delay = delay;
            Task_List[i].period = period;
            Task_List[i].run_me = 0;
            return;
        }
    }
}

void SCH_Dispatch_Tasks(void) {
    for (int i = 0; i < MAX_TASKS; i++) {
        if (Task_List[i].run_me > 0) {
            Task_List[i].pTask();       /* Execute the task */
            Task_List[i].run_me = 0;    /* Reset the flag */
        }
    }
}

void SCH_Start(void) {
    /* Enable Global Interrupts */
    __asm volatile ("cpsie i" : : : "memory");
}
