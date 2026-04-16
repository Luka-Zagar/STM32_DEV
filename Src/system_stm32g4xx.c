#include "stm32g474xx.h"

/**
 * @file system_stm32g4xx.c
 * @brief Clock setup for 170MHz.
 * 
 * Standard STM32G4 boot process:
 * 1. Start on HSI (Internal 16MHz clock).
 * 2. Configure Flash wait states (because Flash can't keep up with 170MHz).
 * 3. Set up the PLL to multiply 16MHz up to 170MHz.
 * 4. Switch the CPU to use the PLL.
 */

void SystemInit(void) {
    /* 1. Enable HSI (High Speed Internal) 16MHz oscillator */
    RCC->CR |= RCC_CR_HSION;
    while (!(RCC->CR & RCC_CR_HSIRDY)); /* Wait until HSI is stable */

    /* 2. Configure FLASH Access
       The CPU is much faster than the memory. At 170MHz, we need 4 'Wait States' 
       (LATENCY) so the CPU doesn't get corrupted data from memory. */
    FLASH->ACR &= ~0xF; /* Clear latency bits */
    FLASH->ACR |= FLASH_ACR_LATENCY_4WS | FLASH_ACR_PRFTEN | FLASH_ACR_ICEN | FLASH_ACR_DCEN;

    /* 3. Configure PLL (Phase Locked Loop)
       Math: HSI (16MHz) / M (4) * N (85) / R (2) = 170MHz
    */
    RCC->PLLCFGR = 0; /* Reset config */
    RCC->PLLCFGR |= RCC_PLLCFGR_PLLSRC_HSI;              /* Source = 16 MHz */
    RCC->PLLCFGR |= (3UL << RCC_PLLCFGR_PLLM_Pos);        /* M = 4  (16/4 = 4MHz) */
    RCC->PLLCFGR |= (85UL << RCC_PLLCFGR_PLLN_Pos);       /* N = 85 (4*85 = 340MHz) */
    RCC->PLLCFGR |= (0UL << RCC_PLLCFGR_PLLR_Pos);        /* R = 2  (340/2 = 170MHz) */
    RCC->PLLCFGR |= RCC_PLLCFGR_PLLREN;                   /* Enable the R-output */

    /* 4. Enable PLL and wait for lock */
    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY)); /* Wait until PLL is 'locked' and stable */

    /* 5. Switch System Clock to PLL */
    RCC->CFGR &= ~3UL;              /* Clear switch bits */
    RCC->CFGR |= RCC_CFGR_SW_PLL;    /* Select PLL as source */
    while ((RCC->CFGR & RCC_CFGR_SWS_PLL) != RCC_CFGR_SWS_PLL); /* Wait for switch confirmation */
}
