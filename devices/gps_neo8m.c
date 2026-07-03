#include "gps_neo8m.h"
#include "uart.h"

#define NMEA_MAX_LEN 96

static char line_buf[NMEA_MAX_LEN];
static uint32_t line_len = 0;
static gps_fix_t fix = {0};

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/* Returns the token starting at *cursor, NUL-terminating it at the next
 * comma (or '*'/end of sentence) and advancing *cursor past it. */
static char *next_field(char **cursor) {
    char *start = *cursor;
    char *p = start;
    while (*p && *p != ',' && *p != '*') p++;
    if (*p == ',') {
        *p = '\0';
        *cursor = p + 1;
    } else {
        *cursor = p; /* stop at '*' (checksum) or end of string */
    }
    return start;
}

static uint32_t parse_udec(const char *s) {
    uint32_t v = 0;
    while (*s >= '0' && *s <= '9') v = v * 10 + (uint32_t)(*s++ - '0');
    return v;
}

/* Converts an NMEA "ddmm.mmmm" / "dddmm.mmmm" field plus a hemisphere
 * char ('N'/'S'/'E'/'W') into signed 1e-7-degree fixed point. Integer
 * arithmetic only: decimal_degrees = degrees + minutes/60, computed by
 * treating the field's digits as one scaled integer. */
static int32_t parse_coord_e7(const char *field, char hemi) {
    if (field[0] == '\0') return 0;

    const char *dot = field;
    while (*dot && *dot != '.') dot++;

    int64_t scale = 1;
    uint64_t raw = 0;
    for (const char *p = field; *p; p++) {
        if (*p == '.') continue;
        raw = raw * 10 + (uint32_t)(*p - '0');
        if (p > dot) scale *= 10;
    }

    int64_t degrees = (int64_t)(raw / (100ULL * (uint64_t)scale));
    int64_t minutes_scaled = (int64_t)raw - degrees * 100 * scale;

    int64_t deg_e7 = degrees * 10000000LL +
                      (minutes_scaled * 10000000LL) / (60LL * scale);

    if (hemi == 'S' || hemi == 'W') deg_e7 = -deg_e7;
    return (int32_t)deg_e7;
}

static void handle_rmc(char *cursor) {
    char *utc = next_field(&cursor);
    char *status = next_field(&cursor);
    char *lat = next_field(&cursor);
    char *ns = next_field(&cursor);
    char *lon = next_field(&cursor);
    char *ew = next_field(&cursor);
    next_field(&cursor); /* speed over ground, unused */
    next_field(&cursor); /* track angle, unused */
    char *date = next_field(&cursor);

    fix.fix_valid = (status[0] == 'A') ? 1 : 0;
    fix.utc_time = parse_udec(utc);
    fix.utc_date = parse_udec(date);
    if (fix.fix_valid) {
        fix.lat_e7 = parse_coord_e7(lat, ns[0]);
        fix.lon_e7 = parse_coord_e7(lon, ew[0]);
    }
}

static void handle_gga(char *cursor) {
    next_field(&cursor); /* utc time, already have it from RMC */
    next_field(&cursor); /* lat, already have it from RMC */
    next_field(&cursor); /* N/S */
    next_field(&cursor); /* lon, already have it from RMC */
    next_field(&cursor); /* E/W */
    char *quality = next_field(&cursor);
    char *sats = next_field(&cursor);

    fix.num_sats = (uint8_t)parse_udec(sats);
    if (quality[0] == '0') fix.fix_valid = 0;
}

static uint8_t checksum_ok(const char *sentence) {
    if (sentence[0] != '$') return 0;

    const char *p = sentence + 1;
    uint8_t sum = 0;
    while (*p && *p != '*') sum ^= (uint8_t)*p++;
    if (*p != '*') return 0; /* no checksum present */

    int hi = hex_digit(p[1]);
    int lo = hex_digit(p[2]);
    if (hi < 0 || lo < 0) return 0;

    return sum == (uint8_t)((hi << 4) | lo);
}

static void handle_sentence(char *sentence) {
    if (!checksum_ok(sentence)) return;

    char *cursor = sentence;
    char *tag = next_field(&cursor); /* e.g. "$GNRMC" */
    uint32_t tlen = 0;
    while (tag[tlen]) tlen++;
    if (tlen < 3) return;

    const char *suffix = tag + tlen - 3;
    if (suffix[0] == 'R' && suffix[1] == 'M' && suffix[2] == 'C') {
        handle_rmc(cursor);
    } else if (suffix[0] == 'G' && suffix[1] == 'G' && suffix[2] == 'A') {
        handle_gga(cursor);
    }
}

void gps_init(USART_TypeDef *uart, uint32_t baud) {
    uart_init(uart, baud);
    line_len = 0;
}

void gps_poll(USART_TypeDef *uart) {
    uint8_t byte;
    while (uart_read_byte(uart, &byte)) {
        if (byte == '\r') continue;
        if (byte == '\n') {
            if (line_len > 0) {
                line_buf[line_len] = '\0';
                handle_sentence(line_buf);
            }
            line_len = 0;
            continue;
        }
        if (line_len < NMEA_MAX_LEN - 1) {
            line_buf[line_len++] = (char)byte;
        } else {
            line_len = 0; /* overflow: drop and resync on the next line */
        }
    }
}

const gps_fix_t *gps_get_fix(void) {
    return &fix;
}
