#ifndef INA3221_H
#define INA3221_H

#include "stm32g474xx.h"
#include <stdint.h>

#define INA3221_ADDR 0x40 /* A0 tied to GND - see docs/pinout.md */

/* WHO_AM_I-style bring-up check (project brief's I2C ritual: probe
 * address -> read chip-ID -> print it, wrong ID = stop and fix wiring
 * before debugging data): reads Manufacturer ID (reg 0xFE, must be
 * 0x5449 = "TI" in ASCII) and Die ID (reg 0xFF, must be 0x3220).
 * Returns 1 if both match. */
int ina3221_probe(I2C_TypeDef *i2c);

/* Writes the configuration register - channels 1-3 enabled, continuous
 * shunt+bus conversion (0x7127, which also happens to be the chip's
 * power-on-reset default - written explicitly anyway so behavior
 * doesn't depend on what the chip happened to reset to). Call once
 * after a successful probe. */
void ina3221_init(I2C_TypeDef *i2c);

/* Channel 3 is the battery channel (see docs/pinout.md) - reads its bus
 * voltage and current, computed from a 0.1 Ohm (R100) shunt per the
 * project's battery-monitoring spec. Fixed-point only, no floats.
 * Returns 1 on success. */
int ina3221_read_battery(I2C_TypeDef *i2c, uint16_t *vbat_mv, int16_t *ibat_ma);

#endif /* INA3221_H */
