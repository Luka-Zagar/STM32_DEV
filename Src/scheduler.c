/**
 ******************************************************************************
 * @file        scheduler.c
 * @brief       Simple Cooperative Task Scheduler (RTOS) Implementation.
 *              Uses SysTick as a time base to flag tasks for execution.
 ******************************************************************************
 */

#include "scheduler.h"
#include "stm32g474xx.h"

/* The internal list of tasks - fixed size defined in header */
static Task_t Task_List[MAX_TASKS] = {0};

/**
 * @brief SysTick Interrupt Handler - The OS Heartbeat.
 * This runs in Interrupt Context every 1ms.
 * It decrements delays and sets the 'run_me' flag when a task is due.
 */
void SysTick_Handler(void) {
    for (int i = 0; i < MAX_TASKS; i++) {
        if (Task_List[i].pTask) {
            if (Task_List[i].delay == 0) {
                /* Task is due - set flag and reload period */
                Task_List[i].run_me = 1;
                Task_List[i].delay = Task_List[i].period - 1;
            } else {
                /* Task is still waiting */
                Task_List[i].delay--;
            }
        }
    }
}

/**
 * @brief Initializes the scheduler and sets up SysTick for 1ms intervals.
 */
void SCH_Init(uint32_t sys_clock_hz) {
    /* Configure SysTick: Reload value = (Clock / 1000) - 1 for 1ms */
    SYSTICK->LOAD = (sys_clock_hz / 1000) - 1;
    SYSTICK->VAL = 0;
    SYSTICK->CTRL = SYSTICK_CTRL_CLKSOURCE | SYSTICK_CTRL_TICKINT | SYSTICK_CTRL_ENABLE;
    
    /* Ensure task list is empty */
    for (int i = 0; i < MAX_TASKS; i++) {
        Task_List[i].pTask = 0;
    }
}

/**
 * @brief Registers a new task into the first available slot in Task_List.
 */
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

/**
 * @brief The Dispatcher. Runs in the main loop (thread context).
 * It executes tasks that have their 'run_me' flag set by the interrupt.
 */
void SCH_Dispatch_Tasks(void) {
    for (int i = 0; i < MAX_TASKS; i++) {
        if (Task_List[i].run_me > 0) {
            Task_List[i].pTask();       /* Execute the user function */
            Task_List[i].run_me = 0;    /* Reset flag until next period */
        }
    }
}

/**
 * @brief Starts the system by enabling global interrupts.
 */
void SCH_Start(void) {
    /* Enable Global Interrupts (Change Processor State - Enable Interrupts) */
    __asm volatile ("cpsie i" : : : "memory");
}

