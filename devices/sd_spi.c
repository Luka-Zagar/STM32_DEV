#include "sd_spi.h"
#include "spi.h"
#include "systick.h"

#define CS_PIN 12 /* PB12 - SD_CS, plain GPIO (see docs/pinout.md) */

#define CMD0    0   /* GO_IDLE_STATE */
#define CMD8    8   /* SEND_IF_COND */
#define CMD9    9   /* SEND_CSD */
#define CMD16   16  /* SET_BLOCKLEN */
#define CMD17   17  /* READ_SINGLE_BLOCK */
#define CMD24   24  /* WRITE_BLOCK */
#define CMD55   55  /* APP_CMD - prefixes an ACMD */
#define CMD58   58  /* READ_OCR */
#define ACMD41  41  /* SD_SEND_OP_COND (after CMD55) */

#define DATA_TOKEN_START   0xFE /* single-block read/write data token */

static sd_card_type_t card_type = SD_TYPE_NONE;

static void cs_gpio_init(void) {
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOBEN;
    GPIOB->MODER &= ~(3UL << (CS_PIN * 2));
    GPIOB->MODER |= (1UL << (CS_PIN * 2));
    GPIOB->BSRR = (1UL << CS_PIN); /* idle deselected (active low) */
}

static void cs_select(void)   { GPIOB->BSRR = (1UL << (CS_PIN + 16)); }
static void cs_deselect(void) { GPIOB->BSRR = (1UL << CS_PIN); }

/* Every card command frame and R1 poll needs the bus fed 0xFF - the card
 * only drives MISO meaningfully while it has something to say. */
static uint8_t xfer(SPI_TypeDef *spi, uint8_t b) {
    return spi_transfer(spi, b);
}

static void clock_idle_bytes(SPI_TypeDef *spi, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) xfer(spi, 0xFF);
}

/* Sends a 6-byte command frame and returns R1. CRC is only ever checked
 * by the card for CMD0/CMD8 (SPI mode default: CRC off otherwise), so a
 * fixed CRC7 for just those two commands is enough - no need for a
 * general CRC7 routine. */
static uint8_t send_cmd(SPI_TypeDef *spi, uint8_t cmd, uint32_t arg) {
    uint8_t crc = 0x01;
    if (cmd == CMD0) crc = 0x95;
    else if (cmd == CMD8) crc = 0x87;

    cs_select();
    xfer(spi, 0xFF); /* one idle byte before the command, per spec */
    xfer(spi, 0x40 | cmd);
    xfer(spi, (uint8_t)(arg >> 24));
    xfer(spi, (uint8_t)(arg >> 16));
    xfer(spi, (uint8_t)(arg >> 8));
    xfer(spi, (uint8_t)arg);
    xfer(spi, crc);

    uint8_t r1 = 0xFF;
    for (int i = 0; i < 10; i++) {
        r1 = xfer(spi, 0xFF);
        if (!(r1 & 0x80)) break; /* R1's top bit is always 0 */
    }
    return r1;
}

/* CMD/ACMD end with cs_deselect() left to the caller - some commands
 * (CMD17/CMD24) need the chip still selected afterward for the data
 * phase, so this isn't folded into send_cmd(). */
static uint8_t send_acmd(SPI_TypeDef *spi, uint8_t acmd, uint32_t arg) {
    uint8_t r1 = send_cmd(spi, CMD55, 0);
    cs_deselect();
    clock_idle_bytes(spi, 1);
    if (r1 & 0xFE) return r1; /* anything but "idle" on CMD55 itself is an error */
    return send_cmd(spi, acmd, arg);
}

