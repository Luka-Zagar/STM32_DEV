/**
 ******************************************************************************
 * @file        stm32g474xx.h
 * @brief       Minimalist register definitions for STM32G474xx.
 *              These structures map C-code directly to hardware memory addresses.
 ******************************************************************************
 */

#ifndef STM32G474XX_H
#define STM32G474XX_H

#include <stdint.h>

/* __IO is a shortcut for 'volatile'. It tells the compiler NOT to optimize 
   these variables because their value can change outside of the program 
   flow (i.e., by the hardware itself). */
#define __IO    volatile

/* ── RCC (Reset and Clock Control) ────────────────────────────────────────── 
   Base Address: 0x40021000
   This peripheral controls which parts of the chip are "awake" and how fast 
   they run. 
*/
typedef struct {
    __IO uint32_t CR;            /* 0x00: Clock Control (Turn on HSI, PLL, etc.) */
    __IO uint32_t ICSCR;         /* 0x04: Internal Clock Calibration */
    __IO uint32_t CFGR;          /* 0x08: Clock Configuration (Select SysClock source) */
    __IO uint32_t PLLCFGR;       /* 0x0C: PLL Configuration (The math for 170MHz) */
    __IO uint32_t Reserved0[2];  /* 0x10-0x14: Reserved space in memory map */
    __IO uint32_t CIER;          /* 0x18: Clock Interrupt Enable */
    __IO uint32_t CIFR;          /* 0x1C: Clock Interrupt Flag */
    __IO uint32_t CICR;          /* 0x20: Clock Interrupt Clear */
    __IO uint32_t Reserved1[9];  /* 0x24-0x44: Padding to reach offset 0x48 */
    __IO uint32_t AHB1ENR;       /* 0x48: AHB1 Peripheral Clock Enable */
    __IO uint32_t AHB2ENR;       /* 0x4C: AHB2 Peripheral Clock Enable (GPIOs live here) */
    __IO uint32_t AHB3ENR;       /* 0x50: AHB3 Peripheral Clock Enable */
} RCC_TypeDef;

#define RCC_BASE            (0x40021000UL)
#define RCC                 ((RCC_TypeDef *) RCC_BASE)

/* RCC Bit Definitions */
#define RCC_CR_HSION        (1UL << 8)   /* Internal High Speed clock enable */
#define RCC_CR_HSIRDY       (1UL << 10)  /* Internal High Speed clock ready flag */
#define RCC_CR_PLLON        (1UL << 24)  /* Main PLL enable */
#define RCC_CR_PLLRDY       (1UL << 25)  /* Main PLL ready flag */

#define RCC_PLLCFGR_PLLSRC_HSI (2UL << 0) /* Set HSI as PLL source */
#define RCC_PLLCFGR_PLLM_Pos   (4U)       /* PLL divider M position */
#define RCC_PLLCFGR_PLLN_Pos   (8U)       /* PLL multiplier N position */
#define RCC_PLLCFGR_PLLR_Pos   (25U)      /* PLL divider R position */
#define RCC_PLLCFGR_PLLREN     (1UL << 24)/* PLL R-output enable */

#define RCC_CFGR_SW_PLL        (3UL << 0) /* Select PLL as System Clock */
#define RCC_CFGR_SWS_PLL       (3UL << 2) /* Check if PLL is used as System Clock */

#define RCC_AHB2ENR_GPIOAEN    (1UL << 0) /* Enable Clock for Port A */
#define RCC_AHB2ENR_GPIOBEN    (1UL << 1) /* Enable Clock for Port B */

/* ── FLASH ──────────────────────────────────────────────────────────────── 
   Flash memory is slower than the CPU. We need "Wait States" (Latency) 
   so the CPU doesn't try to read data faster than the Flash can provide it.
*/
typedef struct {
    __IO uint32_t ACR;           /* 0x00: Access Control Register */
} FLASH_TypeDef;

#define FLASH_BASE          (0x40022000UL)
#define FLASH               ((FLASH_TypeDef *) FLASH_BASE)

#define FLASH_ACR_LATENCY_4WS  (4UL << 0) /* 4 Wait States needed for 170MHz */
#define FLASH_ACR_PRFTEN       (1UL << 8) /* Prefetch Enable (Performance) */
#define FLASH_ACR_ICEN         (1UL << 9) /* Instruction Cache Enable */
#define FLASH_ACR_DCEN         (1UL << 10)/* Data Cache Enable */

/* ── GPIO (General Purpose I/O) ───────────────────────────────────────────── 
   Each port (A, B, C...) has its own set of these registers.
*/
typedef struct {
    __IO uint32_t MODER;         /* 0x00: Mode (Input, Output, Alt, Analog) */
    __IO uint32_t OTYPER;        /* 0x04: Output Type (Push-Pull, Open-Drain) */
    __IO uint32_t OSPEEDR;       /* 0x08: Output Speed (Low, Medium, High, Very High) */
    __IO uint32_t PUPDR;         /* 0x0C: Pull-up / Pull-down */
    __IO uint32_t IDR;           /* 0x10: Input Data (Read pins here) */
    __IO uint32_t ODR;           /* 0x14: Output Data (Write pins here) */
    __IO uint32_t BSRR;          /* 0x18: Bit Set/Reset (Atomic pin control) */
    __IO uint32_t LCKR;          /* 0x1C: Configuration Lock */
    __IO uint32_t AFR[2];        /* 0x20-0x24: Alternate Function selection */
    __IO uint32_t BRR;           /* 0x28: Bit Reset */
} GPIO_TypeDef;

#define GPIOA_BASE          (0x48000000UL)
#define GPIOA               ((GPIO_TypeDef *) GPIOA_BASE)
#define GPIOB_BASE          (0x48000400UL)
#define GPIOB               ((GPIO_TypeDef *) GPIOB_BASE)

#define GPIO_ODR_0             (1UL << 0) /* Bit mask for Pin 0 */
#define GPIO_ODR_5             (1UL << 5) /* Bit mask for Pin 5 */

/* ── SysTick ────────────────────────────────────────────────────────────── 
   A simple 24-bit down-counter inside the ARM Cortex-M core used for 
   generating periodic interrupts (our OS Tick).
*/
typedef struct {
    __IO uint32_t CTRL;          /* 0x00: Control and Status */
    __IO uint32_t LOAD;          /* 0x04: Reload Value (Sets the frequency) */
    __IO uint32_t VAL;           /* 0x08: Current Value */
    __IO uint32_t CALIB;         /* 0x0C: Calibration */
} SysTick_TypeDef;

#define SYSTICK_BASE        (0xE000E010UL)
#define SYSTICK             ((SysTick_TypeDef *) SYSTICK_BASE)

#define SYSTICK_CTRL_ENABLE    (1UL << 0) /* Start the timer */
#define SYSTICK_CTRL_TICKINT   (1UL << 1) /* Enable the interrupt */
#define SYSTICK_CTRL_CLKSOURCE (1UL << 2) /* Use Processor Clock source */

#endif /* STM32G474XX_H */
