#ifndef TASK_GPS_H
#define TASK_GPS_H

/* Initializes USART1 for the GPS module (NEO-8M, PC4/PC5, 9600 baud). */
void Task_GPS_Init(void);

/* Register with the scheduler at a short period (e.g. 10ms) so incoming
 * NMEA bytes get drained promptly. Non-blocking. */
void Task_GPS(void);

#endif /* TASK_GPS_H */
