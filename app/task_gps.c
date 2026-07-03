#include "task_gps.h"
#include "stm32g474xx.h"
#include "gps_neo8m.h"
#include "rtc.h"

#define GPS_UART USART1
#define GPS_BAUD 9600

static int rtc_synced = 0;

void Task_GPS_Init(void) {
    gps_init(GPS_UART, GPS_BAUD);
    rtc_init();
}

void Task_GPS(void) {
    gps_poll(GPS_UART);

    if (!rtc_synced) {
        const gps_fix_t *fix = gps_get_fix();
        if (fix->fix_valid && fix->utc_date != 0) {
            rtc_datetime_t dt;
            dt.hour = (uint8_t)(fix->utc_time / 10000);
            dt.min = (uint8_t)((fix->utc_time / 100) % 100);
            dt.sec = (uint8_t)(fix->utc_time % 100);
            dt.day = (uint8_t)(fix->utc_date / 10000);
            dt.month = (uint8_t)((fix->utc_date / 100) % 100);
            dt.year = (uint16_t)(2000 + (fix->utc_date % 100));

            rtc_set_datetime(&dt);
            rtc_synced = 1;
        }
    }
}

int Task_GPS_RTC_Synced(void) {
    return rtc_synced;
}
