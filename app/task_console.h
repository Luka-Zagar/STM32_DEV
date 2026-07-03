#ifndef TASK_CONSOLE_H
#define TASK_CONSOLE_H

/* Initializes the console UART and prints the banner. heartbeat_task_id
 * is the scheduler task id whose period gets retargeted when a valid
 * "<number>\r\n" line comes in. */
void Console_Task_Init(int heartbeat_task_id);

/* Register with the scheduler. Polls for a completed line and, if it
 * parses as a positive number, updates the heartbeat task's period. */
void Console_Task(void);

#endif /* TASK_CONSOLE_H */
