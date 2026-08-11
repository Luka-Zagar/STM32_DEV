#ifndef SPI_H
#define SPI_H

#include "stm32g474xx.h"

/* Register-level SPI driver, instance-parameterized like uart.c/i2c.c -
 * spi_init(SPI2, ...), never a peripheral-specific spi2_init(). Chip
 * select is NOT handled here: it's a plain GPIO the device layer drives
 * (see devices/sd_spi.c), since one SPI bus can have several devices
 * with independent CS pins. Master mode, full duplex, 8-bit frames only. */

/* prescaler is one of the SPI_BAUD_DIVx values below - fPCLK / divisor. */
void spi_init(SPI_TypeDef *spi, uint32_t prescaler);

/* Changes just the baud rate divisor on an already-initialized bus (SD
 * cards need <400kHz during their own init sequence, then can go fast). */
void spi_set_prescaler(SPI_TypeDef *spi, uint32_t prescaler);

/* Full-duplex single byte transfer: whatever comes back on MISO while
 * out_byte goes out on MOSI. SPI is inherently a shift register - there
 * is no "send only," a receive always happens alongside a send. */
uint8_t spi_transfer(SPI_TypeDef *spi, uint8_t out_byte);

#define SPI_BAUD_DIV2    (0UL << SPI_CR1_BR_Pos)
#define SPI_BAUD_DIV4    (1UL << SPI_CR1_BR_Pos)
#define SPI_BAUD_DIV8    (2UL << SPI_CR1_BR_Pos)
#define SPI_BAUD_DIV16   (3UL << SPI_CR1_BR_Pos)
#define SPI_BAUD_DIV32   (4UL << SPI_CR1_BR_Pos)
#define SPI_BAUD_DIV64   (5UL << SPI_CR1_BR_Pos)
#define SPI_BAUD_DIV128  (6UL << SPI_CR1_BR_Pos)
#define SPI_BAUD_DIV256  (7UL << SPI_CR1_BR_Pos)

#endif /* SPI_H */
