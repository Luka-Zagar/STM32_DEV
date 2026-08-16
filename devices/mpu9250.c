#include "mpu9250.h"
#include "i2c.h"

#define REG_GYRO_CONFIG  0x1B
#define REG_ACCEL_CONFIG 0x1C
#define REG_INT_PIN_CFG  0x37
#define REG_TEMP_OUT_H   0x41
#define REG_GYRO_XOUT_H  0x43
#define REG_ACCEL_XOUT_H 0x3B
#define REG_USER_CTRL    0x6A
#define REG_PWR_MGMT_1   0x6B
#define REG_WHO_AM_I     0x75

#define WHO_AM_I_MPU9250 0x71
#define WHO_AM_I_MPU9255 0x73
#define WHO_AM_I_MPU6500 0x70
#define WHO_AM_I_MPU6050 0x68

#define ACCEL_FS_4G     0x08 /* AFS_SEL=01 in ACCEL_CONFIG bits[4:3] -> +/-4g */
#define ACCEL_LSB_PER_G 8192 /* sensitivity at +/-4g full scale */

#define GYRO_FS_500DPS       0x08 /* FS_SEL=01 in GYRO_CONFIG bits[4:3] -> +/-500dps */
#define GYRO_LSB_PER_DPS_X10 655  /* 65.5 LSB/dps, x10'd to stay integer */

#define AK_REG_WHO_AM_I     0x00
#define AK_REG_ST1          0x02
#define AK_REG_XOUT_L       0x03
#define AK_REG_CNTL         0x0A
#define AK_REG_ASAX         0x10
#define AK_WHO_AM_I         0x48
#define AK_MODE_POWERDOWN   0x00
#define AK_MODE_FUSE_ROM    0x0F
#define AK_MODE_CONT2_16BIT 0x16 /* bit4=1: 16-bit output; bits[3:0]=0110: continuous mode 2, 100Hz */
#define AK_ST2_HOFL         0x08 /* magnetic sensor overflow - this sample is garbage */
#define AK_MAG_FULLSCALE_UT 4912 /* +/-4912uT full scale in 16-bit mode, per datasheet */
#define AK_MAG_COUNTS       32760

/* Factory per-axis sensitivity adjustment, (ASA+128) so the neutral
 * (uncalibrated, factor=1.0) value is 256 - see mpu9250_mag_init(). */
static int16_t mag_asa_x256[3] = {256, 256, 256};

mpu9250_chip_t mpu9250_probe(I2C_TypeDef *i2c) {
    uint8_t wbuf[1] = { REG_WHO_AM_I };
    uint8_t who;
    if (!i2c_write_read(i2c, MPU9250_ADDR, wbuf, 1, &who, 1)) return MPU_CHIP_UNKNOWN;

    switch (who) {
        case WHO_AM_I_MPU9250: return MPU_CHIP_MPU9250;
        case WHO_AM_I_MPU9255: return MPU_CHIP_MPU9255;
        case WHO_AM_I_MPU6500: return MPU_CHIP_MPU6500;
        case WHO_AM_I_MPU6050: return MPU_CHIP_MPU6050;
        default: return MPU_CHIP_UNKNOWN;
    }
}

int mpu9250_has_magnetometer(mpu9250_chip_t chip) {
    return chip == MPU_CHIP_MPU9250 || chip == MPU_CHIP_MPU9255;
}

void mpu9250_init(I2C_TypeDef *i2c) {
    uint8_t wake[2] = { REG_PWR_MGMT_1, 0x00 }; /* clear SLEEP, keep default internal oscillator */
    i2c_write(i2c, MPU9250_ADDR, wake, 2);

    uint8_t afs[2] = { REG_ACCEL_CONFIG, ACCEL_FS_4G };
    i2c_write(i2c, MPU9250_ADDR, afs, 2);

    uint8_t gfs[2] = { REG_GYRO_CONFIG, GYRO_FS_500DPS };
    i2c_write(i2c, MPU9250_ADDR, gfs, 2);
}

