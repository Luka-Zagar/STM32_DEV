#include "task_gps.h"
#include "stm32g474xx.h"
#include "systick.h"
#include "gps_neo8m.h"
#include "rtc.h"

#define GPS_UART USART1
#define GPS_BAUD 9600

/* Bounds LSE crystal drift accumulated between syncs (~1.2ms/minute for
 * a typical +/-20ppm crystal). Note this is not the dominant error term:
 * without a PPS pin, sync itself is only accurate to the NMEA sentence's
 * UART transmit/parse latency (tens of ms), which no resync interval
 * can improve on - that would need PPS -> EXTI, unavailable on this
 * GPS module (TX/RX only). */
#define RTC_RESYNC_INTERVAL_MS 60000UL

static int rtc_synced = 0;
static uint32_t last_sync_ms = 0;

void Task_GPS_Init(void) {
    gps_init(GPS_UART, GPS_BAUD);
    rtc_init();
}

void Task_GPS(void) {
    gps_poll(GPS_UART);

    const gps_fix_t *fix = gps_get_fix();
    if (!fix->fix_valid || fix->utc_date == 0) return;

    uint32_t now = SysTick_GetMillis();
    if (rtc_synced && (int32_t)(now - last_sync_ms) < (int32_t)RTC_RESYNC_INTERVAL_MS) {
        return;
    }

    rtc_datetime_t dt;
    dt.hour = (uint8_t)(fix->utc_time / 10000);
    dt.min = (uint8_t)((fix->utc_time / 100) % 100);
    dt.sec = (uint8_t)(fix->utc_time % 100);
    dt.day = (uint8_t)(fix->utc_date / 10000);
    dt.month = (uint8_t)((fix->utc_date / 100) % 100);
    dt.year = (uint16_t)(2000 + (fix->utc_date % 100));

    rtc_set_datetime(&dt);
    rtc_synced = 1;
    last_sync_ms = now;
}

int Task_GPS_RTC_Synced(void) {
    return rtc_synced;
}

uint32_t Task_GPS_Ms_Since_Sync(void) {
    if (!rtc_synced) return 0;
    return SysTick_GetMillis() - last_sync_ms;
}
