#include "task_heartbeat.h"
#include "stm32g474xx.h"

#define LED_PIN 7 /* PC7 = white status LED (replaces retired LD2/PB0) */

void Heartbeat_Task_Init(void) {
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOCEN;

    GPIOC->MODER &= ~(3UL << (LED_PIN * 2));
    GPIOC->MODER |= (1UL << (LED_PIN * 2));
}

void Heartbeat_Task(void) {
    GPIOC->ODR ^= GPIO_ODR_7;
}
