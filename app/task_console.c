#include "task_console.h"
#include "stm32g474xx.h"
#include "systick.h"
#include "uart.h"
#include "scheduler.h"
#include "gps_neo8m.h"
#include "rtc.h"
#include "task_gps.h"
#include "task_wifi.h"
#include "esp8266.h"
#include "task_led.h"
#include "task_battery.h"
#include "task_sd.h"
#include "task_logger.h"
#include "task_imu.h"

#define CONSOLE_UART USART2
#define CONSOLE_BAUD 115200
#define LINE_BUF_SIZE 32
#define SUBMENU_REFRESH_MS 1000

/* CEST = UTC+2. The RTC itself keeps UTC (set from GPS); this is just a
 * fixed display-time offset, not real timezone/DST handling. */
#define LOCAL_TZ_OFFSET_HOURS 2

typedef enum { CONSOLE_IDLE, CONSOLE_LED_MENU, CONSOLE_GPS_MENU, CONSOLE_WIFI_MENU,
               CONSOLE_BATTERY_MENU, CONSOLE_SD_MENU, CONSOLE_IMU_MENU } console_state_t;

static int heartbeat_task_id = -1;
static int status_task_id = -1;
static console_state_t state = CONSOLE_IDLE;
static uint32_t last_refresh_ms = 0;

static char line_buf[LINE_BUF_SIZE];
static uint32_t line_len = 0;

static void line_reset(void) {
    line_len = 0;
}

/* Feeds one byte into the line buffer, echoing it and handling
 * backspace. Returns 1 and NUL-terminates `line` once Enter is pressed
 * on a non-empty line, 0 otherwise. */
static int console_feed_line_byte(uint8_t byte, char *line, uint32_t max_len) {
    if (byte == '\r' || byte == '\n') {
        uart_write_str(CONSOLE_UART, "\r\n");
        if (line_len == 0) return 0;
        line_buf[line_len] = '\0';
        for (uint32_t i = 0; i <= line_len && i < max_len; i++) line[i] = line_buf[i];
        line_len = 0;
        return 1;
    }
    if (byte == '\b' || byte == 0x7F) {
        if (line_len > 0) {
            line_len--;
            uart_write_str(CONSOLE_UART, "\b \b");
        }
        return 0;
    }
    if (line_len < LINE_BUF_SIZE - 1) {
        line_buf[line_len++] = (char)byte;
        uart_write_byte(CONSOLE_UART, byte); /* local echo */
    }
    return 0;
}

static uint32_t parse_uint(const char *s) {
    uint32_t v = 0;
    while (*s >= '0' && *s <= '9') v = v * 10 + (uint32_t)(*s++ - '0');
    return v;
}

static void write_2digit(uint32_t v) {
    if (v < 10) uart_write_byte(CONSOLE_UART, '0');
    uart_write_uint(CONSOLE_UART, v);
}

/* Applies the fixed CEST offset above; does not roll the date over
 * midnight if the offset crosses one. */
static void print_local_time(const rtc_datetime_t *dt) {
    uint32_t hh = ((uint32_t)dt->hour + LOCAL_TZ_OFFSET_HOURS) % 24;
    write_2digit(hh);
    uart_write_byte(CONSOLE_UART, ':');
    write_2digit(dt->min);
    uart_write_byte(CONSOLE_UART, ':');
    write_2digit(dt->sec);
}

static void print_date(const rtc_datetime_t *dt) {
    write_2digit(dt->day);
    uart_write_byte(CONSOLE_UART, '.');
    write_2digit(dt->month);
    uart_write_byte(CONSOLE_UART, '.');
    uart_write_uint(CONSOLE_UART, dt->year);
}

static const char *rtc_clock_source_str(void) {
    switch (rtc_get_clock_source()) {
        case RTC_CLK_LSE: return "LSE";
        case RTC_CLK_LSI: return "LSI, no LSE crystal detected";
        default: return "NONE - RTC not running";
    }
}

