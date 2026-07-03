#include "stm32g474xx.h"
#include "scheduler.h"
#include <stdio.h>
#include <string.h>
#include "esp8266.h"

/**
 ******************************************************************************
 * @file        main.c
 * @brief       EURUS Main Logic - MQTT Sensor Node
 ******************************************************************************
 */

int __io_putchar(int ch) {
    while (!(USART2->ISR & USART_ISR_TXE));
    USART2->TDR = ch;
    return ch;
}

void UART2_Init(void) {
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN;
    RCC->APB1ENR1 |= RCC_APB1ENR1_USART2EN;
    GPIOA->MODER &= ~(3UL << (2 * 2)); GPIOA->MODER |= (2UL << (2 * 2));
    GPIOA->AFR[0] &= ~(0xF << (2 * 4)); GPIOA->AFR[0] |= (7UL << (2 * 4));
    GPIOA->MODER &= ~(3UL << (3 * 2)); GPIOA->MODER |= (2UL << (3 * 2));
    GPIOA->AFR[0] &= ~(0xF << (3 * 4)); GPIOA->AFR[0] |= (7UL << (3 * 4));
    USART2->BRR = 1476;
    USART2->CR1 = USART_CR1_UE | USART_CR1_RE | USART_CR1_TE;
}

void Heartbeat_Task(void) {
    GPIOA->ODR ^= GPIO_ODR_5;
    GPIOB->ODR ^= GPIO_ODR_0;
}

/* Add this task function above main() */
void Sensor_Task(void) {
    if (!ESP_IsReady()) return;

    /* Generate dummy data (Seed using SysTick) */
    static uint32_t seed = 0;
    if (seed == 0) seed = SYSTICK->VAL;
    
    float temp     = 20.0f  + (float)(seed % 100) / 10.0f;
    float pressure = 980.0f + (float)(seed % 400) / 10.0f;
    float humidity = 40.0f  + (float)(seed % 200) / 10.0f;
    float gas      = 100.0f + (float)(seed % 4000) / 10.0f;
    seed = (seed * 1103515245 + 12345) & 0x7fffffff; // LCG

    ESP_Publish_SensorData(temp, pressure, humidity, gas);
    printf("MQTT: Sent Sensor Data (T:%.1f, P:%.1f, H:%.1f, G:%.1f)\r\n", 
           temp, pressure, humidity, gas);
}

int main(void) {
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN | RCC_AHB2ENR_GPIOBEN | RCC_AHB2ENR_GPIOCEN;
    
    /* LED pins */
    GPIOA->MODER &= ~(3UL << (5 * 2)); GPIOA->MODER |= (1UL << (5 * 2));
    GPIOB->MODER &= ~(3UL << (0 * 2)); GPIOB->MODER |= (1UL << (0 * 2));

    UART2_Init();
    printf("\r\n--- EURUS MQTT Node ---\r\n");

    ESP_Init();
    SCH_Init(170000000);
    
    SCH_Add_Task(Heartbeat_Task, 0, 500);
    SCH_Add_Task(ESP_Task, 0, 100);        /* Run ESP state machine every 100ms */
    SCH_Add_Task(Sensor_Task, 1000, 10000); /* Runs every 10 seconds */

    SCH_Start();
    while (1) {
        SCH_Dispatch_Tasks();
    }
}
