#include "esp32.h"
#include "uart.h"
#include "systick.h"

#define LINE_MAX_LEN 128
#define RESPONSE_CAP 256

static char line_buf[LINE_MAX_LEN];
static uint32_t line_len = 0;

static esp_status_t status = ESP_STATUS_IDLE;
static uint32_t deadline_ms = 0;

/* Every line seen since the current esp32_send()/esp32_wait_again(),
 * newline-joined and bounded - lets the console show the module's
 * actual raw response instead of guessing blind from just OK/ERROR.
 * Reset on each new send()/wait_again(). */
static char response_buf[RESPONSE_CAP];
static uint32_t response_len = 0;

static int mqtt_connected = 0;

static int line_is(const char *line, const char *match) {
    while (*line && *match && *line == *match) {
        line++;
        match++;
    }
    return *line == '\0' && *match == '\0';
}

/* +MQTTCONNECTED/+MQTTDISCONNECTED aren't bare tokens - the real wire
 * format is "+MQTTCONNECTED:<LinkID>,<scheme>,<host>,<port>,<path>,
 * <reconnect>" and "+MQTTDISCONNECTED:<LinkID>" (confirmed against
 * Espressif's esp-at MQTT AT command docs) - line_is()'s exact match
 * would never fire on these, silently leaving esp32_mqtt_is_connected()
 * stuck at 0 forever even after a real successful connect. Prefix match
 * instead; we don't need to parse the LinkID/etc, just notice the event. */
static int starts_with(const char *line, const char *prefix) {
    while (*prefix) {
        if (*line != *prefix) return 0;
        line++;
        prefix++;
    }
    return 1;
}

void esp32_init(USART_TypeDef *uart, uint32_t baud) {
    uart_init(uart, baud);
    line_len = 0;
    status = ESP_STATUS_IDLE;
    mqtt_connected = 0;
}

static void arm_wait(uint32_t timeout_ms) {
    line_len = 0;
    response_len = 0;
    response_buf[0] = '\0';
    status = ESP_STATUS_WAITING;
    deadline_ms = SysTick_GetMillis() + timeout_ms;
}

void esp32_send(USART_TypeDef *uart, const char *cmd, uint32_t timeout_ms) {
    arm_wait(timeout_ms);
    uart_write_str(uart, cmd);
    uart_write_str(uart, "\r\n");
}

void esp32_wait_again(uint32_t timeout_ms) {
    arm_wait(timeout_ms);
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

    /* MQTT link state - tracked unconditionally, whether or not a
     * command is currently in flight (see esp32_mqtt_is_connected()'s
     * doc comment: the module reports these asynchronously). */
    if (starts_with(line, "+MQTTCONNECTED")) mqtt_connected = 1;
    else if (starts_with(line, "+MQTTDISCONNECTED")) mqtt_connected = 0;

    /* "OK"/"SEND OK" are the plain-AT terminal successes; "+MQTTPUB:OK"
     * is AT+MQTTPUBRAW's own terminal success after the raw payload
     * bytes go out (see esp32_wait_again()'s doc comment). Same idea for
     * the error side. One unified status instead of a special case per
     * command - matches how CIPSEND's "OK"/"SEND OK" pair used to work. */
    if (line_is(line, "OK") || line_is(line, "SEND OK") || line_is(line, "+MQTTPUB:OK")) {
        status = ESP_STATUS_OK;
    } else if (line_is(line, "ERROR") || line_is(line, "FAIL") ||
               line_is(line, "SEND FAIL") || line_is(line, "+MQTTPUB:FAIL")) {
        status = ESP_STATUS_ERROR;
    }
    /* Anything else (echoed command, "+CWJAP:", "WIFI CONNECTED", etc.) is
     * an intermediate line - ignored here; specific commands that need
     * to inspect them get their own handling later. */
}

void esp32_poll(USART_TypeDef *uart) {
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

esp_status_t esp32_get_status(void) {
    return status;
}

const char *esp32_debug_response(void) {
    return response_buf;
}

int esp32_mqtt_is_connected(void) {
    return mqtt_connected;
}

/* ── AT+MQTTPUBRAW payload prompt ('>') ─────────────────────────────────
 * Unlike a response line, the module's "ready for payload bytes" signal
 * is a single '>' byte with no CRLF around it - the line parser above
 * would just sit there waiting for a '\n' that never comes. This is a
 * separate, self-contained consumer of the same RX ring buffer; never
 * call this and esp32_poll() in the same tick (same rule the old
 * ESP8266 driver's +IPD reader had, for the same reason).
 */
typedef enum { PROMPT_IDLE, PROMPT_WAITING, PROMPT_DONE, PROMPT_TIMEOUT } prompt_state_t;

static prompt_state_t prompt_state = PROMPT_IDLE;
static uint32_t prompt_deadline_ms = 0;

void esp32_prompt_begin(uint32_t timeout_ms) {
    prompt_state = PROMPT_WAITING;
    prompt_deadline_ms = SysTick_GetMillis() + timeout_ms;
}

int esp32_prompt_poll(USART_TypeDef *uart) {
    uint8_t byte;
    while (uart_read_byte(uart, &byte)) {
        if (byte == '>') {
            prompt_state = PROMPT_DONE;
            return 1;
        }
        /* Anything else (echoed command text, blank lines) is discarded -
         * we only care about spotting the prompt byte itself. */
    }

    if (prompt_state != PROMPT_DONE && (int32_t)(SysTick_GetMillis() - prompt_deadline_ms) >= 0) {
        prompt_state = PROMPT_TIMEOUT;
        return -1;
    }
    return (prompt_state == PROMPT_DONE) ? 1 : 0;
}
