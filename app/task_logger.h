#ifndef TASK_LOGGER_H
#define TASK_LOGGER_H

#include <stdint.h>

/* Drains task_acquire's ring buffer to SD as CSV, one row per record,
 * with a periodic f_sync() (see project brief's SD write-latency
 * gotcha - f_sync() bounds how much is lost if power drops mid-write,
 * without syncing on every single row, which would be far too slow).
 * Never blocks a producer: if the SD isn't mounted, or a write fails,
 * records are just dropped (counted, not retried) rather than backing
 * the ring buffer up.
 *
 * A new file is started every session (every boot, and every SD
 * re-mount after an eject - see task_sd.c's eject button) - named from
 * the RTC's date/time if GPS has disciplined it by the time the first
 * row is written, or a sequential BOOTnnnn.CSV fallback (found by
 * scanning the card) if not. The name is decided once, when the file is
 * actually created - it does not get renamed mid-session if a GPS fix
 * shows up later. */
void Task_Logger_Init(void);
void Task_Logger(void);

/* For the console dashboard and (later) the red-LED "SD write error"
 * state. */
uint32_t Task_Logger_Rows_Written(void);
uint32_t Task_Logger_Write_Errors(void);
uint32_t Task_Logger_Dropped_No_SD(void);
const char *Task_Logger_Filename(void); /* "" if no file open yet */

/* Called by task_sd.c's eject-button handler, in this order:
 * Task_Logger_Prepare_For_Eject() first (syncs and closes the open
 * file, and stops ensure_file_open() from touching the card at all
 * until resumed - the whole point is to guarantee nothing is mid-write
 * when the card is physically pulled), then the SD card can be
 * unmounted/removed safely. Task_Logger_Resume() re-arms logging after
 * a fresh mount and forces a brand new session file (not a continuation
 * of the old one) on the next write. */
void Task_Logger_Prepare_For_Eject(void);
void Task_Logger_Resume(void);

#endif /* TASK_LOGGER_H */