int mpu9250_mag_init(I2C_TypeDef *i2c) {
    uint8_t usr[2] = { REG_USER_CTRL, 0x00 }; /* I2C master mode off - we use bypass instead */
    i2c_write(i2c, MPU9250_ADDR, usr, 2);
    uint8_t bypass[2] = { REG_INT_PIN_CFG, 0x02 }; /* BYPASS_EN */
    i2c_write(i2c, MPU9250_ADDR, bypass, 2);

    uint8_t wbuf[1] = { AK_REG_WHO_AM_I };
    uint8_t who;
    if (!i2c_write_read(i2c, AK8963_ADDR, wbuf, 1, &who, 1) || who != AK_WHO_AM_I) {
        return 0;
    }

    /* Factory sensitivity adjustment values, one per axis - only
     * readable in fuse ROM access mode. */
    uint8_t pd1[2] = { AK_REG_CNTL, AK_MODE_POWERDOWN };
    i2c_write(i2c, AK8963_ADDR, pd1, 2);
    uint8_t fuse[2] = { AK_REG_CNTL, AK_MODE_FUSE_ROM };
    i2c_write(i2c, AK8963_ADDR, fuse, 2);

    uint8_t asa_reg[1] = { AK_REG_ASAX };
    uint8_t asa[3];
    i2c_write_read(i2c, AK8963_ADDR, asa_reg, 1, asa, 3);
    for (int i = 0; i < 3; i++) {
        /* factor = (ASA-128)/256 + 1 = (ASA+128)/256 - stored x256 to stay integer */
        mag_asa_x256[i] = (int16_t)((int32_t)asa[i] + 128);
    }

    uint8_t pd2[2] = { AK_REG_CNTL, AK_MODE_POWERDOWN };
    i2c_write(i2c, AK8963_ADDR, pd2, 2);
    uint8_t cont[2] = { AK_REG_CNTL, AK_MODE_CONT2_16BIT };
    i2c_write(i2c, AK8963_ADDR, cont, 2);

    return 1;
}

int mpu9250_read_accel(I2C_TypeDef *i2c, int16_t *accel_mg) {
    uint8_t wbuf[1] = { REG_ACCEL_XOUT_H };
    uint8_t rbuf[6];
    if (!i2c_write_read(i2c, MPU9250_ADDR, wbuf, 1, rbuf, 6)) return 0;

    for (int i = 0; i < 3; i++) {
        int16_t raw = (int16_t)(((uint16_t)rbuf[i * 2] << 8) | rbuf[i * 2 + 1]);
        accel_mg[i] = (int16_t)(((int32_t)raw * 1000) / ACCEL_LSB_PER_G);
    }
    return 1;
}

int mpu9250_read_gyro(I2C_TypeDef *i2c, int16_t *gyro_dps_x10) {
    uint8_t wbuf[1] = { REG_GYRO_XOUT_H };
    uint8_t rbuf[6];
    if (!i2c_write_read(i2c, MPU9250_ADDR, wbuf, 1, rbuf, 6)) return 0;

    for (int i = 0; i < 3; i++) {
        int16_t raw = (int16_t)(((uint16_t)rbuf[i * 2] << 8) | rbuf[i * 2 + 1]);
        gyro_dps_x10[i] = (int16_t)(((int32_t)raw * 100) / GYRO_LSB_PER_DPS_X10);
    }
    return 1;
}

int mpu9250_read_temp(I2C_TypeDef *i2c, int16_t *temp_c100) {
    uint8_t wbuf[1] = { REG_TEMP_OUT_H };
    uint8_t rbuf[2];
    if (!i2c_write_read(i2c, MPU9250_ADDR, wbuf, 1, rbuf, 2)) return 0;

    int16_t raw = (int16_t)(((uint16_t)rbuf[0] << 8) | rbuf[1]);
    /* degC = raw/333.87 + 21 (InvenSense datasheet) -> degC*100 = raw*10000/33387 + 2100 */
    *temp_c100 = (int16_t)(((int32_t)raw * 10000) / 33387 + 2100);
    return 1;
}

int mpu9250_read_mag(I2C_TypeDef *i2c, int16_t *mag_ut_x10) {
    uint8_t st1_reg[1] = { AK_REG_ST1 };
    uint8_t st1;
    if (!i2c_write_read(i2c, AK8963_ADDR, st1_reg, 1, &st1, 1)) return 0;
    if (!(st1 & 0x01)) return 0; /* not ready yet - not an error, just nothing new */

    /* X/Y/Z (6 bytes) + ST2 - ST2 must be read as part of the same
     * sequential transaction to latch/release the data registers for
     * the next sample, per the AK8963 datasheet. */
    uint8_t wbuf[1] = { AK_REG_XOUT_L };
    uint8_t rbuf[7];
    if (!i2c_write_read(i2c, AK8963_ADDR, wbuf, 1, rbuf, 7)) return 0;

    if (rbuf[6] & AK_ST2_HOFL) return 0; /* overflow - this sample is garbage */

    for (int i = 0; i < 3; i++) {
        /* AK8963 registers are little-endian (L then H), unlike the
         * MPU's own accel/gyro/temp registers (H then L). */
        int16_t raw = (int16_t)(((uint16_t)rbuf[i * 2 + 1] << 8) | rbuf[i * 2]);
        /* mag_uT_x10 = raw * (ASA+128)/256 * 4912*10/32760 - int64
         * intermediate since raw*asa_x256*49120 can exceed int32. */
        int64_t v = (int64_t)raw * mag_asa_x256[i] * (AK_MAG_FULLSCALE_UT * 10);
        mag_ut_x10[i] = (int16_t)(v / (256 * AK_MAG_COUNTS));
    }
    return 1;
}
