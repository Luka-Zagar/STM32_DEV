#include "esp8266.h"
#include "uart.h"
#include "systick.h"

#define LINE_MAX_LEN 128

static char line_buf[LINE_MAX_LEN];
static uint32_t line_len = 0;

static esp_status_t status = ESP_STATUS_IDLE;
static uint32_t deadline_ms = 0;

static int line_is(const char *line, const char *match) {
    while (*line && *match && *line == *match) {
        line++;
        match++;
    }
    return *line == '\0' && *match == '\0';
}

void esp8266_init(USART_TypeDef *uart, uint32_t baud) {
    uart_init(uart, baud);
    line_len = 0;
    status = ESP_STATUS_IDLE;
}

void esp8266_send(USART_TypeDef *uart, const char *cmd, uint32_t timeout_ms) {
    line_len = 0;
    status = ESP_STATUS_WAITING;
    deadline_ms = SysTick_GetMillis() + timeout_ms;

    uart_write_str(uart, cmd);
    uart_write_str(uart, "\r\n");
}

static void handle_line(const char *line) {
    if (line[0] == '\0') return; /* AT firmware pads responses with blank lines */

    if (line_is(line, "OK")) {
        status = ESP_STATUS_OK;
    } else if (line_is(line, "ERROR") || line_is(line, "FAIL")) {
        status = ESP_STATUS_ERROR;
    }
    /* Anything else (echoed command, "+CWJAP:", "WIFI CONNECTED", etc.) is
     * an intermediate line - ignored here; specific commands that need
     * to inspect them get their own handling later. */
}

void esp8266_poll(USART_TypeDef *uart) {
    uint8_t byte;
    while (uart_read_byte(uart, &byte)) {
        if (byte == '\r') continue;
        if (byte == '\n') {
            line_buf[line_len] = '\0';
            handle_line(line_buf);
            line_len = 0;
            continue;
        }
        if (line_len < LINE_MAX_LEN - 1) {
            line_buf[line_len++] = (char)byte;
        } else {
            line_len = 0; /* overflow: drop and resync on the next line */
        }
    }

    if (status == ESP_STATUS_WAITING && (int32_t)(SysTick_GetMillis() - deadline_ms) >= 0) {
        status = ESP_STATUS_TIMEOUT;
    }
}

esp_status_t esp8266_get_status(void) {
    return status;
}
