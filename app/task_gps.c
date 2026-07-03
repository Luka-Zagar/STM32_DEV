#include "task_gps.h"
#include "stm32g474xx.h"
#include "gps_neo8m.h"

#define GPS_UART USART1
#define GPS_BAUD 9600

void Task_GPS_Init(void) {
    gps_init(GPS_UART, GPS_BAUD);
}

void Task_GPS(void) {
    gps_poll(GPS_UART);
}
