#ifndef TASK_IMU_H
#define TASK_IMU_H

#include <stdint.h>

typedef enum {
    IMU_HW_NOT_FOUND, /* probe failed, or never run */
    IMU_HW_OK
} imu_hw_state_t;

/* Bring-up ritual (same pattern as task_battery.c): probes the MPU's
 * WHO_AM_I register, inits it (wakes it, sets +/-4g accel / +/-500dps
 * gyro range) if recognized, brings up the AK8963 magnetometer (via I2C
 * bypass mode) if this chip variant has one, and prints PASS/FAIL + the
 * detected chip variant to the console. Call once at boot, AFTER
 * Task_Battery_Init() - both share I2C1 (see docs/pinout.md's I2C bus
 * map) and only task_battery.c brings the bus itself up (i2c_init());
 * this just uses it. */
void Task_IMU_Init(void);

/* Periodic poll (register with the scheduler) - re-reads accel/gyro/
 * temp X/Y/Z, and magnetometer X/Y/Z if present. No-ops if the bring-up
 * probe never succeeded. Reports a latched I2C fault to the LED task
 * after 3 consecutive read failures (mirrors task_battery.c's threshold
 * - a single glitch shouldn't latch). */
void Task_IMU(void);

imu_hw_state_t Task_IMU_Get_State(void);
const char *Task_IMU_Chip_Str(void);
int Task_IMU_Has_Magnetometer(void);

const int16_t *Task_IMU_Accel_mg(void);     /* [3]: X, Y, Z, milli-g */
const int16_t *Task_IMU_Gyro_dps_x10(void); /* [3]: X, Y, Z, degrees/sec x10 */
int16_t Task_IMU_Temp_c100(void);            /* chip's own die temp, deg C x100 - not ambient air */
const int16_t *Task_IMU_Mag_uT_x10(void);   /* [3]: X, Y, Z, microtesla x10 - 0,0,0 if no magnetometer */

/* Relative yaw, degrees x100, range -18000..+18000 - integrated from
 * gyro Z each poll, 0 at boot. Not a compass bearing (needs a trusted
 * magnetometer for that, which this project doesn't rely on) and
 * drifts slowly over time even at rest (MEMS gyro bias never averages
 * to exactly 0) - honest limitation of gyro-only yaw, not a bug. */
int32_t Task_IMU_Yaw_cdeg(void);

#endif /* TASK_IMU_H */
