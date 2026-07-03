#ifndef UART_H
#define UART_H

#include <stdint.h>
#include "stm32g474xx.h"

/* Instance-parameterized UART driver: uart_init(USART2, 115200), never
 * console_init(). GPIO/RCC wiring per instance is added inside uart.c as
 * each peripheral gets physically wired (see docs/pinout.md); today only
 * USART2 (ST-LINK VCP) is supported.
 *
 * RX is interrupt-driven into a per-instance ring buffer so callers never
 * block waiting on incoming bytes.
 */
void uart_init(USART_TypeDef *uart, uint32_t baud);

void uart_write_byte(USART_TypeDef *uart, uint8_t byte);
void uart_write(USART_TypeDef *uart, const uint8_t *data, uint32_t len);
void uart_write_str(USART_TypeDef *uart, const char *s);

/* Writes the decimal representation of v. No floats, no printf. */
void uart_write_uint(USART_TypeDef *uart, uint32_t v);

/* Non-blocking. Returns 1 and stores the byte in *out if one was available. */
int uart_read_byte(USART_TypeDef *uart, uint8_t *out);

#endif /* UART_H */
