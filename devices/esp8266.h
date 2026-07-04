#ifndef ESP8266_H
#define ESP8266_H

#include <stdint.h>
#include "stm32g474xx.h"

typedef enum {
    ESP_STATUS_IDLE,    /* no command in flight */
    ESP_STATUS_WAITING, /* command sent, waiting for a response */
    ESP_STATUS_OK,      /* last command's response line was "OK" */
    ESP_STATUS_ERROR,   /* last command's response line was "ERROR" or "FAIL" */
    ESP_STATUS_TIMEOUT  /* no terminal response within the deadline */
} esp_status_t;

/* Bus-handle convention: esp8266_init(USART3, 115200). */
void esp8266_init(USART_TypeDef *uart, uint32_t baud);

/* Sends an AT command (without \r\n - appended automatically) and starts
 * waiting up to timeout_ms for a terminal "OK"/"ERROR"/"FAIL" response
 * line. Non-blocking - call esp8266_poll() repeatedly to drive it
 * forward. Overwrites any command already in flight. */
void esp8266_send(USART_TypeDef *uart, const char *cmd, uint32_t timeout_ms);

/* Feeds any bytes waiting in the UART's RX ring buffer into the
 * response-line matcher and updates status/timeout. Call frequently
 * (e.g. every scheduler tick); never blocks. */
void esp8266_poll(USART_TypeDef *uart);

esp_status_t esp8266_get_status(void);

/* Temporary debug aid: the most recent non-empty response line seen. */
const char *esp8266_debug_last_line(void);

#endif /* ESP8266_H */
