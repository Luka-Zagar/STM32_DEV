#ifndef I2C_H
#define I2C_H

#include "stm32g474xx.h"

/* Register-level I2C driver, instance-parameterized like uart.c/spi.c -
 * i2c_init(I2C1, ...), never i2c1_init(). Master mode only (this project
 * never acts as an I2C slave), blocking with bounded timeouts (never
 * hangs forever on a stuck bus - same philosophy as every other driver
 * here). One physical bus, shared by every I2C sensor (see project
 * brief's bus map) - devices/ina3221.c etc. take an I2C_TypeDef* as
 * their bus handle. */

/* CubeMX-computed, ST-verified value for Fast Mode (400kHz) at
 * I2CCLK=170MHz (APB1, undivided - matches this project's clock tree),
 * rise time 100ns / fall time 10ns. Taken from ST's own NUCLEO-G474RE
 * I2C LL example (same PLLM/N/R as core/system_clock.c), not hand-
 * derived - I2C TIMINGR math has fixed synchronizer/filter delays that
 * a naive formula misses, so trusting a characterized value here rather
 * than repeating the RTC WPR-offset mistake. */
#define I2C_TIMING_400KHZ_170MHZ 0x00C0216CUL

void i2c_init(I2C_TypeDef *i2c, uint32_t timing);

/* Zero-length write - just checks for an ACK on the address byte. The
 * standard "is anything at this address" bus-probe, used for the WHO_AM_I-
 * style bring-up ritual every I2C device here starts with. Returns 1 if
 * acked, 0 otherwise (NACK, timeout, bus stuck). */
int i2c_probe(I2C_TypeDef *i2c, uint8_t addr7);

int i2c_write(I2C_TypeDef *i2c, uint8_t addr7, const uint8_t *data, uint32_t len);
int i2c_read(I2C_TypeDef *i2c, uint8_t addr7, uint8_t *data, uint32_t len);

/* Write then repeated-START into a read, no STOP in between - the usual
 * "write register address, read register value(s)" pattern every one of
 * these sensors uses. */
int i2c_write_read(I2C_TypeDef *i2c, uint8_t addr7,
                    const uint8_t *wbuf, uint32_t wlen,
                    uint8_t *rbuf, uint32_t rlen);

/* Un-wedges a stuck bus. If a slave is interrupted mid-transaction
 * (electrical noise, a slave brown-out/reset while the master held it
 * mid-byte) it can latch SDA low forever - the hardware peripheral has
 * no way to recover from that on its own, and every transaction after
 * that just times out on BUSY, silently, forever (this is the "sensor
 * readings freeze, I2C fault LED latches, never comes back until power
 * cycle" failure mode). Standard fix: drop to plain GPIO, clock SCL up
 * to 9 times (enough to flush any partial byte a stuck slave thinks it
 * still owes) watching for SDA to release, issue a manual STOP, then
 * reinitialize the peripheral from scratch (same timing it was last
 * given). Callers should call this after a run of consecutive failures
 * (see task_battery.c/task_imu.c's I2C_FAULT_THRESHOLD) - not on every
 * single glitch, a real bus lockup is what this is for. */
void i2c_bus_recover(I2C_TypeDef *i2c);

#endif /* I2C_H */
