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

/* Full raw response (all lines, newline-joined) since the last
 * esp8266_send() call - lets the console show exactly what the module
 * said, useful for multi-line responses (AT+GMR) and diagnosing
 * unexpected ERROR/timeout on any command. */
const char *esp8266_debug_response(void);

/* Re-arms the response wait (new deadline, reset OK/ERROR detection)
 * WITHOUT sending anything - for AT flows with two terminal responses,
 * e.g. AT+CIPSEND: first "OK" (ready for data), then - after the raw
 * payload bytes are written directly via the uart driver - "SEND OK"/
 * "SEND FAIL" once the module has actually transmitted them. */
void esp8266_wait_again(uint32_t timeout_ms);

/* Starts a raw-frame capture: waits for "+IPD,<len>:" then captures
 * exactly <len> raw bytes (bounded), bypassing line-based parsing -
 * needed for binary protocol responses (e.g. an MQTT CONNACK) where
 * '\r'/'\n' bytes can legitimately appear inside the payload. Call
 * esp8266_ipd_poll() INSTEAD OF esp8266_poll() while a capture is in
 * progress - they must not both consume the RX ring buffer at once. */
void esp8266_ipd_begin(uint32_t timeout_ms);

/* Returns 1 once the frame is fully captured, 0 while still waiting,
 * -1 on timeout. */
int esp8266_ipd_poll(USART_TypeDef *uart);

const uint8_t *esp8266_ipd_data(void);
uint32_t esp8266_ipd_len(void);

#endif /* ESP8266_H */
