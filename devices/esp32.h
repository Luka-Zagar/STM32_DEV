#ifndef ESP32_H
#define ESP32_H

#include <stdint.h>
#include "stm32g474xx.h"

/* Generic line-based AT-command engine for Espressif's esp-at firmware -
 * chip-agnostic in principle (same firmware project on ESP8266/ESP32),
 * but this file specifically also recognizes the ESP32's native
 * AT+MQTTxxx command set (AT+MQTTCONN/+MQTTCONNECTED/+MQTTPUB:OK/etc),
 * which the ESP8266's old 2016 AT firmware never had - hence the name
 * and the swap from devices/esp8266.c (see docs/pinout.md). Bus-handle
 * convention as usual: esp32_init(USART3, 115200). */

typedef enum {
    ESP_STATUS_IDLE,    /* no command in flight */
    ESP_STATUS_WAITING, /* command sent, waiting for a response */
    ESP_STATUS_OK,      /* terminal response was "OK" (or an OK-equivalent - see esp32.c) */
    ESP_STATUS_ERROR,   /* terminal response was "ERROR"/"FAIL" (or an equivalent) */
    ESP_STATUS_TIMEOUT  /* no terminal response within the deadline */
} esp_status_t;

void esp32_init(USART_TypeDef *uart, uint32_t baud);

/* Sends an AT command (without \r\n - appended automatically) and starts
 * waiting up to timeout_ms for a terminal response line. Non-blocking -
 * call esp32_poll() repeatedly to drive it forward. Overwrites any
 * command already in flight. */
void esp32_send(USART_TypeDef *uart, const char *cmd, uint32_t timeout_ms);

/* Feeds any bytes waiting in the UART's RX ring buffer into the
 * response-line matcher and updates status/timeout. Call frequently
 * (e.g. every scheduler tick) EXCEPT while a esp32_prompt_begin() wait
 * is in progress (see below - they must not both consume the RX ring
 * buffer in the same tick). Never blocks. */
void esp32_poll(USART_TypeDef *uart);

esp_status_t esp32_get_status(void);

/* Full raw response (all lines, newline-joined) since the last
 * esp32_send() call - lets the console show exactly what the module
 * said, useful for multi-line responses (AT+GMR) and diagnosing
 * unexpected ERROR/timeout on any command. */
const char *esp32_debug_response(void);

/* Re-arms the response wait (new deadline, reset OK/ERROR detection)
 * WITHOUT sending anything - for AT flows with a second terminal
 * response after some other action in between, e.g. AT+MQTTPUBRAW:
 * after the '>' prompt and the raw payload bytes are written directly
 * via the uart driver, this waits for "+MQTTPUB:OK"/"+MQTTPUB:FAIL". */
void esp32_wait_again(uint32_t timeout_ms);

/* MQTT link state - updated the instant a "+MQTTCONNECTED" or
 * "+MQTTDISCONNECTED" line is seen, independent of whatever command (if
 * any) is currently in flight. The module can report a drop
 * asynchronously at any time (broker restart, network blip), not just
 * as a direct reply to something we sent - task_wifi.c polls this every
 * tick while idle/publishing to notice a drop and reconnect. */
int esp32_mqtt_is_connected(void);

/* WiFi station link state - updated the instant a "WIFI DISCONNECT" or
 * "WIFI GOT IP" line is seen, same "asynchronous, not just a direct
 * reply" reasoning as esp32_mqtt_is_connected(). Going out of an AP's
 * range and back is exactly this: unsolicited, can happen at any time,
 * and (unlike a dropped MQTT link) leaves nothing for AT+MQTTCONN to
 * reconnect to until WiFi itself rejoins - task_wifi.c polls this to
 * notice and drive a fresh AT+CWJAP rather than retrying MQTT forever
 * against a dead network. */
int esp32_wifi_is_joined(void);

/* AT+MQTTPUBRAW's payload-ready signal is a single '>' byte, not a
 * newline-terminated line - needs byte-level waiting instead of the
 * line parser above. Call esp32_prompt_poll() INSTEAD OF esp32_poll()
 * while a wait is in progress (same non-overlap rule the old ESP8266
 * driver's +IPD reader had). */
void esp32_prompt_begin(uint32_t timeout_ms);

/* Returns 1 once '>' has been seen (ready for payload bytes), 0 while
 * still waiting, -1 on timeout. */
int esp32_prompt_poll(USART_TypeDef *uart);

#endif /* ESP32_H */
