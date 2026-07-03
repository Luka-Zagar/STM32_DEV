#ifndef SYSTICK_H
#define SYSTICK_H

#include <stdint.h>

/* Configures SysTick for a 1ms tick, assuming SYSCLK = 170 MHz (system_clock.c). */
void SysTick_Init(void);

/* Milliseconds elapsed since SysTick_Init(). Wraps at ~49.7 days. */
uint32_t SysTick_GetMillis(void);

#endif /* SYSTICK_H */
