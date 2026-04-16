#include "stm32g474xx.h"
#include "scheduler.h"

/**
 ******************************************************************************
 * @file        main.c
 * @project     EURUS — Environmental Urban Roaming Unified Sensoring
 * @author      Luka Zagar (student at University of Ljubljana, Faculty of Electrical Engineering)
 * 
 * @date        5th April 2026
 * @brief       Application logic using the modular scheduler.
 *
 * @board       NUCLEO-G474RE
 * @mcu         STM32G474RET6 — ARM Cortex-M4 @ 170MHz, 512KB Flash, 128KB RAM
 *
 ******************************************************************************
 */

/* ── Application Tasks ───────────────────────────────────────────────────── */

// Heartbeat Task: Toggles onboard and external LEDs.
void Heartbeat_Task(void) {

    GPIOA->ODR ^= GPIO_ODR_5; /* LD2 (Green onboard LED) */
    GPIOB->ODR ^= GPIO_ODR_0; /* External LED on PB0 */
}

/* ── Main Entry Point ───────────────────────────────────────────────────── */

int main(void) {
    // 1. Hardware Initialization (Register Level)
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN | RCC_AHB2ENR_GPIOBEN;

    GPIOA->MODER &= ~(3UL << (5 * 2)); GPIOA->MODER |= (1UL << (5 * 2));
    GPIOB->MODER &= ~(3UL << (0 * 2)); GPIOB->MODER |= (1UL << (0 * 2));


    // 2. Initialize the OS Scheduler (Running at 170MHz)
    SCH_Init(170000000);

    // 3. Add Application Tasks
    SCH_Add_Task(Heartbeat_Task, 0, 500);

    // 4. Start the OS
    SCH_Start();

    // 5. The Dispatch Loop
    while (1) {
        SCH_Dispatch_Tasks();
    }
}
