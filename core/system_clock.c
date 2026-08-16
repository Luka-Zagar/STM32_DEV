#include "stm32g474xx.h"
#include "system_clock.h"

/* Standard STM32G4 boot to 170 MHz:
 * 1. Start on HSI (16 MHz internal oscillator).
 * 2. Set Flash wait states for the higher clock (Flash can't keep up otherwise).
 * 3. Configure PLL: HSI (16MHz) / M(4) * N(85) / R(2) = 170 MHz.
 * 4. Switch SYSCLK to the PLL output.
 */
void SystemInit(void) {
#if (__FPU_PRESENT == 1) && (__FPU_USED == 1)
    SCB->CPACR |= (3UL << 10 * 2) | (3UL << 11 * 2); /* CP10/CP11 full access */
#endif

    RCC->CR |= RCC_CR_HSION;
    while (!(RCC->CR & RCC_CR_HSIRDY));

    FLASH->ACR &= ~0xFUL;
    FLASH->ACR |= FLASH_ACR_LATENCY_4WS | FLASH_ACR_PRFTEN | FLASH_ACR_ICEN | FLASH_ACR_DCEN;

    RCC->PLLCFGR = 0;
    RCC->PLLCFGR |= RCC_PLLCFGR_PLLSRC_HSI;
    RCC->PLLCFGR |= (3UL << RCC_PLLCFGR_PLLM_Pos);  /* M = 4 */
    RCC->PLLCFGR |= (85UL << RCC_PLLCFGR_PLLN_Pos);  /* N = 85 */
    RCC->PLLCFGR |= (0UL << RCC_PLLCFGR_PLLR_Pos);   /* R = 2 */
    RCC->PLLCFGR |= RCC_PLLCFGR_PLLREN;

    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY));

    RCC->CFGR &= ~3UL;
    RCC->CFGR |= RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS_PLL) != RCC_CFGR_SWS_PLL);
}
