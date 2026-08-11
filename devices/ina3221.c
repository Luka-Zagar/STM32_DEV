#include "ina3221.h"
#include "i2c.h"

#define REG_CONFIG       0x00
#define REG_SHUNT_CH3    0x05
#define REG_BUS_CH3      0x06
#define REG_MANUFACTURER 0xFE
#define REG_DIE_ID       0xFF

#define MANUFACTURER_ID_EXPECT 0x5449 /* "TI" */
#define DIE_ID_EXPECT           0x3220

#define SHUNT_MILLIOHMS 100 /* R100 shunt, per project spec */

static int read_reg16(I2C_TypeDef *i2c, uint8_t reg, int16_t *out) {
    uint8_t wbuf[1] = { reg };
    uint8_t rbuf[2];
    if (!i2c_write_read(i2c, INA3221_ADDR, wbuf, 1, rbuf, 2)) return 0;
    *out = (int16_t)(((uint16_t)rbuf[0] << 8) | rbuf[1]);
    return 1;
}

int ina3221_probe(I2C_TypeDef *i2c) {
    int16_t mfr, die;
    if (!read_reg16(i2c, REG_MANUFACTURER, &mfr)) return 0;
    if (!read_reg16(i2c, REG_DIE_ID, &die)) return 0;
    return (uint16_t)mfr == MANUFACTURER_ID_EXPECT && (uint16_t)die == DIE_ID_EXPECT;
}

void ina3221_init(I2C_TypeDef *i2c) {
    uint8_t wbuf[3] = { REG_CONFIG, 0x71, 0x27 };
    i2c_write(i2c, INA3221_ADDR, wbuf, 3);
}

int ina3221_read_battery(I2C_TypeDef *i2c, uint16_t *vbat_mv, int16_t *ibat_ma) {
    int16_t bus_raw, shunt_raw;
    if (!read_reg16(i2c, REG_BUS_CH3, &bus_raw)) return 0;
    if (!read_reg16(i2c, REG_SHUNT_CH3, &shunt_raw)) return 0;

    /* Both registers are 13-bit signed, left-justified in bits 15:3.
     * LSB = 8mV for bus voltage, 40uV for shunt voltage (TI datasheet).
     * Arithmetic right shift on a negative int16_t is technically
     * implementation-defined by the C standard, but is exactly what
     * every ARM GCC target (and every reference INA3221 driver) does in
     * practice - sign-extending, as required here. */
    int32_t bus_mv = (bus_raw >> 3) * 8;
    int32_t shunt_uv = (shunt_raw >> 3) * 40;

    *vbat_mv = (uint16_t)(bus_mv < 0 ? 0 : bus_mv);
    *ibat_ma = (int16_t)(shunt_uv / SHUNT_MILLIOHMS); /* mA = uV / mOhm */
    return 1;
}