static void print_led_data(void) {
    uart_write_str(CONSOLE_UART, "White (heartbeat): interval=");
    uart_write_uint(CONSOLE_UART, SCH_Get_Period(heartbeat_task_id));
    uart_write_str(CONSOLE_UART, "ms  ran=");
    uart_write_uint(CONSOLE_UART, SCH_Get_Run_Count(heartbeat_task_id));
    uart_write_str(CONSOLE_UART, " times  last=");
    uart_write_uint(CONSOLE_UART, SCH_Get_Last_Duration_us(heartbeat_task_id));
    uart_write_str(CONSOLE_UART, "us/run\r\n");
    uart_write_str(CONSOLE_UART, "Green: ");
    uart_write_str(CONSOLE_UART, Task_LED_Green_Str());
    uart_write_str(CONSOLE_UART, "\r\n");
    uart_write_str(CONSOLE_UART, "Red:   ");
    uart_write_str(CONSOLE_UART, Task_LED_Red_Str());
    uart_write_str(CONSOLE_UART, "\r\n(press 'A' to acknowledge/clear latched red faults)\r\n");
}

static void print_gps_dashboard(void) {
    const gps_fix_t *fix = gps_get_fix();
    uint32_t in_view = (uint32_t)fix->sats_in_view[GPS_CONST_GPS] +
                       fix->sats_in_view[GPS_CONST_GLONASS] +
                       fix->sats_in_view[GPS_CONST_GALILEO] +
                       fix->sats_in_view[GPS_CONST_BEIDOU];

    rtc_datetime_t dt;
    rtc_get_datetime(&dt);

    uart_write_str(CONSOLE_UART, "\r\n=== EkoSonda GPS Dashboard ===\r\n");

    uart_write_str(CONSOLE_UART, "Local Time: ");
    print_local_time(&dt);
    uart_write_str(CONSOLE_UART, " (CEST, UTC+2 assumed)\r\n");

    uart_write_str(CONSOLE_UART, "Local Date: ");
    print_date(&dt);
    uart_write_str(CONSOLE_UART, "\r\n");

    uart_write_str(CONSOLE_UART, "RTC clock:  ");
    uart_write_str(CONSOLE_UART, rtc_clock_source_str());
    uart_write_str(CONSOLE_UART, ", ");
    if (Task_GPS_RTC_Synced()) {
        uart_write_str(CONSOLE_UART, "last synced ");
        uart_write_uint(CONSOLE_UART, Task_GPS_Ms_Since_Sync() / 1000);
        uart_write_str(CONSOLE_UART, "s ago");
    } else {
        uart_write_str(CONSOLE_UART, "not yet synced");
    }
    uart_write_str(CONSOLE_UART, "\r\n------------------------------------\r\n");

    uart_write_str(CONSOLE_UART, "FIX:       ");
    uart_write_str(CONSOLE_UART, fix->fix_type == 3 ? "3D FIX" :
                                  fix->fix_type == 2 ? "2D FIX" : "NO FIX");
    uart_write_str(CONSOLE_UART, "\r\n");

    uart_write_str(CONSOLE_UART, "Antenna:   ");
    switch (fix->antenna_status) {
        case GPS_ANTENNA_OK:
            uart_write_str(CONSOLE_UART, "OK");
            break;
        case GPS_ANTENNA_OPEN:
            uart_write_str(CONSOLE_UART, "OPEN");
            if (fix->fix_type >= 2) {
                /* A real open-antenna fault would starve the receiver of
                 * signal - it wouldn't hold a 2D/3D fix. Seeing both at
                 * once means the flag itself is the wrong signal here,
                 * a known false-positive on some NEO-8M clones. */
                uart_write_str(CONSOLE_UART,
                    " (likely false positive - clone NEO-8M, check wiring!)");
            } else {
                uart_write_str(CONSOLE_UART, " (check wiring!)");
            }
            break;
        case GPS_ANTENNA_SHORT:
            uart_write_str(CONSOLE_UART, "SHORT (check wiring!)");
            break;
        default:
            uart_write_str(CONSOLE_UART, "unknown");
            break;
    }
    uart_write_str(CONSOLE_UART, "\r\n");

    uart_write_str(CONSOLE_UART, "Sats:      ");
    uart_write_uint(CONSOLE_UART, fix->num_sats);
    uart_write_str(CONSOLE_UART, " Used (from ");
    uart_write_uint(CONSOLE_UART, in_view);
    uart_write_str(CONSOLE_UART, " in View)\r\n");

    uart_write_str(CONSOLE_UART, "CONSTELLATION:\r\n - GPS: ");
    uart_write_uint(CONSOLE_UART, fix->sats_in_view[GPS_CONST_GPS]);
    uart_write_str(CONSOLE_UART, "   - BeiDou: ");
    uart_write_uint(CONSOLE_UART, fix->sats_in_view[GPS_CONST_BEIDOU]);
    uart_write_str(CONSOLE_UART, "\r\n - Gal: ");
    uart_write_uint(CONSOLE_UART, fix->sats_in_view[GPS_CONST_GALILEO]);
    uart_write_str(CONSOLE_UART, "   - GLONASS: ");
    uart_write_uint(CONSOLE_UART, fix->sats_in_view[GPS_CONST_GLONASS]);
    uart_write_str(CONSOLE_UART, "\r\n");

    uart_write_str(CONSOLE_UART, "Signal:    ");
    uart_write_fixed(CONSOLE_UART, fix->avg_snr_x10, 1);
    uart_write_str(CONSOLE_UART, " dBHz (Avg SNR)\r\n------------------------------------\r\n");

    uart_write_str(CONSOLE_UART, "Latitude:  ");
    uart_write_fixed(CONSOLE_UART, fix->lat_e7 < 0 ? -fix->lat_e7 : fix->lat_e7, 7);
    uart_write_str(CONSOLE_UART, fix->lat_e7 < 0 ? " S\r\n" : " N\r\n");

    uart_write_str(CONSOLE_UART, "Longitude: ");
    uart_write_fixed(CONSOLE_UART, fix->lon_e7 < 0 ? -fix->lon_e7 : fix->lon_e7, 7);
    uart_write_str(CONSOLE_UART, fix->lon_e7 < 0 ? " W\r\n" : " E\r\n");

    uart_write_str(CONSOLE_UART, "Altitude:  ");
    uart_write_fixed(CONSOLE_UART, fix->alt_dm, 1);
    uart_write_str(CONSOLE_UART, " m (MSL)\r\n");

    uart_write_str(CONSOLE_UART, "EST. ERROR (Calc):\r\n - Horiz:  +/-");
    uart_write_fixed(CONSOLE_UART, (int32_t)fix->hdop_x10 * 25 / 10, 1);
    uart_write_str(CONSOLE_UART, " m\r\n - Vert:   +/-");
    uart_write_fixed(CONSOLE_UART, (int32_t)fix->vdop_x10 * 4, 1);
    uart_write_str(CONSOLE_UART, " m\r\n------------------------------------\r\n");

    uart_write_str(CONSOLE_UART, "Speed:     ");
    uart_write_fixed(CONSOLE_UART, fix->speed_kmh_x10, 1);
    uart_write_str(CONSOLE_UART, " km/h\r\n");

    uart_write_str(CONSOLE_UART, "Heading:   ");
    uart_write_fixed(CONSOLE_UART, fix->heading_x10, 1);
    uart_write_str(CONSOLE_UART, " deg\r\n------------------------------------\r\n");

    uart_write_str(CONSOLE_UART, "PDOP: ");
    uart_write_fixed(CONSOLE_UART, fix->pdop_x10, 1);
    uart_write_str(CONSOLE_UART, "  HDOP: ");
    uart_write_fixed(CONSOLE_UART, fix->hdop_x10, 1);
    uart_write_str(CONSOLE_UART, "  VDOP: ");
    uart_write_fixed(CONSOLE_UART, fix->vdop_x10, 1);
    uart_write_str(CONSOLE_UART, "\r\n====================================\r\n");
}

