#ifndef TASK_GPS_H
#define TASK_GPS_H

#include <stdint.h>

/* Initializes USART1 for the GPS module (NEO-8M, PC4/PC5, 9600 baud) and
 * the RTC (synced from this GPS data once a valid fix arrives). */
void Task_GPS_Init(void);

/* Register with the scheduler at a short period (e.g. 10ms) so incoming
 * NMEA bytes get drained promptly. Non-blocking. Also syncs the RTC from
 * the GPS's UTC time on the first valid fix, then re-syncs periodically
 * (every RTC_RESYNC_INTERVAL_MS) whenever a fix is available, to bound
 * LSE crystal drift between syncs. */
void Task_GPS(void);

/* Has the RTC been set from a GPS fix at least once this boot? */
int Task_GPS_RTC_Synced(void);

/* Milliseconds since the last successful RTC resync. Meaningless (0) if
 * Task_GPS_RTC_Synced() is false. */
uint32_t Task_GPS_Ms_Since_Sync(void);

#endif /* TASK_GPS_H */
