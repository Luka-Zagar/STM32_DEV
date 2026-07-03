#include "stm32g474xx.h"
#include "system_clock.h"
#include "systick.h"

#define LED_PIN 5 /* PA5 = LD2, on-board user LED (Nucleo-G474RE) */

static void led_init(void) {
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN;
    GPIOA->MODER &= ~(3UL << (LED_PIN * 2));
    GPIOA->MODER |= (1UL << (LED_PIN * 2)); /* general purpose output */
}

int main(void) {
    led_init();
    SysTick_Init();

    uint32_t last_toggle = SysTick_GetMillis();
    while (1) {
        uint32_t now = SysTick_GetMillis();
        if (now - last_toggle >= 500) {
            GPIOA->ODR ^= GPIO_ODR_5;
            last_toggle = now;
        }
    }
}
