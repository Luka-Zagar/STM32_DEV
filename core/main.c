#include "stm32g474xx.h"
#include "system_clock.h"
#include "systick.h"
#include "uart.h"
#include "scheduler.h"

#define LED_PIN 5 /* PA5 = LD2, on-board user LED (Nucleo-G474RE) */
#define CONSOLE_UART USART2
#define CONSOLE_BAUD 115200
#define LINE_BUF_SIZE 32

static int heartbeat_task_id;

static void led_init(void) {
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN;
    GPIOA->MODER &= ~(3UL << (LED_PIN * 2));
    GPIOA->MODER |= (1UL << (LED_PIN * 2));
}

static void Heartbeat_Task(void) {
    GPIOA->ODR ^= GPIO_ODR_5;
}

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

static void write_uint(uint32_t v) {
    char digits[10];
    int n = 0;
    do {
        digits[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (n > 0) uart_write_byte(CONSOLE_UART, (uint8_t)digits[--n]);
}

static void Console_Task(void) {
    char line[LINE_BUF_SIZE];
    if (!console_poll_line(line, sizeof(line))) return;

    uint32_t v = parse_uint(line);
    if (v > 0) {
        SCH_Set_Period(heartbeat_task_id, v);
        uart_write_str(CONSOLE_UART, "OK, blink interval = ");
        write_uint(v);
        uart_write_str(CONSOLE_UART, " ms\r\n> ");
    } else {
        uart_write_str(CONSOLE_UART, "?\r\n> ");
    }
}

int main(void) {
    led_init();
    SysTick_Init();
    uart_init(CONSOLE_UART, CONSOLE_BAUD);
    SCH_Init();

    uart_write_str(CONSOLE_UART,
        "\r\nEkoSonda console ready. Type a number + Enter to set the "
        "blink interval in ms.\r\n> ");

    heartbeat_task_id = SCH_Add_Task(Heartbeat_Task, 0, 500);
    SCH_Add_Task(Console_Task, 0, 10);

    while (1) {
        SCH_Dispatch();
    }
}