static void enter_led_menu(void) {
    state = CONSOLE_LED_MENU;
    line_reset();
    if (status_task_id >= 0) SCH_Pause_Task(status_task_id);
    last_refresh_ms = SysTick_GetMillis();
    uart_write_str(CONSOLE_UART,
        "\r\n-- LED setup and data (press 'L' again to exit) --\r\n"
        "Type a number + Enter to set a new blink interval.\r\n");
    print_led_data();
}

static void enter_gps_menu(void) {
    state = CONSOLE_GPS_MENU;
    if (status_task_id >= 0) SCH_Pause_Task(status_task_id);
    last_refresh_ms = SysTick_GetMillis();
    uart_write_str(CONSOLE_UART, "\r\n-- GPS setup and data (press 'G' again to exit) --\r\n");
    print_gps_dashboard();
}

static void print_wifi_data(void) {
    uart_write_str(CONSOLE_UART, "ESP8266: ");
    uart_write_str(CONSOLE_UART, Task_WiFi_Status_Str());
    uart_write_str(CONSOLE_UART, "\r\n");
    uart_write_str(CONSOLE_UART, "Firmware (AT+GMR):\r\n");
    uart_write_str(CONSOLE_UART, Task_WiFi_Firmware_Str());
    uart_write_str(CONSOLE_UART, "\r\n");
    uart_write_str(CONSOLE_UART, "Published: ");
    uart_write_uint(CONSOLE_UART, Task_WiFi_Publish_Count());
    uart_write_str(CONSOLE_UART, " fixes\r\n");
    uart_write_str(CONSOLE_UART, "Last CONNACK: ");
    uart_write_str(CONSOLE_UART, Task_WiFi_Connack_Hex());
    uart_write_str(CONSOLE_UART, "\r\n");
    uart_write_str(CONSOLE_UART, "Last response:\r\n");
    uart_write_str(CONSOLE_UART, esp8266_debug_response());
    uart_write_str(CONSOLE_UART, "\r\n");
}

