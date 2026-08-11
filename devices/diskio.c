/* FatFs low-level disk I/O glue, hand-written at register level (via
 * drivers/spi.c + devices/sd_spi.c) - this is the part of the SD stack
 * that's actually interesting to write ourselves; ff.c/ff.h above it are
 * vendored (ChaN's FatFs R0.15, see devices/fatfs/). Single SD card on
 * SPI2, drive number 0 (FF_VOLUMES == 1). */

#include "diskio.h"
#include "spi.h"
#include "sd_spi.h"
#include "rtc.h"

#define SD_SPI SPI2

static DSTATUS drive_status = STA_NOINIT;

DSTATUS disk_initialize(BYTE pdrv) {
    if (pdrv != 0) return STA_NOINIT;

    /* Card init sequence must run at <400kHz (spec requirement while the
     * card is still figuring out what kind of card it is) - fPCLK is
     * 170MHz, so DIV256 gives ~664kHz. Close enough in practice (this is
     * a common simplification - cards are lenient here), but drop to
     * DIV256 as the slowest available divisor rather than risk a faster
     * one that's further from spec. */
    spi_init(SD_SPI, SPI_BAUD_DIV256);

    if (sd_init(SD_SPI)) {
        /* DIV8 (~21.25MHz, fPCLK/8 since APB1 isn't prescaled from the
         * 170MHz SYSCLK) turned out unreliable enough over jumper wires
         * to corrupt the very first fast-clock read (the boot sector -
         * SPI mode has CRC disabled by default, so bit errors pass
         * through silently instead of raising a read error). DIV64
         * (~2.66MHz) trades speed for signal integrity on this
         * breadboard wiring; revisit once this is a proper PCB trace. */
        spi_set_prescaler(SD_SPI, SPI_BAUD_DIV64);
        drive_status = 0;
    } else {
        drive_status = STA_NOINIT;
    }
    return drive_status;
}

DSTATUS disk_status(BYTE pdrv) {
    if (pdrv != 0) return STA_NOINIT;
    return drive_status;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
    if (pdrv != 0 || drive_status) return RES_NOTRDY;
    for (UINT i = 0; i < count; i++) {
        if (!sd_read_block(SD_SPI, (uint32_t)sector + i, buff + i * 512U)) {
            return RES_ERROR;
        }
    }
    return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count) {
    if (pdrv != 0 || drive_status) return RES_NOTRDY;
    for (UINT i = 0; i < count; i++) {
        if (!sd_write_block(SD_SPI, (uint32_t)sector + i, buff + i * 512U)) {
            return RES_ERROR;
        }
    }
    return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
    if (pdrv != 0) return RES_PARERR;
    switch (cmd) {
        case CTRL_SYNC:
            /* Nothing buffered on our side (sd_write_block already waits
             * out the card's busy signal before returning) - the write
             * is durable by the time f_write()/f_sync() calls us. */
            return RES_OK;
        case GET_SECTOR_SIZE:
            *(WORD *)buff = 512;
            return RES_OK;
        default:
            return RES_PARERR; /* GET_SECTOR_COUNT/GET_BLOCK_SIZE etc. - only needed for f_mkfs, unused (FF_USE_MKFS=0) */
    }
}

/* FF_FS_NORTC == 0 (ffconf.h): FatFs wants real file timestamps and we
 * have a real RTC (synced from GPS - see app/task_gps.c), so use it
 * instead of the fixed-date fallback. Packed per FatFs's DWORD format:
 * bit31:25 year-1980, 24:21 month, 20:16 day, 15:11 hour, 10:5 min,
 * 4:0 sec/2. */
DWORD get_fattime(void) {
    rtc_datetime_t dt;
    rtc_get_datetime(&dt);

    uint32_t year_field = (dt.year >= 1980) ? (dt.year - 1980) : 0;
    return (year_field << 25) | ((uint32_t)dt.month << 21) | ((uint32_t)dt.day << 16)
         | ((uint32_t)dt.hour << 11) | ((uint32_t)dt.min << 5) | ((uint32_t)dt.sec >> 1);
}
