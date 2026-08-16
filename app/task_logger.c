#include "task_logger.h"
#include "task_acquire.h"
#include "task_sd.h"
#include "task_gps.h"
#include "record.h"
#include "strbuf.h"
#include "rtc.h"
#include "ff.h"
#include "systick.h"

#define SYNC_INTERVAL_MS 5000UL
#define LOG_FILENAME_CAP 32

static FIL log_file;
static int file_open = 0;
static int halted = 0; /* set while the eject button has the card checked out */
static char log_filename[LOG_FILENAME_CAP];
static uint32_t last_sync_ms = 0;
static uint32_t rows_written = 0;
static uint32_t write_errors = 0;
static uint32_t dropped_no_sd = 0;

static void strbuf_2digit(strbuf_t *sb, uint32_t v) {
    strbuf_char(sb, (char)('0' + (v / 10) % 10));
    strbuf_char(sb, (char)('0' + v % 10));
}

/* Scans the card's root for existing BOOTnnnn.CSV files (the no-fix-yet
 * fallback name) and returns one past the highest number found, so a
 * fresh boot never collides with a previous session's file even without
 * a real clock yet. Starts from 1 (not 0) if none exist or the scan
 * itself fails - a failed scan shouldn't block logging entirely. */
static uint32_t next_boot_number(void) {
    DIR dir;
    FILINFO fno;
    uint32_t max_n = 0;

    if (f_opendir(&dir, "") != FR_OK) return 1;
    while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0] != '\0') {
        if (fno.fattrib & AM_DIR) continue;
        const char *name = fno.fname;
        if (name[0] == 'B' && name[1] == 'O' && name[2] == 'O' && name[3] == 'T') {
            uint32_t n = 0;
            const char *p = name + 4;
            while (*p >= '0' && *p <= '9') { n = n * 10 + (uint32_t)(*p - '0'); p++; }
            if (n > max_n) max_n = n;
        }
    }
    f_closedir(&dir);
    return max_n + 1;
}

/* One name per session, decided once when the file is actually created:
 * a real date/time (YYYYMMDD_HHMMSS.CSV) if GPS has disciplined the RTC
 * by now, otherwise a sequential BOOTnnnn.CSV found by scanning the
 * card - not renamed later if a fix shows up mid-session. */
static void build_log_filename(char *out, uint32_t cap) {
    strbuf_t sb;
    strbuf_init(&sb, out, cap);

    if (Task_GPS_RTC_Synced()) {
        rtc_datetime_t dt;
        rtc_get_datetime(&dt);
        strbuf_uint(&sb, dt.year);
        strbuf_2digit(&sb, dt.month);
        strbuf_2digit(&sb, dt.day);
        strbuf_char(&sb, '_');
        strbuf_2digit(&sb, dt.hour);
        strbuf_2digit(&sb, dt.min);
        strbuf_2digit(&sb, dt.sec);
        strbuf_str(&sb, ".CSV");
    } else {
        strbuf_str(&sb, "BOOT");
        uint32_t n = next_boot_number();
        if (n < 1000) strbuf_char(&sb, '0');
        if (n < 100) strbuf_char(&sb, '0');
        if (n < 10) strbuf_char(&sb, '0');
        strbuf_uint(&sb, n);
        strbuf_str(&sb, ".CSV");
    }
}

/* Creates this session's file (FA_CREATE_NEW - never silently appends
 * onto a stale file with a reused name, since the entire point of this
 * naming scheme is a fresh file every session) and writes the header
 * row. Lazy: only actually tried once SD reports mounted, since
 * Task_SD_Init() runs before this task starts polling. */
static int ensure_file_open(void) {
    if (file_open) return 1;
    if (halted) return 0; /* card checked out via the eject button */
    if (Task_SD_Get_State() != SD_STATE_MOUNTED) return 0;

    build_log_filename(log_filename, sizeof(log_filename));

    if (f_open(&log_file, log_filename, FA_WRITE | FA_CREATE_NEW) != FR_OK) {
        return 0;
    }

    /* 24 columns with today's names is ~227 bytes + CRLF. A previous,
     * narrower version of this header already silently overflowed a
     * too-small buffer once (strbuf_char() drops rather than overflows,
     * which merged the cut-off header directly into row 1 with no line
     * break) - 300 leaves real headroom for whatever columns come next,
     * checked by hand against the actual header string rather than
     * guessed this time. */
    char header_buf[300];
    strbuf_t sb;
    strbuf_init(&sb, header_buf, sizeof(header_buf));
    record_csv_header(&sb);
    strbuf_str(&sb, "\r\n");
    UINT bw;
    f_write(&log_file, header_buf, sb.len, &bw);

    file_open = 1;
    return 1;
}

void Task_Logger_Init(void) {
    file_open = 0;
    halted = 0;
    log_filename[0] = '\0';
    last_sync_ms = SysTick_GetMillis();
}

void Task_Logger(void) {
    if (!ensure_file_open()) {
        /* Nothing to do until SD shows up (or the eject button releases
         * it) - drain and drop rather than let the ring buffer fill up
         * and start rejecting new samples while GPS/RTC keep producing
         * them. */
        record_t rec;
        ringbuf_t *rb = Task_Acquire_GetRingbuf();
        while (ringbuf_pop(rb, &rec)) dropped_no_sd++;
        return;
    }

    ringbuf_t *rb = Task_Acquire_GetRingbuf();
    record_t rec;
    while (ringbuf_pop(rb, &rec)) {
        char row_buf[240]; /* worst realistic case (long negative values, "Discharging", "999h 59min") checked by hand at ~194 bytes - see header_buf's comment above */
        strbuf_t sb;
        strbuf_init(&sb, row_buf, sizeof(row_buf));
        record_to_csv_row(&sb, &rec);
        strbuf_str(&sb, "\r\n");

        UINT bw;
        FRESULT fr = f_write(&log_file, row_buf, sb.len, &bw);
        if (fr != FR_OK || bw != sb.len) {
            write_errors++;
        } else {
            rows_written++;
        }
    }

    uint32_t now = SysTick_GetMillis();
    if ((int32_t)(now - last_sync_ms) >= (int32_t)SYNC_INTERVAL_MS) {
        if (f_sync(&log_file) != FR_OK) write_errors++;
        last_sync_ms = now;
    }
}

void Task_Logger_Prepare_For_Eject(void) {
    if (file_open) {
        f_sync(&log_file);
        f_close(&log_file);
        file_open = 0;
    }
    halted = 1;
}

void Task_Logger_Resume(void) {
    halted = 0;
    file_open = 0; /* forces a brand new session file, not a reused name */
    log_filename[0] = '\0';
}

uint32_t Task_Logger_Rows_Written(void)   { return rows_written; }
uint32_t Task_Logger_Write_Errors(void)   { return write_errors; }
uint32_t Task_Logger_Dropped_No_SD(void)  { return dropped_no_sd; }
const char *Task_Logger_Filename(void)    { return log_filename; }
