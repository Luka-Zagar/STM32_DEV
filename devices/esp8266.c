#include "esp8266.h"
#include "uart.h"
#include "systick.h"

#define LINE_MAX_LEN 128
#define RESPONSE_CAP 256

static char line_buf[LINE_MAX_LEN];
static uint32_t line_len = 0;

static esp_status_t status = ESP_STATUS_IDLE;
static uint32_t deadline_ms = 0;

/* Every line seen since the current esp8266_send(), newline-joined and
 * bounded - lets the console show the module's actual raw response
 * (multi-line commands like AT+GMR included), instead of guessing blind
 * from just OK/ERROR. Reset on each new send(). */
static char response_buf[RESPONSE_CAP];
static uint32_t response_len = 0;

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
    response_len = 0;
    response_buf[0] = '\0';
    status = ESP_STATUS_WAITING;
    deadline_ms = SysTick_GetMillis() + timeout_ms;

    uart_write_str(uart, cmd);
    uart_write_str(uart, "\r\n");
}

static void handle_line(const char *line) {
    if (line[0] == '\0') return; /* AT firmware pads responses with blank lines */

    if (response_len > 0 && response_len < RESPONSE_CAP - 1) {
        response_buf[response_len++] = '\n';
    }
    uint32_t i = 0;
    while (line[i] && response_len < RESPONSE_CAP - 1) {
        response_buf[response_len++] = line[i++];
    }
    response_buf[response_len] = '\0';

    if (line_is(line, "OK") || line_is(line, "SEND OK")) {
        status = ESP_STATUS_OK;
    } else if (line_is(line, "ERROR") || line_is(line, "FAIL") || line_is(line, "SEND FAIL")) {
        status = ESP_STATUS_ERROR;
    }
    /* Anything else (echoed command, "+CWJAP:", "WIFI CONNECTED", etc.) is
     * an intermediate line - ignored here; specific commands that need
     * to inspect them get their own handling later. */
}

void esp8266_wait_again(uint32_t timeout_ms) {
    response_len = 0;
    response_buf[0] = '\0';
    status = ESP_STATUS_WAITING;
    deadline_ms = SysTick_GetMillis() + timeout_ms;
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

const char *esp8266_debug_response(void) {
    return response_buf;
}

/* ── +IPD raw-frame capture ─────────────────────────────────────────────
 * TCP data pushed by the module arrives as "+IPD,<len>:<len raw bytes>",
 * and those raw bytes are an arbitrary binary protocol payload (MQTT's
 * CONNACK here) that can legitimately contain '\r'/'\n' - the ordinary
 * line-based parser above would misframe it. This is a separate,
 * self-contained consumer of the same RX ring buffer; never call this
 * and esp8266_poll() in the same tick.
 */
#define IPD_DATA_CAP 32
#define IPD_MARKER "+IPD,"

typedef enum { IPD_IDLE, IPD_MARKER_MATCH, IPD_LEN, IPD_DATA, IPD_DONE, IPD_TIMEOUT } ipd_state_t;

static ipd_state_t ipd_state = IPD_IDLE;
static uint32_t ipd_marker_pos = 0;
static uint32_t ipd_expected_len = 0;
static uint8_t ipd_data[IPD_DATA_CAP];
static uint32_t ipd_data_len = 0;
static uint32_t ipd_deadline_ms = 0;

void esp8266_ipd_begin(uint32_t timeout_ms) {
    ipd_state = IPD_MARKER_MATCH;
    ipd_marker_pos = 0;
    ipd_expected_len = 0;
    ipd_data_len = 0;
    ipd_deadline_ms = SysTick_GetMillis() + timeout_ms;
}

int esp8266_ipd_poll(USART_TypeDef *uart) {
    uint8_t byte;
    while (uart_read_byte(uart, &byte)) {
        switch (ipd_state) {
            case IPD_MARKER_MATCH:
                if ((char)byte == IPD_MARKER[ipd_marker_pos]) {
                    ipd_marker_pos++;
                    if (IPD_MARKER[ipd_marker_pos] == '\0') {
                        ipd_state = IPD_LEN;
                        ipd_expected_len = 0;
                    }
                } else {
                    /* Restart the match; the failing byte might itself be
                     * the '+' that starts a fresh attempt. */
                    ipd_marker_pos = ((char)byte == IPD_MARKER[0]) ? 1 : 0;
                }
                break;

            case IPD_LEN:
                if (byte >= '0' && byte <= '9') {
                    ipd_expected_len = ipd_expected_len * 10 + (uint32_t)(byte - '0');
                } else if (byte == ':') {
                    if (ipd_expected_len > IPD_DATA_CAP) ipd_expected_len = IPD_DATA_CAP; /* truncate defensively */
                    ipd_data_len = 0;
                    ipd_state = (ipd_expected_len > 0) ? IPD_DATA : IPD_DONE;
                }
                break;

            case IPD_DATA:
                if (ipd_data_len < IPD_DATA_CAP) ipd_data[ipd_data_len] = byte;
                ipd_data_len++;
                if (ipd_data_len >= ipd_expected_len) {
                    ipd_state = IPD_DONE;
                    return 1;
                }
                break;

            default:
                break;
        }
    }

    if (ipd_state != IPD_DONE && (int32_t)(SysTick_GetMillis() - ipd_deadline_ms) >= 0) {
        ipd_state = IPD_TIMEOUT;
        return -1;
    }
    return (ipd_state == IPD_DONE) ? 1 : 0;
}

const uint8_t *esp8266_ipd_data(void) {
    return ipd_data;
}

uint32_t esp8266_ipd_len(void) {
    return ipd_data_len;
}
