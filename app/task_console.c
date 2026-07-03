#include "task_console.h"
#include "stm32g474xx.h"
#include "systick.h"
#include "uart.h"
#include "scheduler.h"
#include "gps_neo8m.h"

#define CONSOLE_UART USART2
#define CONSOLE_BAUD 115200
#define LINE_BUF_SIZE 32
#define SUBMENU_REFRESH_MS 1000

/* CEST = UTC+2. No RTC/DST logic yet (that's the next build step), so
 * this is a fixed offset for display purposes only - not a real
 * timezone conversion. */
#define LOCAL_TZ_OFFSET_HOURS 2

typedef enum { CONSOLE_IDLE, CONSOLE_LED_MENU, CONSOLE_GPS_MENU } console_state_t;

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

/* utc_hhmmss is HHMMSS as decimal (e.g. 91114 = 09:11:14). Applies the
 * fixed CEST offset above; does not roll the date over midnight. */
static void print_local_time(uint32_t utc_hhmmss) {
    uint32_t hh = (utc_hhmmss / 10000 + LOCAL_TZ_OFFSET_HOURS) % 24;
    uint32_t mm = (utc_hhmmss / 100) % 100;
    uint32_t ss = utc_hhmmss % 100;
    write_2digit(hh);
    uart_write_byte(CONSOLE_UART, ':');
    write_2digit(mm);
    uart_write_byte(CONSOLE_UART, ':');
    write_2digit(ss);
}

/* utc_ddmmyy is DDMMYY as decimal, printed as DD.MM.20YY. */
static void print_date(uint32_t utc_ddmmyy) {
    uint32_t dd = utc_ddmmyy / 10000;
    uint32_t mm = (utc_ddmmyy / 100) % 100;
    uint32_t yy = utc_ddmmyy % 100;
    write_2digit(dd);
    uart_write_byte(CONSOLE_UART, '.');
    write_2digit(mm);
    uart_write_str(CONSOLE_UART, ".20");
    write_2digit(yy);
}

static void print_led_data(void) {
    uart_write_str(CONSOLE_UART, "Heartbeat: interval=");
    uart_write_uint(CONSOLE_UART, SCH_Get_Period(heartbeat_task_id));
    uart_write_str(CONSOLE_UART, "ms  ran=");
    uart_write_uint(CONSOLE_UART, SCH_Get_Run_Count(heartbeat_task_id));
    uart_write_str(CONSOLE_UART, " times  last=");
    uart_write_uint(CONSOLE_UART, SCH_Get_Last_Duration_us(heartbeat_task_id));
    uart_write_str(CONSOLE_UART, "us/run\r\n");
}

static void print_gps_dashboard(void) {
    const gps_fix_t *fix = gps_get_fix();
    uint32_t in_view = (uint32_t)fix->sats_in_view[GPS_CONST_GPS] +
                       fix->sats_in_view[GPS_CONST_GLONASS] +
                       fix->sats_in_view[GPS_CONST_GALILEO] +
                       fix->sats_in_view[GPS_CONST_BEIDOU];

    uart_write_str(CONSOLE_UART, "\r\n=== EkoSonda GPS Dashboard ===\r\n");

    uart_write_str(CONSOLE_UART, "Local Time: ");
    print_local_time(fix->utc_time);
    uart_write_str(CONSOLE_UART, " (CEST, UTC+2 assumed - no RTC yet)\r\n");

    uart_write_str(CONSOLE_UART, "Local Date: ");
    print_date(fix->utc_date);
    uart_write_str(CONSOLE_UART, "\r\n------------------------------------\r\n");

    uart_write_str(CONSOLE_UART, "FIX:       ");
    uart_write_str(CONSOLE_UART, fix->fix_type == 3 ? "3D FIX" :
                                  fix->fix_type == 2 ? "2D FIX" : "NO FIX");
    uart_write_str(CONSOLE_UART, "\r\n");

    uart_write_str(CONSOLE_UART, "Antenna:   ");
    switch (fix->antenna_status) {
        case GPS_ANTENNA_OK:    uart_write_str(CONSOLE_UART, "OK"); break;
        case GPS_ANTENNA_OPEN:  uart_write_str(CONSOLE_UART, "OPEN (check wiring!)"); break;
        case GPS_ANTENNA_SHORT: uart_write_str(CONSOLE_UART, "SHORT (check wiring!)"); break;
        default:                uart_write_str(CONSOLE_UART, "unknown"); break;
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
    }
}

static void run_led_menu(void) {
    uint8_t byte;
    while (uart_read_byte(CONSOLE_UART, &byte)) {
        if (byte == 'L' || byte == 'l') {
            exit_submenu();
            return;
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

void Console_Task(void) {
    switch (state) {
        case CONSOLE_IDLE: run_idle(); break;
        case CONSOLE_LED_MENU: run_led_menu(); break;
        case CONSOLE_GPS_MENU: run_gps_menu(); break;
    }
}
