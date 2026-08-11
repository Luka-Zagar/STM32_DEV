#include "record.h"

static void write_2digit(strbuf_t *sb, uint32_t v) {
    strbuf_char(sb, (char)('0' + (v / 10) % 10));
    strbuf_char(sb, (char)('0' + v % 10));
}

void record_csv_header(strbuf_t *sb) {
    strbuf_str(sb, "timestamp,local_time,local_date,fix_type,satellites,"
                   "latitude,longitude,altitude,speed,heading,"
                   "accel_x,accel_y,accel_z,gyro_x,gyro_y,gyro_z,imu_temp,"
                   "mag_x,mag_y,mag_z,"
                   "battery_state,battery_voltage,battery_current,soc,time_left");
}

void record_to_csv_row(strbuf_t *sb, const record_t *rec) {
    strbuf_uint(sb, rec->timestamp);
    strbuf_char(sb, ',');

    write_2digit(sb, rec->local_hour);
    strbuf_char(sb, ':');
    write_2digit(sb, rec->local_min);
    strbuf_char(sb, ':');
    write_2digit(sb, rec->local_sec);
    strbuf_char(sb, ',');

    write_2digit(sb, rec->local_day);
    strbuf_char(sb, '.');
    write_2digit(sb, rec->local_month);
    strbuf_char(sb, '.');
    strbuf_uint(sb, rec->local_year);
    strbuf_char(sb, ',');

    strbuf_uint(sb, rec->fix_type);
    strbuf_char(sb, ',');
    strbuf_uint(sb, rec->num_sats);
    strbuf_char(sb, ',');

    strbuf_fixed(sb, rec->lat_e7, 7);
    strbuf_char(sb, ',');
    strbuf_fixed(sb, rec->lon_e7, 7);
    strbuf_char(sb, ',');
    strbuf_fixed(sb, rec->alt_dm, 1);
    strbuf_char(sb, ',');
    strbuf_fixed(sb, rec->speed_kmh_x10, 1);
    strbuf_char(sb, ',');
    strbuf_fixed(sb, rec->heading_x10, 1);
    strbuf_char(sb, ',');

    strbuf_int(sb, rec->accel_x);
    strbuf_char(sb, ',');
    strbuf_int(sb, rec->accel_y);
    strbuf_char(sb, ',');
    strbuf_int(sb, rec->accel_z);
    strbuf_char(sb, ',');
    strbuf_fixed(sb, rec->gyro_x, 1);
    strbuf_char(sb, ',');
    strbuf_fixed(sb, rec->gyro_y, 1);
    strbuf_char(sb, ',');
    strbuf_fixed(sb, rec->gyro_z, 1);
    strbuf_char(sb, ',');
    strbuf_fixed(sb, rec->imu_temp_c100, 2);
    strbuf_char(sb, ',');
    strbuf_fixed(sb, rec->mag_x, 1);
    strbuf_char(sb, ',');
    strbuf_fixed(sb, rec->mag_y, 1);
    strbuf_char(sb, ',');
    strbuf_fixed(sb, rec->mag_z, 1);
    strbuf_char(sb, ',');

    if (rec->ibat_ma < 0) strbuf_str(sb, "Charging");
    else if (rec->ibat_ma > 0) strbuf_str(sb, "Discharging");
    else strbuf_str(sb, "Idle");
    strbuf_char(sb, ',');

    strbuf_fixed(sb, rec->vbat_mv, 3);
    strbuf_char(sb, ',');
    strbuf_fixed(sb, rec->ibat_ma, 3);
    strbuf_char(sb, ',');
    strbuf_uint(sb, rec->soc_pct);
    strbuf_char(sb, ',');

    strbuf_uint(sb, rec->battery_minutes_left / 60);
    strbuf_str(sb, "h ");
    write_2digit(sb, rec->battery_minutes_left % 60);
    strbuf_str(sb, "min");
}