static void enter_wifi_menu(void) {
    state = CONSOLE_WIFI_MENU;
    if (status_task_id >= 0) SCH_Pause_Task(status_task_id);
    last_refresh_ms = SysTick_GetMillis();
    uart_write_str(CONSOLE_UART, "\r\n-- WiFi setup and data (press 'W' again to exit) --\r\n");
    print_wifi_data();
}

/* Wiring-dependent: on this pack/shunt orientation, negative current =
 * flowing INTO the pack (charging), positive = flowing OUT to the load
 * (discharging) - confirmed against a real charger (-777mA while
 * charging). If the shunt is ever rewired the other way round, these
 * labels swap. */
static const char *battery_state_str(int16_t ibat) {
    if (ibat < 0) return "Charging";
    if (ibat > 0) return "Discharging";
    return "Idle";
}

static void print_battery_data(void) {
    int hw_ok = (Task_Battery_Get_State() == BATTERY_HW_OK);
    int16_t ibat = hw_ok ? Task_Battery_Ibat_mA() : 0;

    uart_write_str(CONSOLE_UART, "\r\n=== EkoSonda LI-ION Battery Dashboard ===\r\n");
    uart_write_str(CONSOLE_UART, "Battery pack: 1S5P 3.7V 17.5Ah\r\n");
    uart_write_str(CONSOLE_UART, "Cells: INR18650MJ1 cells\r\n");
    uart_write_str(CONSOLE_UART, "BMS: 2MOS-YH10A\r\n");
    uart_write_str(CONSOLE_UART, "------------------------------------\r\n");

    uart_write_str(CONSOLE_UART, "State: ");
    uart_write_str(CONSOLE_UART, hw_ok ? battery_state_str(ibat) : "unknown");
    uart_write_str(CONSOLE_UART, "\r\n");
    uart_write_str(CONSOLE_UART, "INA3221: ");
    uart_write_str(CONSOLE_UART, hw_ok ? "OK" : "not found");
    uart_write_str(CONSOLE_UART, "\r\n------------------------------------\r\n");

    if (!hw_ok) {
        uart_write_str(CONSOLE_UART, "====================================\r\n");
        return;
    }

    uart_write_str(CONSOLE_UART, "Voltage: ");
    uart_write_fixed(CONSOLE_UART, Task_Battery_Vbat_mV(), 3); /* mV IS millivolts = volts with 3 implied decimals */
    uart_write_str(CONSOLE_UART, " V  (");
    uart_write_uint(CONSOLE_UART, Task_Battery_Vbat_mV());
    uart_write_str(CONSOLE_UART, " mV)\r\n");

    uart_write_str(CONSOLE_UART, "Current: ");
    uart_write_fixed(CONSOLE_UART, ibat, 3);
    uart_write_str(CONSOLE_UART, " A  (");
    uart_write_int(CONSOLE_UART, ibat);
    uart_write_str(CONSOLE_UART, " mA) \r\n------------------------------------\r\n");

    uart_write_str(CONSOLE_UART, "SOC (rough OCV estimate): ~");
    uart_write_uint(CONSOLE_UART, Task_Battery_Soc_Pct());
    uart_write_str(CONSOLE_UART, "%\r\n");

    uart_write_str(CONSOLE_UART, "Estimate time left: ");
    if (ibat == 0) {
        uart_write_str(CONSOLE_UART, "-- (idle, no current draw)\r\n");
    } else {
        uint32_t mins = Task_Battery_Minutes_Left();
        uart_write_uint(CONSOLE_UART, mins / 60);
        uart_write_str(CONSOLE_UART, "h ");
        uart_write_uint(CONSOLE_UART, mins % 60);
        uart_write_str(CONSOLE_UART, "min ");
        uart_write_str(CONSOLE_UART, ibat < 0 ? "to full\r\n" : "to empty\r\n");
    }
    uart_write_str(CONSOLE_UART, "====================================\r\n");
}

