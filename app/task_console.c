#include "task_console.h"
#include "stm32g474xx.h"
#include "uart.h"
#include "scheduler.h"
#include "gps_neo8m.h"

#define CONSOLE_UART USART2
#define CONSOLE_BAUD 115200
#define LINE_BUF_SIZE 32

typedef enum { CONSOLE_IDLE, CONSOLE_EDITING } console_state_t;

static int heartbeat_task_id = -1;
static int status_task_id = -1;
static console_state_t state = CONSOLE_IDLE;

/* Polls the console for a completed line (CR/LF-terminated), echoing bytes
 * and handling backspace as they arrive. Returns 1 and NUL-terminates
 * `line` once Enter is pressed on a non-empty line, 0 otherwise. */
static int console_poll_line(char *line, uint32_t max_len) {
    static char buf[LINE_BUF_SIZE];
    static uint32_t len = 0;
    uint8_t byte;

    while (uart_read_byte(CONSOLE_UART, &byte)) {
        if (byte == '\r' || byte == '\n') {
            uart_write_str(CONSOLE_UART, "\r\n");
            if (len == 0) continue;
            buf[len] = '\0';
            for (uint32_t i = 0; i <= len && i < max_len; i++) line[i] = buf[i];
            len = 0;
            return 1;
        }
        if (byte == '\b' || byte == 0x7F) {
            if (len > 0) {
                len--;
                uart_write_str(CONSOLE_UART, "\b \b");
            }
            continue;
        }
        if (len < LINE_BUF_SIZE - 1) {
            buf[len++] = (char)byte;
            uart_write_byte(CONSOLE_UART, byte); /* local echo */
        }
    }
    return 0;
}

static uint32_t parse_uint(const char *s) {
    uint32_t v = 0;
    while (*s >= '0' && *s <= '9') v = v * 10 + (uint32_t)(*s++ - '0');
    return v;
}

void Console_Task_Init(int heartbeat_id) {
    heartbeat_task_id = heartbeat_id;
    uart_init(CONSOLE_UART, CONSOLE_BAUD);
    uart_write_str(CONSOLE_UART,
        "\r\nEkoSonda console ready. \r\n");
}

void Console_Set_Status_Task(int task_id) {
    status_task_id = task_id;
}

void Console_Task(void) {
    uint8_t byte;

    if (state == CONSOLE_IDLE) {
        /* Ignore everything except the mode-entry key so stray bytes (or
         * the periodic status output itself) can never be mistaken for
         * the start of a value. */
        while (uart_read_byte(CONSOLE_UART, &byte)) {
            if (byte == 'S' || byte == 's') {
                state = CONSOLE_EDITING;
                if (status_task_id >= 0) SCH_Pause_Task(status_task_id);
                uart_write_str(CONSOLE_UART, "\r\nEnter blink interval (ms): ");
                break;
            }
            if (byte == 'G' || byte == 'g') {
                const gps_fix_t *fix = gps_get_fix();
                uart_write_str(CONSOLE_UART, "\r\nGPS: fix=");
                uart_write_str(CONSOLE_UART, fix->fix_valid ? "yes" : "no");
                uart_write_str(CONSOLE_UART, "  sats=");
                uart_write_uint(CONSOLE_UART, fix->num_sats);
                uart_write_str(CONSOLE_UART, "  lat_e7=");
                uart_write_int(CONSOLE_UART, fix->lat_e7);
                uart_write_str(CONSOLE_UART, "  lon_e7=");
                uart_write_int(CONSOLE_UART, fix->lon_e7);
                uart_write_str(CONSOLE_UART, "  utc=");
                uart_write_uint(CONSOLE_UART, fix->utc_time);
                uart_write_str(CONSOLE_UART, "\r\n");
            }
        }
        return;
    }

    char line[LINE_BUF_SIZE];
    if (!console_poll_line(line, sizeof(line))) return;

    uint32_t v = parse_uint(line);
    if (v > 0) {
        SCH_Set_Period(heartbeat_task_id, v);
        uart_write_str(CONSOLE_UART, "OK, blink interval = ");
        uart_write_uint(CONSOLE_UART, v);
        uart_write_str(CONSOLE_UART, " ms\r\n");
    } else {
        uart_write_str(CONSOLE_UART, "?\r\n");
    }

    state = CONSOLE_IDLE;
    if (status_task_id >= 0) SCH_Resume_Task(status_task_id);
}
