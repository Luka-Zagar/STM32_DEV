#include "uart.h"
#include "system_clock.h"

#define UART_RX_BUF_SIZE 128

typedef struct {
    volatile uint8_t buf[UART_RX_BUF_SIZE];
    volatile uint16_t head;
    volatile uint16_t tail;
} uart_rxbuf_t;

/* Slot 0 = USART1 (GPS), slot 1 = USART2 (console). Extend as USART3
 * (ESP8266) gets wired. */
static uart_rxbuf_t rx_bufs[2];

static int uart_slot(USART_TypeDef *uart) {
    if (uart == USART1) return 0;
    if (uart == USART2) return 1;
    return -1;
}

static void uart_gpio_init(USART_TypeDef *uart) {
    if (uart == USART1) {
        /* PC4 = TX, PC5 = RX, AF7 - to the GPS module (NEO-8M) */
        RCC->AHB2ENR |= RCC_AHB2ENR_GPIOCEN;
        GPIOC->MODER &= ~((3UL << (4 * 2)) | (3UL << (5 * 2)));
        GPIOC->MODER |= (2UL << (4 * 2)) | (2UL << (5 * 2));
        GPIOC->AFR[0] &= ~((0xFUL << (4 * 4)) | (0xFUL << (5 * 4)));
        GPIOC->AFR[0] |= (7UL << (4 * 4)) | (7UL << (5 * 4));
    } else if (uart == USART2) {
        /* PA2 = TX, PA3 = RX, AF7 - routed to the ST-LINK VCP on Nucleo-G474RE */
        RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN;
        GPIOA->MODER &= ~((3UL << (2 * 2)) | (3UL << (3 * 2)));
        GPIOA->MODER |= (2UL << (2 * 2)) | (2UL << (3 * 2));
        GPIOA->AFR[0] &= ~((0xFUL << (2 * 4)) | (0xFUL << (3 * 4)));
        GPIOA->AFR[0] |= (7UL << (2 * 4)) | (7UL << (3 * 4));
    }
}

void uart_init(USART_TypeDef *uart, uint32_t baud) {
    int slot = uart_slot(uart);
    if (slot < 0) return;

    uart_gpio_init(uart);

    if (uart == USART1) {
        RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
    } else if (uart == USART2) {
        RCC->APB1ENR1 |= RCC_APB1ENR1_USART2EN;
    }

    rx_bufs[slot].head = 0;
    rx_bufs[slot].tail = 0;

    uart->BRR = (SYSTEM_CLOCK_HZ + baud / 2) / baud;
    uart->CR1 = USART_CR1_UE | USART_CR1_RE | USART_CR1_TE | USART_CR1_RXNEIE;

    if (uart == USART1) {
        NVIC_EnableIRQ(USART1_IRQn);
    } else if (uart == USART2) {
        NVIC_EnableIRQ(USART2_IRQn);
    }
}

void uart_write_byte(USART_TypeDef *uart, uint8_t byte) {
    while (!(uart->ISR & USART_ISR_TXE));
    uart->TDR = byte;
}

void uart_write(USART_TypeDef *uart, const uint8_t *data, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        uart_write_byte(uart, data[i]);
    }
}

void uart_write_str(USART_TypeDef *uart, const char *s) {
    while (*s) {
        uart_write_byte(uart, (uint8_t)*s++);
    }
}

void uart_write_uint(USART_TypeDef *uart, uint32_t v) {
    char digits[10];
    int n = 0;
    do {
        digits[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (n > 0) {
        uart_write_byte(uart, (uint8_t)digits[--n]);
    }
}

void uart_write_int(USART_TypeDef *uart, int32_t v) {
    if (v < 0) {
        uart_write_byte(uart, '-');
        v = -v;
    }
    uart_write_uint(uart, (uint32_t)v);
}

int uart_read_byte(USART_TypeDef *uart, uint8_t *out) {
    int slot = uart_slot(uart);
    if (slot < 0) return 0;

    uart_rxbuf_t *rb = &rx_bufs[slot];
    if (rb->head == rb->tail) return 0; /* empty */

    *out = rb->buf[rb->tail];
    rb->tail = (uint16_t)((rb->tail + 1) % UART_RX_BUF_SIZE);
    return 1;
}

static void uart_isr_common(USART_TypeDef *uart, int slot) {
    if (uart->ISR & USART_ISR_RXNE) {
        uint8_t byte = (uint8_t)uart->RDR; /* reading RDR clears RXNE */
        uart_rxbuf_t *rb = &rx_bufs[slot];
        uint16_t next = (uint16_t)((rb->head + 1) % UART_RX_BUF_SIZE);
        if (next != rb->tail) { /* drop the byte if the buffer is full */
            rb->buf[rb->head] = byte;
            rb->head = next;
        }
    }
    if (uart->ISR & USART_ISR_ORE) {
        uart->ICR |= USART_ICR_ORECF;
    }
}

void USART1_IRQHandler(void) {
    uart_isr_common(USART1, 0);
}

void USART2_IRQHandler(void) {
    uart_isr_common(USART2, 1);
}
