#ifndef TASK_STATUS_H
#define TASK_STATUS_H

/* Registers which task ids to report on. Call once before registering
 * Task_Status with the scheduler. */
void Task_Status_Init(int heartbeat_task_id, int console_task_id, int gps_task_id, int wifi_task_id,
                       int acquire_task_id, int logger_task_id);

/* Prints period/run-count/last-duration for each tracked task, e.g.
 * registered every 3000ms for a periodic status line. */
void Task_Status(void);

#endif /* TASK_STATUS_H */
