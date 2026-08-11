#include "i2c.h"
#include "systick.h"

#define I2C_TIMEOUT_MS 50UL /* generous for a few-byte transfer at 400kHz; a stuck bus should never hang the caller past this */

static void i2c_gpio_init(I2C_TypeDef *i2c) {
    if (i2c == I2C1) {
        /* PB8 = SCL, PB9 = SDA, AF4 - open-drain is not optional here
         * (I2C is a wired-AND bus; push-pull would fight another device
         * pulling the line low), and pull-up alongside whatever external
         * ones are already on the sensor modules is cheap insurance, not
         * redundant harm - internal ~40k pull is dominated by any real
         * external pull-up anyway. */
        RCC->AHB2ENR |= RCC_AHB2ENR_GPIOBEN;
        GPIOB->MODER &= ~((3UL << (8 * 2)) | (3UL << (9 * 2)));
        GPIOB->MODER |= (2UL << (8 * 2)) | (2UL << (9 * 2));
        GPIOB->OTYPER |= (1UL << 8) | (1UL << 9);
        GPIOB->PUPDR &= ~((3UL << (8 * 2)) | (3UL << (9 * 2)));
        GPIOB->PUPDR |= (1UL << (8 * 2)) | (1UL << (9 * 2));
        GPIOB->AFR[1] &= ~((0xFUL << ((8 - 8) * 4)) | (0xFUL << ((9 - 8) * 4)));
        GPIOB->AFR[1] |= (4UL << ((8 - 8) * 4)) | (4UL << ((9 - 8) * 4));
    }
}

void i2c_init(I2C_TypeDef *i2c, uint32_t timing) {
    i2c_gpio_init(i2c);

    if (i2c == I2C1) {
        RCC->APB1ENR1 |= RCC_APB1ENR1_I2C1EN;
    }

    i2c->CR1 &= ~I2C_CR1_PE; /* TIMINGR is only writable while PE=0 */
    i2c->TIMINGR = timing;
    i2c->CR1 = I2C_CR1_PE;
}

static int wait_set(I2C_TypeDef *i2c, uint32_t mask, uint32_t timeout_ms) {
    uint32_t start = SysTick_GetMillis();
    while (!(i2c->ISR & mask)) {
        if ((SysTick_GetMillis() - start) > timeout_ms) return 0;
    }
    return 1;
}

static int wait_not_busy(I2C_TypeDef *i2c, uint32_t timeout_ms) {
    uint32_t start = SysTick_GetMillis();
    while (i2c->ISR & I2C_ISR_BUSY) {
        if ((SysTick_GetMillis() - start) > timeout_ms) return 0;
    }
    return 1;
}

/* Runs one direction of a transfer (len bytes, START already implied),
 * either finishing with AUTOEND's automatic STOP or leaving the bus held
 * (autoend=0) for a repeated START - i2c_write_read uses the latter to
 * chain a write phase straight into a read phase. len=0 is a valid,
 * deliberate case: the address-only bus probe (i2c_probe) - NACKF is
 * checked in exactly the same place either way, since with NBYTES=0 the
 * hardware resolves ack/nack on the address itself before any TXIS/RXNE
 * would ever fire. Always leaves the bus in a clean state: any failure
 * path issues its own STOP so a NACK never wedges the bus for the next
 * caller. */
static int transfer_phase(I2C_TypeDef *i2c, uint8_t addr7, uint8_t *buf, uint32_t len,
                           int is_read, int autoend) {
    uint32_t cr2 = ((uint32_t)addr7 << 1) | (len << I2C_CR2_NBYTES_Pos) | I2C_CR2_START;
    if (is_read) cr2 |= I2C_CR2_RD_WRN;
    if (autoend) cr2 |= I2C_CR2_AUTOEND;
    i2c->CR2 = cr2;

    for (uint32_t i = 0; i < len; i++) {
        uint32_t mask = is_read ? I2C_ISR_RXNE : I2C_ISR_TXIS;
        if (!wait_set(i2c, mask | I2C_ISR_NACKF, I2C_TIMEOUT_MS) || (i2c->ISR & I2C_ISR_NACKF)) goto fail;
        if (is_read) buf[i] = (uint8_t)i2c->RXDR;
        else i2c->TXDR = buf[i];
    }

    if (!wait_set(i2c, (autoend ? I2C_ISR_STOPF : I2C_ISR_TC) | I2C_ISR_NACKF, I2C_TIMEOUT_MS)) goto fail;
    if (i2c->ISR & I2C_ISR_NACKF) goto fail;
    if (autoend) i2c->ICR = I2C_ICR_STOPCF;
    return 1;

fail:
    i2c->CR2 |= I2C_CR2_STOP;
    wait_set(i2c, I2C_ISR_STOPF, I2C_TIMEOUT_MS);
    i2c->ICR = I2C_ICR_NACKCF | I2C_ICR_STOPCF;
    return 0;
}

int i2c_probe(I2C_TypeDef *i2c, uint8_t addr7) {
    if (!wait_not_busy(i2c, I2C_TIMEOUT_MS)) return 0;
    return transfer_phase(i2c, addr7, 0, 0, 0, 1);
}

int i2c_write(I2C_TypeDef *i2c, uint8_t addr7, const uint8_t *data, uint32_t len) {
    if (!wait_not_busy(i2c, I2C_TIMEOUT_MS)) return 0;
    return transfer_phase(i2c, addr7, (uint8_t *)data, len, 0, 1);
}

int i2c_read(I2C_TypeDef *i2c, uint8_t addr7, uint8_t *data, uint32_t len) {
    if (!wait_not_busy(i2c, I2C_TIMEOUT_MS)) return 0;
    return transfer_phase(i2c, addr7, data, len, 1, 1);
}

int i2c_write_read(I2C_TypeDef *i2c, uint8_t addr7,
                    const uint8_t *wbuf, uint32_t wlen,
                    uint8_t *rbuf, uint32_t rlen) {
    if (!wait_not_busy(i2c, I2C_TIMEOUT_MS)) return 0;
    if (!transfer_phase(i2c, addr7, (uint8_t *)wbuf, wlen, 0, 0)) return 0; /* no AUTOEND - repeated START follows */
    return transfer_phase(i2c, addr7, rbuf, rlen, 1, 1);
}
