#include "stm32g474xx.h"
#include "systick.h"

#define SYSCLK_HZ 170000000UL

static volatile uint32_t ms_ticks = 0;

void SysTick_Init(void) {
    SYSTICK->LOAD = (SYSCLK_HZ / 1000UL) - 1UL;
    SYSTICK->VAL = 0;
    SYSTICK->CTRL = SYSTICK_CTRL_CLKSOURCE | SYSTICK_CTRL_TICKINT | SYSTICK_CTRL_ENABLE;
}

uint32_t SysTick_GetMillis(void) {
    return ms_ticks;
}

void SysTick_Handler(void) {
    ms_ticks++;
}