static void enter_battery_menu(void) {
    state = CONSOLE_BATTERY_MENU;
    if (status_task_id >= 0) SCH_Pause_Task(status_task_id);
    last_refresh_ms = SysTick_GetMillis();
    uart_write_str(CONSOLE_UART, "\r\n-- Battery setup and data (press 'B' again to exit) --\r\n");
    print_battery_data();
}

static void print_sd_data(void) {
    int mounted = (Task_SD_Get_State() == SD_STATE_MOUNTED);

    uart_write_str(CONSOLE_UART, "\r\n=== EkoSonda SD Card Dashboard ===\r\n");
    uart_write_str(CONSOLE_UART, "SD: ");
    uart_write_str(CONSOLE_UART, mounted ? "mounted OK" : "not mounted");
    uart_write_str(CONSOLE_UART, "\r\n");
    if (mounted) {
        uart_write_str(CONSOLE_UART, "Space: ");
        uart_write_uint(CONSOLE_UART, Task_SD_Free_KB());
        uart_write_str(CONSOLE_UART, " KB free\r\n");
    } else {
        uart_write_str(CONSOLE_UART, "Last status: ");
        uart_write_str(CONSOLE_UART, Task_SD_Status_Str());
        uart_write_str(CONSOLE_UART, "\r\n");
    }
    uart_write_str(CONSOLE_UART, "------------------------------------\r\n");

    uart_write_str(CONSOLE_UART, "Log file: ");
    uart_write_str(CONSOLE_UART, Task_Logger_Filename()[0] ? Task_Logger_Filename() : "(none)");
    uart_write_str(CONSOLE_UART, "\r\n");
    uart_write_str(CONSOLE_UART, "Rows written: ");
    uart_write_uint(CONSOLE_UART, Task_Logger_Rows_Written());
    uart_write_str(CONSOLE_UART, "\r\n");
    uart_write_str(CONSOLE_UART, "Write errors: ");
    uart_write_uint(CONSOLE_UART, Task_Logger_Write_Errors());
    uart_write_str(CONSOLE_UART, "\r\n------------------------------------\r\n");

    uart_write_str(CONSOLE_UART, "Card state: ");
    if (mounted) {
        uart_write_str(CONSOLE_UART, "MOUNTED - do not remove\r\n");
        uart_write_str(CONSOLE_UART, "(press the eject button, wait for this to say 'safe to remove', then pull the card)\r\n");
    } else {
        uart_write_str(CONSOLE_UART, "EJECTED - safe to remove\r\n");
        uart_write_str(CONSOLE_UART, "(press the eject button again after reinserting to remount)\r\n");
    }
    uart_write_str(CONSOLE_UART, "====================================\r\n");
}

static void enter_sd_menu(void) {
    state = CONSOLE_SD_MENU;
    if (status_task_id >= 0) SCH_Pause_Task(status_task_id);
    last_refresh_ms = SysTick_GetMillis();
    uart_write_str(CONSOLE_UART, "\r\n-- SD card setup and data (press 'D' again to exit) --\r\n");
    print_sd_data();
}

/* Pure-integer atan2 approximation (Rajan/Shima-style two-piece
 * linear-in-r formula; exact at every 45-degree point, <0.6 degree max
 * error elsewhere), returning signed centidegrees (degrees x100),
 * -18000..+18000. Originally used the hardware FPU's atan2f() for the
 * (now-removed) magnetic heading feature, but this toolchain's prebuilt
 * libm can't actually link it for this target (confirmed with a bare
 * 3-line standalone test: same "Unknown destination type (ARM/Thumb)"
 * relocation error - a toolchain/multilib problem, not a bug in this
 * code) - integer math it is, and it's still useful here for pitch/roll
 * from the accelerometer. */
