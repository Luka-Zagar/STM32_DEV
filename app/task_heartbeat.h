#ifndef TASK_HEARTBEAT_H
#define TASK_HEARTBEAT_H

/* Configures PA5 (LD2, on-board user LED) and PB0 (external heartbeat
 * LED) as outputs. Call once before registering Heartbeat_Task with the
 * scheduler. */
void Heartbeat_Task_Init(void);

/* Register with the scheduler at the desired period (500ms = 500ms ON,
 * 500ms OFF). Toggles both LEDs together once per call. */
void Heartbeat_Task(void);

#endif /* TASK_HEARTBEAT_H */
