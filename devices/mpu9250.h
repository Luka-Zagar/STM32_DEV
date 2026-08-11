#ifndef MPU9250_H
#define MPU9250_H

#include "stm32g474xx.h"
#include <stdint.h>

#define MPU9250_ADDR 0x68 /* AD0 tied to GND - see docs/pinout.md */
#define AK8963_ADDR  0x0C /* embedded magnetometer, only present on MPU9250/9255 - reachable via I2C bypass mode */

/* MPU-9250 is discontinued and the project brief already flags this:
 * many modules in circulation are remarked MPU-6500 (no magnetometer)
 * or outright different parts. */
typedef enum {
    MPU_CHIP_UNKNOWN = 0,
    MPU_CHIP_MPU9250, /* WHO_AM_I = 0x71 */
    MPU_CHIP_MPU9255, /* WHO_AM_I = 0x73 - later MPU9250 successor, identical register map */
    MPU_CHIP_MPU6500, /* WHO_AM_I = 0x70 - common MPU9250 substitute, no magnetometer */
    MPU_CHIP_MPU6050  /* WHO_AM_I = 0x68 - older/different part, sometimes mislabeled */
} mpu9250_chip_t;

/* WHO_AM_I-style bring-up check (project brief's I2C ritual). Returns
 * the detected chip variant, or MPU_CHIP_UNKNOWN if the WHO_AM_I byte
 * doesn't match anything recognized - stop and check wiring in that
 * case before debugging data. */
mpu9250_chip_t mpu9250_probe(I2C_TypeDef *i2c);

/* True for chip variants with an embedded AK8963 magnetometer
 * (MPU9250/9255) - false for MPU6500/6050. */
int mpu9250_has_magnetometer(mpu9250_chip_t chip);

/* Wakes the chip (clears the power-on-default SLEEP bit), sets the
 * accelerometer to +/-4g and gyroscope to +/-500dps - both trade a bit
 * of resolution for headroom against real bumps/shock on a bus-mounted
 * sensor, vs the +/-2g/+/-250dps power-on defaults. Call once after a
 * successful probe. */
void mpu9250_init(I2C_TypeDef *i2c);

/* Enables I2C bypass mode (INT_PIN_CFG's BYPASS_EN) so the AK8963
 * becomes directly addressable at AK8963_ADDR on this same bus, instead
 * of needing the MPU's built-in I2C-master pass-through - much simpler
 * to drive by hand at register level. Also reads the AK8963's factory
 * sensitivity-adjustment values and puts it in 16-bit continuous mode.
 * Only call if mpu9250_has_magnetometer() is true. Returns 1 on success
 * (AK8963 WHO_AM_I matched 0x48). */
int mpu9250_mag_init(I2C_TypeDef *i2c);

/* Accelerometer X/Y/Z, milli-g. */
int mpu9250_read_accel(I2C_TypeDef *i2c, int16_t *accel_mg);

/* Gyroscope X/Y/Z, degrees/sec x10. */
int mpu9250_read_gyro(I2C_TypeDef *i2c, int16_t *gyro_dps_x10);

/* Chip's own die temperature (not ambient air - this is not a BME280
 * substitute) - deg C x100. */
int mpu9250_read_temp(I2C_TypeDef *i2c, int16_t *temp_c100);

/* Magnetometer X/Y/Z, microtesla x10 - only call after a successful
 * mpu9250_mag_init(). Returns 0 (not an error - just "nothing new since
 * the last read") if the AK8963's data-ready bit isn't set, or if this
 * sample overflowed (ST2 HOFL bit). */
int mpu9250_read_mag(I2C_TypeDef *i2c, int16_t *mag_ut_x10);

#endif /* MPU9250_H */
