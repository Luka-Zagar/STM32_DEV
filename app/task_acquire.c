#include "task_acquire.h"
#include "record.h"
#include "rtc.h"
#include "gps_neo8m.h"
#include "task_battery.h"
#include "task_imu.h"

/* CEST = UTC+2, same fixed display-time offset as task_console.c's
 * local-time display - not real timezone/DST handling, does not roll
 * the date over a midnight crossing. Kept in sync with that constant by
 * eye (both are tiny, unlikely to drift, and duplicating a #define
 * isn't worth a shared header here). */
#define LOCAL_TZ_OFFSET_HOURS 2

static ringbuf_t rb;

/* Separate from the ring buffer on purpose - the ring buffer is
 * task_logger's single-consumer queue (popping from it here too would
 * race the logger for the same entries); this is just "whatever the
 * latest tick built", for consumers that want the current snapshot
 * rather than the drained log stream (currently: task_wifi.c's MQTT
 * publish, via Task_Acquire_Last_Record()). */
static record_t last_record;

/* Howard Hinnant's days-from-civil algorithm - integer-only (no floats,
 * per project rule), proleptic Gregorian, correct for any year rtc.c can
 * represent. rtc.c deliberately stays in broken-down calendar fields
 * (see its header comment); this conversion to unix time belongs at the
 * app layer where the record is actually assembled. */
static uint32_t days_from_civil(int32_t y, uint32_t m, uint32_t d) {
    y -= (m <= 2) ? 1 : 0;
    int32_t era = (y >= 0 ? y : y - 399) / 400;
    uint32_t yoe = (uint32_t)(y - era * 400);              /* [0, 399] */
    uint32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1; /* [0, 365] */
    uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;   /* [0, 146096] */
    return (uint32_t)(era * 146097 + (int32_t)doe - 719468);
}

static uint32_t rtc_to_unix(const rtc_datetime_t *dt) {
    uint32_t days = days_from_civil(dt->year, dt->month, dt->day);
    return days * 86400UL + (uint32_t)dt->hour * 3600UL + (uint32_t)dt->min * 60UL + dt->sec;
}

void Task_Acquire_Init(void) {
    ringbuf_init(&rb);
}

void Task_Acquire(void) {
    record_t rec = {0};

    rtc_datetime_t dt;
    rtc_get_datetime(&dt);
    rec.timestamp = rtc_to_unix(&dt);

    rec.local_hour = (uint8_t)(((uint32_t)dt.hour + LOCAL_TZ_OFFSET_HOURS) % 24);
    rec.local_min = dt.min;
    rec.local_sec = dt.sec;
    rec.local_day = dt.day;
    rec.local_month = dt.month;
    rec.local_year = dt.year;

    const gps_fix_t *fix = gps_get_fix();
    rec.fix_type = fix->fix_type;
    rec.num_sats = fix->num_sats;
    rec.lat_e7 = fix->lat_e7;
    rec.lon_e7 = fix->lon_e7;
    rec.alt_dm = fix->alt_dm;
    rec.speed_kmh_x10 = fix->speed_kmh_x10;
    rec.heading_x10 = fix->heading_x10;

    if (Task_IMU_Get_State() == IMU_HW_OK) {
        const int16_t *accel = Task_IMU_Accel_mg();
        rec.accel_x = accel[0];
        rec.accel_y = accel[1];
        rec.accel_z = accel[2];

        const int16_t *gyro = Task_IMU_Gyro_dps_x10();
        rec.gyro_x = gyro[0];
        rec.gyro_y = gyro[1];
        rec.gyro_z = gyro[2];

        rec.imu_temp_c100 = Task_IMU_Temp_c100();

        if (Task_IMU_Has_Magnetometer()) {
            const int16_t *mag = Task_IMU_Mag_uT_x10();
            rec.mag_x = mag[0];
            rec.mag_y = mag[1];
            rec.mag_z = mag[2];
        }
    }

    if (Task_Battery_Get_State() == BATTERY_HW_OK) {
        rec.vbat_mv = Task_Battery_Vbat_mV();
        rec.ibat_ma = Task_Battery_Ibat_mA();
        rec.soc_pct = Task_Battery_Soc_Pct();
        rec.battery_minutes_left = (uint16_t)Task_Battery_Minutes_Left();
    }

    /* temp/hum/press/lux stay unset (no record_t field at all right now) -
     * BME280/ADC explicitly out of scope for this prototype, see record.h. */

    last_record = rec;
    ringbuf_push(&rb, &rec);
}

ringbuf_t *Task_Acquire_GetRingbuf(void) {
    return &rb;
}

const record_t *Task_Acquire_Last_Record(void) {
    return &last_record;
}