static int32_t atan2_cdeg(int32_t y, int32_t x) {
    int32_t abs_y = y < 0 ? -y : y;
    if (x == 0 && abs_y == 0) return 0;

    int32_t angle_cdeg;
    if (x >= 0) {
        int32_t r = ((x - abs_y) * 10000) / (x + abs_y);
        angle_cdeg = 4500 - (4500 * r) / 10000;
    } else {
        int32_t r = ((x + abs_y) * 10000) / (abs_y - x);
        angle_cdeg = 13500 - (4500 * r) / 10000;
    }

    if (y < 0) angle_cdeg = -angle_cdeg;
    return angle_cdeg;
}

/* Integer square root (Newton's method) - needed for pitch, which
 * divides by the magnitude of the Y/Z accel plane. */
static uint32_t isqrt(uint32_t n) {
    if (n == 0) return 0;
    uint32_t x = n;
    uint32_t y = (x + 1) / 2;
    while (y < x) {
        x = y;
        y = (x + n / x) / 2;
    }
    return x;
}

static void print_imu_data(void) {
    int hw_ok = (Task_IMU_Get_State() == IMU_HW_OK);

    uart_write_str(CONSOLE_UART, "\r\n=== EkoSonda IMU Dashboard ===\r\n");
    uart_write_str(CONSOLE_UART, "Chip: ");
    uart_write_str(CONSOLE_UART, hw_ok ? Task_IMU_Chip_Str() : "not found");
    uart_write_str(CONSOLE_UART, "\r\n------------------------------------\r\n");

    if (!hw_ok) {
        uart_write_str(CONSOLE_UART, "====================================\r\n");
        return;
    }

    const int16_t *a = Task_IMU_Accel_mg();
    uart_write_str(CONSOLE_UART, "Accel X: ");
    uart_write_fixed(CONSOLE_UART, a[0], 3); /* mg IS milli-g = G with 3 implied decimals */
    uart_write_str(CONSOLE_UART, "G   Y: ");
    uart_write_fixed(CONSOLE_UART, a[1], 3);
    uart_write_str(CONSOLE_UART, "G   Z: ");
    uart_write_fixed(CONSOLE_UART, a[2], 3);
    uart_write_str(CONSOLE_UART, "G\r\n");
    uart_write_str(CONSOLE_UART, "(at rest, one axis should read ~1.000G - that's gravity)\r\n");
    uart_write_str(CONSOLE_UART, "------------------------------------\r\n");

    const int16_t *g = Task_IMU_Gyro_dps_x10();
    uart_write_str(CONSOLE_UART, "Gyro  X: ");
    uart_write_fixed(CONSOLE_UART, g[0], 1);
    uart_write_str(CONSOLE_UART, " dps   Y: ");
    uart_write_fixed(CONSOLE_UART, g[1], 1);
    uart_write_str(CONSOLE_UART, " dps   Z: ");
    uart_write_fixed(CONSOLE_UART, g[2], 1);
    uart_write_str(CONSOLE_UART, " dps\r\n");
    uart_write_str(CONSOLE_UART, "         X: ");
    /* rad/s = dps * pi/180; x1000'd for 3 decimals, coefficient 17453
     * approximates pi*100/18=17453.29... to 5 significant figures
     * (~0.0000167 relative error - not meaningful vs. sensor noise). */
    uart_write_fixed(CONSOLE_UART, (int32_t)g[0] * 17453 / 10000, 3);
    uart_write_str(CONSOLE_UART, " rad/s Y: ");
    uart_write_fixed(CONSOLE_UART, (int32_t)g[1] * 17453 / 10000, 3);
    uart_write_str(CONSOLE_UART, " rad/s Z: ");
    uart_write_fixed(CONSOLE_UART, (int32_t)g[2] * 17453 / 10000, 3);
    uart_write_str(CONSOLE_UART, " rad/s\r\n------------------------------------\r\n");

    /* Pitch/roll: standard tilt-from-gravity formulas, accelerometer
     * only - reliable and always valid (no reference drift). Yaw has no
     * gravity reference, so it comes from task_imu.c's gyro-Z
     * integration instead (relative, drifts - see its own comment). */
    int32_t roll_cdeg = atan2_cdeg(a[1], a[2]);
    int32_t pitch_cdeg = atan2_cdeg(-a[0], (int32_t)isqrt((uint32_t)((int32_t)a[1] * a[1] + (int32_t)a[2] * a[2])));
    int32_t yaw_cdeg = Task_IMU_Yaw_cdeg();

    uart_write_str(CONSOLE_UART, "Pitch: ");
    uart_write_fixed(CONSOLE_UART, pitch_cdeg, 2);
    uart_write_str(CONSOLE_UART, " deg   Roll: ");
    uart_write_fixed(CONSOLE_UART, roll_cdeg, 2);
    uart_write_str(CONSOLE_UART, " deg   Yaw: ");
    uart_write_fixed(CONSOLE_UART, yaw_cdeg, 2);
    uart_write_str(CONSOLE_UART, " deg\r\n");
    uart_write_str(CONSOLE_UART, "(pitch/roll from accel - reliable. yaw is gyro-integrated,\r\n");
    uart_write_str(CONSOLE_UART, " relative to boot, and drifts slowly even at rest)\r\n");
    uart_write_str(CONSOLE_UART, "------------------------------------\r\n");

    uart_write_str(CONSOLE_UART, "Die temp: ");
    uart_write_fixed(CONSOLE_UART, Task_IMU_Temp_c100(), 2);
    uart_write_str(CONSOLE_UART, " C (chip's own temp, not ambient air)\r\n------------------------------------\r\n");

    if (Task_IMU_Has_Magnetometer()) {
        const int16_t *m = Task_IMU_Mag_uT_x10();
        uart_write_str(CONSOLE_UART, "Mag   X: ");
        uart_write_fixed(CONSOLE_UART, m[0], 1);
        uart_write_str(CONSOLE_UART, " uT   Y: ");
        uart_write_fixed(CONSOLE_UART, m[1], 1);
        uart_write_str(CONSOLE_UART, " uT   Z: ");
        uart_write_fixed(CONSOLE_UART, m[2], 1);
        uart_write_str(CONSOLE_UART, " uT\r\n");
        uart_write_str(CONSOLE_UART, "(Earth's field is ~25-65uT depending on location)\r\n");
    } else {
        uart_write_str(CONSOLE_UART, "Mag: no magnetometer on this chip variant\r\n");
    }
    uart_write_str(CONSOLE_UART, "====================================\r\n");
}

