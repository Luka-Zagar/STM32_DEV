/**
 ******************************************************************************
 * @file        scheduler.h
 * @brief       Simple Cooperative Task Scheduler (RTOS) Header.
 ******************************************************************************
 */

#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdint.h>

/* ── Task Definitions ────────────────────────────────────────────────────── */

#define MAX_TASKS 10

typedef struct {
    void (*pTask)(void);    /* The function to execute */
    uint32_t delay;         /* How many ticks to wait before running next */
    uint32_t period;        /* How often to run (in ms) */
    volatile uint8_t run_me; /* Flag set by interrupt, cleared by dispatcher */
} Task_t;

/* ── Public Functions ────────────────────────────────────────────────────── */

/**
 * @brief Initializes the scheduler and hardware timer (SysTick).
 * @param sys_clock_hz The current frequency of the CPU (e.g., 170000000).
 */
void SCH_Init(uint32_t sys_clock_hz);

/**
 * @brief Adds a task to the scheduler list.
 */
void SCH_Add_Task(void (*pTask)(void), uint32_t delay, uint32_t period);

/**
 * @brief The main OS loop. Checks for due tasks and runs them.
 * Call this inside your main while(1) loop.
 */
void SCH_Dispatch_Tasks(void);

/**
 * @brief Starts the scheduler by enabling interrupts.
 */
void SCH_Start(void);

#endif /* SCHEDULER_H */
