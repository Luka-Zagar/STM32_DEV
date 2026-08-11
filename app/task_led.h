#ifndef TASK_LED_H
#define TASK_LED_H

#include <stdint.h>

/* Drives the green (PC6) and red (PC8) status LEDs per the state table
 * below. White (PC7) is untouched here - it's the heartbeat, wired
 * separately in task_heartbeat.c, and deliberately never shares logic
 * with anything else ("the one failure nothing else can report").
 *
 * Green - reflects logging health (SD mounted + GPS fix). Battery state
 * is intentionally NOT part of green's logic even though the spec's
 * "solid on" case mentions battery OK - red already conveys battery
 * state on its own, so folding it into green would just be the same
 * information shown twice with two different failure semantics (green
 * off could then mean "not logging" OR "battery bad", which is worse,
 * not better).
 *   solid on    - SD mounted AND GPS fix
 *   slow flash  - SD mounted, no GPS fix (logging on tick-counter time)
 *   off         - SD not mounted / init failed
 *
 * Red - fault indicator, off in the normal case. Only one pattern can
 * show at a time, so faults are prioritized worst-first:
 *   solid       - battery critical / imminent shutdown      (live)
 *   1 flash/s   - SD write error, data at risk               (latched)
 *   2 flash/s   - I2C fault (INA3221/IMU not responding)      (latched)
 *   3 flash/s   - battery low                                (live)
 *   off         - no fault
 * "Latched" faults stay on once triggered until Task_LED_Ack_Faults()
 * is called (wired to a console key - see task_console.c's LED menu),
 * even if the underlying condition clears itself, so a transient SD
 * write error can't scroll off unnoticed. */

typedef enum {
    BATTERY_OK = 0,
    BATTERY_LOW,
    BATTERY_CRITICAL
} battery_state_t;

void Task_LED_Init(void);
void Task_LED(void);

/* Clears both latched red fault conditions (SD write error, I2C fault).
 * Does not touch the live battery state or the underlying error
 * counters (task_logger.c's Task_Logger_Write_Errors() keeps counting
 * for diagnostics regardless of whether the LED fault was acked). */
void Task_LED_Ack_Faults(void);

/* Hook for the future I2C acquire task (INA3221/IMU) to report a
 * WHO_AM_I mismatch or a bus timeout - latches the red 2-flash/s state.
 * No I2C devices exist yet, so nothing calls this today. */
void Task_LED_Report_I2C_Fault(void);

/* Hook for the future battery/SoC task (INA3221) to report live state.
 * Defaults to BATTERY_OK until something calls this. */
void Task_LED_Set_Battery_State(battery_state_t state);

/* For the console LED menu / status dashboard. */
const char *Task_LED_Green_Str(void);
const char *Task_LED_Red_Str(void);

#endif /* TASK_LED_H */
