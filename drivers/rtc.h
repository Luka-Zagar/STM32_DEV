#ifndef RTC_H
#define RTC_H

#include <stdint.h>

typedef enum {
    RTC_CLK_NONE, /* neither LSE nor LSI came up - RTC is not running */
    RTC_CLK_LSE,  /* 32.768kHz crystal - accurate */
    RTC_CLK_LSI   /* internal ~32kHz RC - always available, ~1% accuracy */
} rtc_clock_source_t;

typedef struct {
    uint8_t hour, min, sec; /* UTC */
    uint8_t day, month;
    uint16_t year;          /* full year, e.g. 2026 */
} rtc_datetime_t;

/* Starts the RTC off LSE, falling back to LSI (with a bounded timeout,
 * never hangs) if no LSE crystal is present or it fails to start. Call
 * once at boot. Does not set a calendar value - see rtc_set_datetime(). */
void rtc_init(void);

/* Which oscillator actually ended up driving the RTC - lets the rest of
 * the system report/display it (e.g. to tell whether the LSE crystal is
 * actually populated on a given board). */
rtc_clock_source_t rtc_get_clock_source(void);

/* Sets the RTC's calendar. UTC in, UTC stored - timezone/DST conversion
 * is a display-layer concern, not the RTC's. */
void rtc_set_datetime(const rtc_datetime_t *dt);

/* Reads the current calendar time (UTC). */
void rtc_get_datetime(rtc_datetime_t *dt);

#endif /* RTC_H */
