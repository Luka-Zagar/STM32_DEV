#ifndef SYSTEM_CLOCK_H
#define SYSTEM_CLOCK_H

#define SYSTEM_CLOCK_HZ 170000000UL

/* Called by the reset handler before .data/.bss init and main().
 * Brings the core up to 170 MHz: HSI (16 MHz) -> PLL -> SYSCLK,
 * with Flash latency set for the higher clock.
 */
void SystemInit(void);

#endif /* SYSTEM_CLOCK_H */
