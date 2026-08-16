#ifndef TASK_SD_H
#define TASK_SD_H

#include <stdint.h>

typedef enum {
    SD_STATE_NOT_MOUNTED, /* init/mount/self-test failed, ejected, or never run */
    SD_STATE_MOUNTED
} sd_state_t;

/* One-shot bring-up ritual, call once at boot (like the I2C WHO_AM_I
 * check or the ESP8266 AT+GMR probe): mounts the card, writes a small
 * self-test file, reads it back, and prints PASS/FAIL + free space to
 * the console. Not a scheduled task - SD access only happens here and
 * later from the logger task, never polled. */
void Task_SD_Init(void);

/* Periodic poll (register with the scheduler) - debounces the SD eject
 * button (PC9, see docs/pinout.md) and toggles between mounted/ejected
 * on each confirmed press:
 *   mounted -> pressed: syncs+closes the open log file
 *              (task_logger.c's Task_Logger_Prepare_For_Eject()) *then*
 *              unmounts, so the card is guaranteed not mid-write the
 *              moment it's physically pulled - this is what makes
 *              removal safe.
 *   ejected -> pressed: re-runs the mount + self-test ritual, and on
 *              success tells the logger to start a brand new session
 *              file (Task_Logger_Resume()) rather than reuse the old
 *              name. */
void Task_SD_Button(void);

sd_state_t Task_SD_Get_State(void);
const char *Task_SD_Status_Str(void);
uint32_t Task_SD_Free_KB(void); /* only meaningful while SD_STATE_MOUNTED */

#endif /* TASK_SD_H */
