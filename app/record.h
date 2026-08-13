#ifndef RECORD_H
#define RECORD_H

#include <stdint.h>
#include "strbuf.h"

/* The one timestamped record everything flows through (see project
 * brief). record_t itself stays fixed-point-int only, per the project's
 * "no floats in the pipeline" rule - lat_e7/vbat_mv/etc are still raw
 * scaled integers here. The human-readable formatting (decimal degrees,
 * "HH:MM:SS", "3.384 V") happens only at the very last step, inside
 * record_to_csv_row() itself, via strbuf_fixed()'s pure-integer
 * decimal-point formatting - still no floats anywhere, just text
 * rendering moved from "on the PC" to "at CSV-write time" for
 * readability, per explicit request.
 *
 * Only fields with a real sensor behind them right now - GPS, IMU
 * (accel), and battery (INA3221). This is the prototype's full sensor
 * set - BME280/ADC are explicitly out of scope for this build; WiFi/MQTT
 * status is next but doesn't need a record_t field (it's an upload
 * concern, not a logged measurement). */
typedef struct {
    uint32_t timestamp;      /* unix time (UTC), from the RTC */

    /* Local time/date - same RTC reading as timestamp, just also kept
     * broken down and shifted by the fixed CEST (UTC+2) display offset
     * already used in task_console.c - not real timezone/DST handling,
     * and does not roll the date over a midnight crossing, same
     * accepted limitation as the console's own local-time display. */
    uint8_t  local_hour, local_min, local_sec;
    uint8_t  local_day, local_month;
    uint16_t local_year;

    /* GPS (devices/gps_neo8m.c) */
    uint8_t  fix_type;       /* 1=no fix, 2=2D, 3=3D */
    uint8_t  num_sats;       /* satellites used in fix */
    int32_t  lat_e7, lon_e7; /* 1e-7 deg, signed (not N/S/E/W suffixed) - standard GIS convention */
    int32_t  alt_dm;         /* altitude, tenths of a meter (MSL) */
    int32_t  speed_kmh_x10;
    int32_t  heading_x10;

    /* IMU (devices/mpu9250.c) - all 0 if not found/not read yet */
    int16_t  accel_x, accel_y, accel_z;    /* milli-g */
    int16_t  gyro_x, gyro_y, gyro_z;       /* degrees/sec x10 */
    int16_t  imu_temp_c100;                /* chip's own die temp, deg C x100 - not ambient air */
    int16_t  mag_x, mag_y, mag_z;          /* microtesla x10 - 0,0,0 if this chip has no magnetometer (e.g. MPU6500) */

    /* Battery (devices/ina3221.c, channel 3) */
    uint16_t vbat_mv;
    int16_t  ibat_ma;        /* negative = charging, positive = discharging - see task_battery.c */
    uint8_t  soc_pct;        /* rough OCV estimate, see task_battery.c */
    uint16_t battery_minutes_left; /* time to full (charging) or empty (discharging) - rough, see task_battery.c */
} record_t;

/* One-time header row, matching record_to_csv_row()'s column order. */
void record_csv_header(strbuf_t *sb);

/* Appends one CSV row (no trailing newline) for rec into sb. */
void record_to_csv_row(strbuf_t *sb, const record_t *rec);

/* Appends one JSON object (no trailing newline) for rec into sb - same
 * fields as record_to_csv_row(), same single source of truth, just for
 * MQTT publishing (app/task_wifi.c) instead of the SD log. Field names
 * match what the dashboard already expects for fix/sats/lat/lon/alt_m/
 * speed_kmh (established before this function existed); the rest follow
 * the same short/lowercase convention. Worst-case length is 416 bytes
 * (computed by hand, see task_wifi.c's PUBLISH_PAYLOAD_CAP comment) -
 * same "measure it, don't guess" rule as the CSV buffer sizes below. */
void record_to_json(strbuf_t *sb, const record_t *rec);

#endif /* RECORD_H */
