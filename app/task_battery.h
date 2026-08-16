#ifndef TASK_BATTERY_H
#define TASK_BATTERY_H

#include <stdint.h>

typedef enum {
    BATTERY_HW_NOT_FOUND, /* probe failed, or never run */
    BATTERY_HW_OK
} battery_hw_state_t;

/* Bring-up ritual (same pattern as task_sd.c): brings up I2C1, probes
 * the INA3221's WHO_AM_I registers, inits it if found, takes one
 * reading, and prints PASS/FAIL + the reading to the console. Call once
 * at boot. */
void Task_Battery_Init(void);

/* Periodic poll (register with the scheduler) - re-reads channel 3
 * (the battery channel - see docs/pinout.md), estimates SOC, and feeds
 * Task_LED_Set_Battery_State() so the red LED's low/critical states
 * reflect real readings. No-ops if the bring-up probe never succeeded.
 * Reports a latched I2C fault to the LED task after 3 consecutive read
 * failures (transient single-poll glitches shouldn't latch a fault). */
void Task_Battery(void);

battery_hw_state_t Task_Battery_Get_State(void);
uint16_t Task_Battery_Vbat_mV(void);
int16_t Task_Battery_Ibat_mA(void);
uint8_t Task_Battery_Soc_Pct(void);

/* Rough estimate, minutes - time to empty while discharging (Ibat > 0),
 * or time to full while charging (Ibat < 0). Returns 0 both for "0
 * minutes" and "no current to estimate from" - check
 * Task_Battery_Ibat_mA() == 0 to tell those apart. */
uint32_t Task_Battery_Minutes_Left(void);

#endif /* TASK_BATTERY_H */
