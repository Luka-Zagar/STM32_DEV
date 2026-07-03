#include "task_heartbeat.h"
#include "stm32g474xx.h"

#define LED_PIN 5 /* PA5 = LD2, on-board user LED (Nucleo-G474RE) */
#define EXT_LED_PIN 0 /* PB0 = external heartbeat LED */

void Heartbeat_Task_Init(void) {
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN | RCC_AHB2ENR_GPIOBEN;

    GPIOA->MODER &= ~(3UL << (LED_PIN * 2));
    GPIOA->MODER |= (1UL << (LED_PIN * 2));

    GPIOB->MODER &= ~(3UL << (EXT_LED_PIN * 2));
    GPIOB->MODER |= (1UL << (EXT_LED_PIN * 2));
}

void Heartbeat_Task(void) {
    GPIOA->ODR ^= GPIO_ODR_5;
    GPIOB->ODR ^= GPIO_ODR_0;
}
