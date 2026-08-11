#ifndef SD_SPI_H
#define SD_SPI_H

#include "stm32g474xx.h"

/* SD card in SPI mode, built on drivers/spi.c - takes a bus handle like
 * every other device driver here (bme280_read(&i2c1, ...) style), so
 * moving the card to another SPI peripheral is a one-argument change.
 * This is also the diskio.c glue target for FatFs. */

typedef enum {
    SD_TYPE_NONE = 0,  /* no card, or init failed */
    SD_TYPE_SDSC,       /* byte-addressed - LBA must be multiplied by 512 */
    SD_TYPE_SDHC_SDXC    /* block-addressed - LBA used directly */
} sd_card_type_t;

/* Runs the SPI-mode power-up sequence (CMD0/CMD8/ACMD41/CMD58/CMD16 as
 * needed). Returns 1 on success, 0 on failure/no card/timeout. Leaves the
 * bus at a fast clock (spi_init must have been called first at a slow
 * one - see sd_spi.c for why). */
int sd_init(SPI_TypeDef *spi);

sd_card_type_t sd_get_type(void);

/* Single 512-byte block read/write. lba is the block number (0-based),
 * not a byte address, regardless of card type. Returns 1 on success. */
int sd_read_block(SPI_TypeDef *spi, uint32_t lba, uint8_t *buf);
int sd_write_block(SPI_TypeDef *spi, uint32_t lba, const uint8_t *buf);

#endif /* SD_SPI_H */