static void enter_imu_menu(void) {
    state = CONSOLE_IMU_MENU;
    if (status_task_id >= 0) SCH_Pause_Task(status_task_id);
    last_refresh_ms = SysTick_GetMillis();
    uart_write_str(CONSOLE_UART, "\r\n-- IMU setup and data (press 'I' again to exit) --\r\n");
    print_imu_data();
}

static void exit_submenu(void) {
    state = CONSOLE_IDLE;
    if (status_task_id >= 0) SCH_Resume_Task(status_task_id);
    uart_write_str(CONSOLE_UART, "\r\n-- back to main menu --\r\n");
}

void Console_Task_Init(int heartbeat_id) {
    heartbeat_task_id = heartbeat_id;
    uart_init(CONSOLE_UART, CONSOLE_BAUD);
    uart_write_str(CONSOLE_UART, "\r\nEkoSonda console ready. \r\n");
}

void Console_Set_Status_Task(int task_id) {
    status_task_id = task_id;
}

static void run_idle(void) {
    uint8_t byte;
    /* Ignore everything except the mode-entry keys so stray bytes can
     * never be mistaken for the start of a value. */
    while (uart_read_byte(CONSOLE_UART, &byte)) {
        if (byte == 'L' || byte == 'l') {
            enter_led_menu();
            return;
        }
        if (byte == 'G' || byte == 'g') {
            enter_gps_menu();
            return;
        }
        if (byte == 'W' || byte == 'w') {
            enter_wifi_menu();
            return;
        }
        if (byte == 'B' || byte == 'b') {
            enter_battery_menu();
            return;
        }
        if (byte == 'D' || byte == 'd') {
            enter_sd_menu();
            return;
        }
        if (byte == 'I' || byte == 'i') {
            enter_imu_menu();
            return;
        }
    }
}

