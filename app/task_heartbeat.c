#include "task_heartbeat.h"
#include "stm32g474xx.h"

#define LED_PIN 5 /* PA5 = LD2, on-board user LED (Nucleo-G474RE) */

void Heartbeat_Task_Init(void) {
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN;
    GPIOA->MODER &= ~(3UL << (LED_PIN * 2));
    GPIOA->MODER |= (1UL << (LED_PIN * 2));
}

void Heartbeat_Task(void) {
    GPIOA->ODR ^= GPIO_ODR_5;
}
