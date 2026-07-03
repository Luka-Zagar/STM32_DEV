#include "task_status.h"
#include "stm32g474xx.h"
#include "uart.h"
#include "scheduler.h"
#include "gps_neo8m.h"

#define STATUS_UART USART2

static int heartbeat_id = -1;
static int console_id = -1;

void Task_Status_Init(int heartbeat_task_id, int console_task_id) {
    heartbeat_id = heartbeat_task_id;
    console_id = console_task_id;
}

static void print_task_line(const char *name, int task_id) {
    uart_write_str(STATUS_UART, name);
    uart_write_str(STATUS_UART, ": interval=");
    uart_write_uint(STATUS_UART, SCH_Get_Period(task_id));
    uart_write_str(STATUS_UART, "ms  ran=");
    uart_write_uint(STATUS_UART, SCH_Get_Run_Count(task_id));
    uart_write_str(STATUS_UART, " times  last=");
    uart_write_uint(STATUS_UART, SCH_Get_Last_Duration_us(task_id));
    uart_write_str(STATUS_UART, "us/run\r\n");
}

static void print_gps_line(void) {
    const gps_fix_t *fix = gps_get_fix();
    uart_write_str(STATUS_UART, "GPS: fix=");
    uart_write_str(STATUS_UART, fix->fix_valid ? "yes" : "no");
    uart_write_str(STATUS_UART, "  sats=");
    uart_write_uint(STATUS_UART, fix->num_sats);
    uart_write_str(STATUS_UART, "  lat_e7=");
    uart_write_int(STATUS_UART, fix->lat_e7);
    uart_write_str(STATUS_UART, "  lon_e7=");
    uart_write_int(STATUS_UART, fix->lon_e7);
    uart_write_str(STATUS_UART, "\r\n");
}

void Task_Status(void) {
    uart_write_str(STATUS_UART, "\n\r---------------------------------------------------------------\r");
    uart_write_str(STATUS_UART, "\n\rEkoSonda RTOS Running manager \r\n");
    uart_write_str(STATUS_UART, "\r * Press 'S' to set the blink interval \r\n");
    uart_write_str(STATUS_UART, "\r * Press 'G' for the current GPS fix \r\n\n");
    print_task_line("Task 1 (Heartbeat)", heartbeat_id);
    print_task_line("Task 2 (Console)", console_id);
    print_gps_line();
}