static void run_led_menu(void) {
    uint8_t byte;
    while (uart_read_byte(CONSOLE_UART, &byte)) {
        if (byte == 'L' || byte == 'l') {
            exit_submenu();
            return;
        }
        if (byte == 'A' || byte == 'a') {
            Task_LED_Ack_Faults();
            uart_write_str(CONSOLE_UART, "OK, red faults acked\r\n");
            print_led_data();
            last_refresh_ms = SysTick_GetMillis();
            continue;
        }
        char line[LINE_BUF_SIZE];
        if (console_feed_line_byte(byte, line, sizeof(line))) {
            uint32_t v = parse_uint(line);
            if (v > 0) {
                SCH_Set_Period(heartbeat_task_id, v);
                uart_write_str(CONSOLE_UART, "OK, blink interval = ");
                uart_write_uint(CONSOLE_UART, v);
                uart_write_str(CONSOLE_UART, " ms\r\n");
            } else {
                uart_write_str(CONSOLE_UART, "?\r\n");
            }
            print_led_data();
            last_refresh_ms = SysTick_GetMillis();
        }
    }

    uint32_t now = SysTick_GetMillis();
    if ((int32_t)(now - last_refresh_ms) >= SUBMENU_REFRESH_MS) {
        last_refresh_ms = now;
        print_led_data();
    }
}

static void run_gps_menu(void) {
    uint8_t byte;
    while (uart_read_byte(CONSOLE_UART, &byte)) {
        if (byte == 'G' || byte == 'g') {
            exit_submenu();
            return;
        }
        /* view-only: any other key is ignored */
    }

    uint32_t now = SysTick_GetMillis();
    if ((int32_t)(now - last_refresh_ms) >= SUBMENU_REFRESH_MS) {
        last_refresh_ms = now;
        print_gps_dashboard();
    }
}

static void run_wifi_menu(void) {
    uint8_t byte;
    while (uart_read_byte(CONSOLE_UART, &byte)) {
        if (byte == 'W' || byte == 'w') {
            exit_submenu();
            return;
        }
        /* view-only: any other key is ignored */
    }

    uint32_t now = SysTick_GetMillis();
    if ((int32_t)(now - last_refresh_ms) >= SUBMENU_REFRESH_MS) {
        last_refresh_ms = now;
        print_wifi_data();
    }
}

static void run_battery_menu(void) {
    uint8_t byte;
    while (uart_read_byte(CONSOLE_UART, &byte)) {
        if (byte == 'B' || byte == 'b') {
            exit_submenu();
            return;
        }
        /* view-only: any other key is ignored */
    }

    uint32_t now = SysTick_GetMillis();
    if ((int32_t)(now - last_refresh_ms) >= SUBMENU_REFRESH_MS) {
        last_refresh_ms = now;
        print_battery_data();
    }
}

static void run_sd_menu(void) {
    uint8_t byte;
    while (uart_read_byte(CONSOLE_UART, &byte)) {
        if (byte == 'D' || byte == 'd') {
            exit_submenu();
            return;
        }
        /* view-only: any other key is ignored */
    }

    uint32_t now = SysTick_GetMillis();
    if ((int32_t)(now - last_refresh_ms) >= SUBMENU_REFRESH_MS) {
        last_refresh_ms = now;
        print_sd_data();
    }
}

static void run_imu_menu(void) {
    uint8_t byte;
    while (uart_read_byte(CONSOLE_UART, &byte)) {
        if (byte == 'I' || byte == 'i') {
            exit_submenu();
            return;
        }
        /* view-only: any other key is ignored */
    }

    uint32_t now = SysTick_GetMillis();
    if ((int32_t)(now - last_refresh_ms) >= SUBMENU_REFRESH_MS) {
        last_refresh_ms = now;
        print_imu_data();
    }
}

void Console_Task(void) {
    switch (state) {
        case CONSOLE_IDLE: run_idle(); break;
        case CONSOLE_LED_MENU: run_led_menu(); break;
        case CONSOLE_GPS_MENU: run_gps_menu(); break;
        case CONSOLE_WIFI_MENU: run_wifi_menu(); break;
        case CONSOLE_BATTERY_MENU: run_battery_menu(); break;
        case CONSOLE_SD_MENU: run_sd_menu(); break;
        case CONSOLE_IMU_MENU: run_imu_menu(); break;
    }
}
