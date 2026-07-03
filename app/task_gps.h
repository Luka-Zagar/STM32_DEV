#ifndef TASK_GPS_H
#define TASK_GPS_H

/* Initializes USART1 for the GPS module (NEO-8M, PC4/PC5, 9600 baud) and
 * the RTC (synced from this GPS data once a valid fix arrives). */
void Task_GPS_Init(void);

/* Register with the scheduler at a short period (e.g. 10ms) so incoming
 * NMEA bytes get drained promptly. Non-blocking. Also syncs the RTC from
 * the GPS's UTC time on the first valid fix seen. */
void Task_GPS(void);

/* Has the RTC been set from a GPS fix yet this boot? */
int Task_GPS_RTC_Synced(void);

#endif /* TASK_GPS_H */
