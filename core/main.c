#include "stm32g474xx.h"
#include "system_clock.h"
#include "systick.h"
#include "scheduler.h"
#include "task_console.h"

#define LED_PIN 5 /* PA5 = LD2, on-board user LED (Nucleo-G474RE) */

static void led_init(void) {
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN;
    GPIOA->MODER &= ~(3UL << (LED_PIN * 2));
    GPIOA->MODER |= (1UL << (LED_PIN * 2));
}

static void Heartbeat_Task(void) {
    GPIOA->ODR ^= GPIO_ODR_5;
}

int main(void) {
    led_init();
    SysTick_Init();
    SCH_Init();

    int heartbeat_task_id = SCH_Add_Task(Heartbeat_Task, 0, 500);
    Console_Task_Init(heartbeat_task_id);
    SCH_Add_Task(Console_Task, 0, 10);

    while (1) {
        SCH_Dispatch();
    }
}