int sd_init(SPI_TypeDef *spi) {
    card_type = SD_TYPE_NONE;
    cs_gpio_init();

    /* Card must see >=74 clocks with CS high and MOSI high before the
     * first command - this is how it leaves native mode and enters SPI
     * mode. spi_init() is expected to already have set a slow (<400kHz)
     * prescaler at this point; the caller switches to fast only after
     * sd_init() returns success. */
    cs_deselect();
    clock_idle_bytes(spi, 10); /* 80 clocks */

    if (send_cmd(spi, CMD0, 0) != 0x01) {
        cs_deselect();
        return 0; /* card didn't answer GO_IDLE_STATE - no card / not SPI-capable */
    }
    cs_deselect();
    clock_idle_bytes(spi, 1);

    /* CMD8: probes for SD v2 (SDHC/SDXC-capable) vs v1/MMC. 0x1AA =
     * voltage range 2.7-3.6V + the "AA" check pattern echoed back. */
    uint8_t r1 = send_cmd(spi, CMD8, 0x1AA);
    int is_v2 = 0;
    if (r1 == 0x01) {
        uint8_t echo[4];
        for (int i = 0; i < 4; i++) echo[i] = xfer(spi, 0xFF);
        if (echo[2] == 0x01 && echo[3] == 0xAA) is_v2 = 1;
    }
    cs_deselect();
    clock_idle_bytes(spi, 1);

    if (!is_v2) {
        /* v1 SD / MMC path is out of scope - this project only targets
         * modern microSD (SDHC/SDXC) shields. */
        return 0;
    }

    /* ACMD41 with HCS (bit30) set, up to ~1s while the card leaves idle. */
    uint32_t start = SysTick_GetMillis();
    do {
        r1 = send_acmd(spi, ACMD41, (1UL << 30));
        cs_deselect();
        clock_idle_bytes(spi, 1);
        if (r1 == 0x00) break;
    } while ((SysTick_GetMillis() - start) < 1000);

    if (r1 != 0x00) return 0; /* card never left idle - init failed/timeout */

    /* CMD58: read OCR to find CCS (block vs byte addressing). */
    r1 = send_cmd(spi, CMD58, 0);
    if (r1 != 0x00) {
        cs_deselect();
        return 0;
    }
    uint8_t ocr[4];
    for (int i = 0; i < 4; i++) ocr[i] = xfer(spi, 0xFF);
    cs_deselect();
    clock_idle_bytes(spi, 1);

    card_type = (ocr[0] & 0x40) ? SD_TYPE_SDHC_SDXC : SD_TYPE_SDSC;

    if (card_type == SD_TYPE_SDSC) {
        /* Byte-addressed cards default to a 512-byte block length on
         * most cards, but not guaranteed - set it explicitly. SDHC/SDXC
         * are always 512 and CMD16 is a no-op/ignored on them. */
        r1 = send_cmd(spi, CMD16, 512);
        cs_deselect();
        clock_idle_bytes(spi, 1);
        if (r1 != 0x00) return 0;
    }

    return 1;
}

sd_card_type_t sd_get_type(void) {
    return card_type;
}

int sd_read_block(SPI_TypeDef *spi, uint32_t lba, uint8_t *buf) {
    if (card_type == SD_TYPE_NONE) return 0;
    uint32_t addr = (card_type == SD_TYPE_SDHC_SDXC) ? lba : (lba * 512U);

    if (send_cmd(spi, CMD17, addr) != 0x00) {
        cs_deselect();
        return 0;
    }

    /* Wait for the data token (0xFE) - the card can take a while to get
     * the block off flash; bound it so a wedged card can't hang the
     * logger task forever. */
    uint32_t start = SysTick_GetMillis();
    uint8_t token;
    do {
        token = xfer(spi, 0xFF);
        if (token == DATA_TOKEN_START) break;
    } while ((SysTick_GetMillis() - start) < 200);

    if (token != DATA_TOKEN_START) {
        cs_deselect();
        return 0;
    }

    for (int i = 0; i < 512; i++) buf[i] = xfer(spi, 0xFF);
    xfer(spi, 0xFF); /* CRC16, ignored - CRC is off in SPI mode by default */
    xfer(spi, 0xFF);

    cs_deselect();
    clock_idle_bytes(spi, 1);
    return 1;
}

int sd_write_block(SPI_TypeDef *spi, uint32_t lba, const uint8_t *buf) {
    if (card_type == SD_TYPE_NONE) return 0;
    uint32_t addr = (card_type == SD_TYPE_SDHC_SDXC) ? lba : (lba * 512U);

    if (send_cmd(spi, CMD24, addr) != 0x00) {
        cs_deselect();
        return 0;
    }

    xfer(spi, 0xFF); /* one byte gap before the data token, per spec */
    xfer(spi, DATA_TOKEN_START);
    for (int i = 0; i < 512; i++) xfer(spi, buf[i]);
    xfer(spi, 0xFF); /* dummy CRC16 - not checked (CRC off) */
    xfer(spi, 0xFF);

    uint8_t resp = xfer(spi, 0xFF);
    if ((resp & 0x1F) != 0x05) { /* data response: xxx0101 = accepted */
        cs_deselect();
        return 0;
    }

    /* Card pulls MISO low while busy programming flash - this is the
     * real SD write-latency spike the project brief warns about
     * (hundreds of ms during housekeeping), so this wait is bounded
     * generously rather than assumed instant. */
    uint32_t start = SysTick_GetMillis();
    while (xfer(spi, 0xFF) == 0x00) {
        if ((SysTick_GetMillis() - start) > 500) {
            cs_deselect();
            return 0;
        }
    }

    cs_deselect();
    clock_idle_bytes(spi, 1);
    return 1;
}
