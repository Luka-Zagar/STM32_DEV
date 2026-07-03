#include "gps_neo8m.h"
#include "uart.h"

#define NMEA_MAX_LEN 96

static char line_buf[NMEA_MAX_LEN];
static uint32_t line_len = 0;
static gps_fix_t fix = {0};

/* Accumulated across one round of GSV sentences, finalized into
 * fix.avg_snr_x10 when the next GGA arrives (GGA leads a burst, GSV
 * sentences for the following round come after it). */
static uint32_t snr_sum = 0;
static uint32_t snr_count = 0;

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/* Returns the token starting at *cursor, NUL-terminating it at the next
 * comma (or '*'/end of sentence) and advancing *cursor past it. Stopping
 * at '*' also NUL-terminates in place (clobbering the checksum text,
 * already verified by checksum_ok() before any tokenizing starts) so
 * that a caller which reads one field too many - e.g. a GSV satellite
 * loop probing for a group that isn't there - gets an empty string
 * back forever, not literal checksum characters. */
static char *next_field(char **cursor) {
    char *start = *cursor;
    char *p = start;
    while (*p && *p != ',' && *p != '*') p++;
    char sep = *p;
    *p = '\0';
    *cursor = (sep == ',') ? p + 1 : p;
    return start;
}

static uint32_t parse_udec(const char *s) {
    uint32_t v = 0;
    while (*s >= '0' && *s <= '9') v = v * 10 + (uint32_t)(*s++ - '0');
    return v;
}

static int contains(const char *haystack, const char *needle) {
    for (const char *h = haystack; *h; h++) {
        const char *a = h;
        const char *b = needle;
        while (*a && *b && *a == *b) {
            a++;
            b++;
        }
        if (*b == '\0') return 1;
    }
    return 0;
}

/* Parses a plain decimal field like "12.34" or "0.00" into an integer
 * scaled by target_scale (e.g. 10 for tenths), using integer arithmetic
 * only. Handles a leading '-' (RMC/GGA/GSA fields here are never
 * negative, but this keeps it generically correct). */
static int32_t parse_decimal_scaled(const char *field, uint32_t target_scale) {
    if (field[0] == '\0') return 0;

    int neg = 0;
    if (*field == '-') {
        neg = 1;
        field++;
    }

    const char *dot = field;
    while (*dot && *dot != '.') dot++;

    int64_t scale = 1;
    int64_t raw = 0;
    for (const char *p = field; *p; p++) {
        if (*p == '.') continue;
        raw = raw * 10 + (*p - '0');
        if (p > dot) scale *= 10;
    }

    int64_t result = (raw * (int64_t)target_scale) / scale;
    return (int32_t)(neg ? -result : result);
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
    char *speed_knots = next_field(&cursor);
    char *track = next_field(&cursor);
    char *date = next_field(&cursor);

    fix.fix_valid = (status[0] == 'A') ? 1 : 0;
    fix.utc_time = parse_udec(utc);
    fix.utc_date = parse_udec(date);
    if (fix.fix_valid) {
        fix.lat_e7 = parse_coord_e7(lat, ns[0]);
        fix.lon_e7 = parse_coord_e7(lon, ew[0]);

        int32_t knots_x10 = parse_decimal_scaled(speed_knots, 10);
        fix.speed_kmh_x10 = (int32_t)(((int64_t)knots_x10 * 1852) / 1000);
        fix.heading_x10 = parse_decimal_scaled(track, 10);
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
    next_field(&cursor); /* HDOP, using GSA's combined value instead */
    char *alt = next_field(&cursor);

    fix.num_sats = (uint8_t)parse_udec(sats);
    fix.alt_dm = parse_decimal_scaled(alt, 10);
    if (quality[0] == '0') fix.fix_valid = 0;

    /* GGA leads each NMEA burst; the SNR accumulated since the last GGA
     * belongs to the round of GSV sentences that just finished. */
    if (snr_count > 0) {
        fix.avg_snr_x10 = (uint16_t)((snr_sum * 10) / snr_count);
    }
    snr_sum = 0;
    snr_count = 0;
}

static void handle_gsa(char *cursor) {
    next_field(&cursor); /* mode (M/A), unused */
    char *fix_type = next_field(&cursor);
    for (int i = 0; i < 12; i++) next_field(&cursor); /* satellite PRNs, unused */
    char *pdop = next_field(&cursor);
    char *hdop = next_field(&cursor);
    char *vdop = next_field(&cursor);

    fix.fix_type = (uint8_t)parse_udec(fix_type);
    fix.pdop_x10 = (uint16_t)parse_decimal_scaled(pdop, 10);
    fix.hdop_x10 = (uint16_t)parse_decimal_scaled(hdop, 10);
    fix.vdop_x10 = (uint16_t)parse_decimal_scaled(vdop, 10);
}

static void handle_txt(char *cursor) {
    next_field(&cursor); /* total messages, unused */
    next_field(&cursor); /* this message's number, unused */
    next_field(&cursor); /* severity code, unused */
    char *text = next_field(&cursor);

    /* u-blox antenna supervisor messages: "ANTENNA OPEN"/"SHORT"/"OK". */
    if (contains(text, "ANTENNA OPEN")) {
        fix.antenna_status = GPS_ANTENNA_OPEN;
    } else if (contains(text, "ANTENNA SHORT")) {
        fix.antenna_status = GPS_ANTENNA_SHORT;
    } else if (contains(text, "ANTENNA OK")) {
        fix.antenna_status = GPS_ANTENNA_OK;
    }
}

static gps_constellation_t talker_to_constellation(const char *tag) {
    /* tag looks like "$GPGSV", "$GLGSV", "$GAGSV", "$GBGSV"/"$BDGSV" */
    if (tag[1] == 'G' && tag[2] == 'P') return GPS_CONST_GPS;
    if (tag[1] == 'G' && tag[2] == 'L') return GPS_CONST_GLONASS;
    if (tag[1] == 'G' && tag[2] == 'A') return GPS_CONST_GALILEO;
    if ((tag[1] == 'B' && tag[2] == 'D') || (tag[1] == 'G' && tag[2] == 'B')) return GPS_CONST_BEIDOU;
    return GPS_CONST_COUNT; /* unknown */
}

static void handle_gsv(const char *tag, char *cursor) {
    next_field(&cursor); /* total messages in this GSV sequence, unused */
    next_field(&cursor); /* this message's number, unused */
    char *in_view = next_field(&cursor);

    gps_constellation_t c = talker_to_constellation(tag);
    if (c < GPS_CONST_COUNT) {
        fix.sats_in_view[c] = (uint8_t)parse_udec(in_view);
    }

    /* Up to 4 (PRN, elevation, azimuth, SNR) groups per message; stop at
     * the first missing PRN (last message of a sequence may have fewer). */
    for (int i = 0; i < 4; i++) {
        char *prn = next_field(&cursor);
        if (prn[0] == '\0') break;
        next_field(&cursor); /* elevation, unused */
        next_field(&cursor); /* azimuth, unused */
        char *snr = next_field(&cursor);
        if (snr[0] != '\0') {
            snr_sum += parse_udec(snr);
            snr_count++;
        }
    }
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
    } else if (suffix[0] == 'G' && suffix[1] == 'S' && suffix[2] == 'A') {
        handle_gsa(cursor);
    } else if (suffix[0] == 'G' && suffix[1] == 'S' && suffix[2] == 'V') {
        handle_gsv(tag, cursor);
    } else if (suffix[0] == 'T' && suffix[1] == 'X' && suffix[2] == 'T') {
        handle_txt(cursor);
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
