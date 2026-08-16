#include "task_imu.h"
#include "i2c.h"
#include "mpu9250.h"
#include "task_led.h"
#include "uart.h"
#include "strbuf.h"

#define CONSOLE_UART USART2
#define IMU_I2C I2C1

#define I2C_FAULT_THRESHOLD 3 /* consecutive read failures before latching - a single glitch shouldn't latch */
#define I2C_FAULT_CLEAR_THRESHOLD 5 /* consecutive good reads before auto-clearing the LED latch -
                                      * see task_battery.c's identical constant, same reasoning */

/* Must match main.c's SCH_Add_Task(Task_IMU, 0, ...) period - used to
 * integrate gyro Z into a running yaw estimate below. */
#define POLL_PERIOD_MS 200

static imu_hw_state_t state = IMU_HW_NOT_FOUND;
static mpu9250_chip_t chip = MPU_CHIP_UNKNOWN;
static int has_mag = 0;
static int16_t accel_mg[3] = {0, 0, 0};
static int16_t gyro_dps_x10[3] = {0, 0, 0};
static int16_t temp_c100 = 0;
static int16_t mag_ut_x10[3] = {0, 0, 0};
static int32_t yaw_cdeg = 0; /* degrees x100, see Task_IMU_Yaw_cdeg() */
static int consecutive_read_failures = 0;
static int consecutive_read_successes = 0;

static int32_t normalize_cdeg(int32_t v) {
    v %= 36000;
    if (v > 18000) v -= 36000;
    if (v < -18000) v += 36000;
    return v;
}

static void report(const char *msg) {
    uart_write_str(CONSOLE_UART, "MPU: ");
    uart_write_str(CONSOLE_UART, msg);
    uart_write_str(CONSOLE_UART, "\r\n");
}

/* Temporary bring-up diagnostic, same as task_battery.c's scan_bus() -
 * tells "nothing answered at all" (bus/wiring issue) apart from "a
 * device answered but its WHO_AM_I didn't match anything recognized"
 * (wrong address, or address occupied by something else on this shared
 * bus). */
static void scan_bus(void) {
    char msg[32];
    strbuf_t sb;
    int found_any = 0;
    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        if (i2c_probe(IMU_I2C, addr)) {
            found_any = 1;
            strbuf_init(&sb, msg, sizeof(msg));
            strbuf_str(&sb, "  found addr (decimal) ");
            strbuf_uint(&sb, addr); /* 0x68 = 104, 0x69 = 105 */
            report(msg);
        }
    }
    if (!found_any) report("bus scan: nothing acked any address 0x08-0x77");
}

static const char *chip_str(mpu9250_chip_t c) {
    switch (c) {
        case MPU_CHIP_MPU9250: return "MPU9250";
        case MPU_CHIP_MPU9255: return "MPU9255 (later MPU9250 successor, identical register map)";
        case MPU_CHIP_MPU6500: return "MPU6500 (common MPU9250 substitute, no magnetometer - fine, accel/gyro-only use)";
        case MPU_CHIP_MPU6050: return "MPU6050 (older/different part, sometimes mislabeled - accel/gyro-only use is still fine)";
        default: return "unknown";
    }
}

void Task_IMU_Init(void) {
    /* I2C1 is already brought up by task_battery.c (shared bus, no
     * mutex needed - see docs/pinout.md's I2C bus map) - do not
     * i2c_init() again here. */
    chip = mpu9250_probe(IMU_I2C);
    if (chip == MPU_CHIP_UNKNOWN) {
        state = IMU_HW_NOT_FOUND;
        report("probe failed (WHO_AM_I mismatch) - check wiring (SCL=PB8, SDA=PB9, addr=0x68)");
        scan_bus();
        return;
    }

    mpu9250_init(IMU_I2C);
    state = IMU_HW_OK;

    has_mag = mpu9250_has_magnetometer(chip);
    if (has_mag) {
        has_mag = mpu9250_mag_init(IMU_I2C);
        if (!has_mag) report("magnetometer expected but AK8963 bring-up failed - accel/gyro/temp still fine");
    }

    report(chip_str(chip));
}

void Task_IMU(void) {
    if (state != IMU_HW_OK) return;

    /* Bitwise &, not &&, deliberately - all three reads should run even
     * if an earlier one fails, so a single register read glitch doesn't
     * also skip updating the others. */
    int ok = mpu9250_read_accel(IMU_I2C, accel_mg)
           & mpu9250_read_gyro(IMU_I2C, gyro_dps_x10)
           & mpu9250_read_temp(IMU_I2C, &temp_c100);

    if (has_mag) {
        mpu9250_read_mag(IMU_I2C, mag_ut_x10); /* 0 just means "no new sample yet" - not a fault, don't count it against ok */
    }

    if (!ok) {
        consecutive_read_failures++;
        consecutive_read_successes = 0;
        if (consecutive_read_failures >= I2C_FAULT_THRESHOLD) {
            Task_LED_Report_I2C_Fault();
            /* Same shared-bus lockup this threshold exists to catch as
             * task_battery.c's identical block - see i2c_bus_recover()'s
             * doc comment. Fixes task_battery.c too, same physical bus. */
            i2c_bus_recover(IMU_I2C);
            consecutive_read_failures = 0;
        }
        return;
    }
    consecutive_read_failures = 0;

    /* Auto-clears the LED's I2C fault latch once recovery's proven
     * itself - see task_battery.c's identical block/Task_LED_Clear_I2C_Fault()'s
     * doc comment for why. Harmless no-op if nothing's latched. */
    consecutive_read_successes++;
    if (consecutive_read_successes >= I2C_FAULT_CLEAR_THRESHOLD) {
        Task_LED_Clear_I2C_Fault();
    }

    /* Yaw has no gravity reference (unlike pitch/roll, which come
     * straight from the accelerometer each read) - the only way to get
     * it without a trusted magnetometer is to integrate the gyroscope's
     * Z-axis rate over time. That's relative (0 at boot, not a compass
     * bearing) and drifts (MEMS gyro bias never averages out to exactly
     * 0, so this walks slowly even sitting still) - exact math, honest
     * limitation, not a bug. dyaw_cdeg = gyro_z_dps_x10 * POLL_PERIOD_MS
     * / 100 (degrees x100 per tick; the /100 divides evenly for
     * POLL_PERIOD_MS=200, no rounding loss). */
    yaw_cdeg = normalize_cdeg(yaw_cdeg + (int32_t)gyro_dps_x10[2] * POLL_PERIOD_MS / 100);
}

imu_hw_state_t Task_IMU_Get_State(void) { return state; }
const char *Task_IMU_Chip_Str(void) { return chip_str(chip); }
int Task_IMU_Has_Magnetometer(void) { return has_mag; }
const int16_t *Task_IMU_Accel_mg(void) { return accel_mg; }
const int16_t *Task_IMU_Gyro_dps_x10(void) { return gyro_dps_x10; }
int16_t Task_IMU_Temp_c100(void) { return temp_c100; }
const int16_t *Task_IMU_Mag_uT_x10(void) { return mag_ut_x10; }
int32_t Task_IMU_Yaw_cdeg(void) { return yaw_cdeg; }
