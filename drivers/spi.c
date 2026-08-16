#include "spi.h"

static void spi_gpio_init(SPI_TypeDef *spi) {
    if (spi == SPI2) {
        /* PB13 = SCK, PB14 = MISO, PB15 = MOSI, AF5 - to the Micro SD
         * shield. CS (PB12) is a plain GPIO, configured by the device
         * layer (devices/sd_spi.c), not here. */
        RCC->AHB2ENR |= RCC_AHB2ENR_GPIOBEN;
        GPIOB->MODER &= ~((3UL << (13 * 2)) | (3UL << (14 * 2)) | (3UL << (15 * 2)));
        GPIOB->MODER |= (2UL << (13 * 2)) | (2UL << (14 * 2)) | (2UL << (15 * 2));
        GPIOB->AFR[1] &= ~((0xFUL << ((13 - 8) * 4)) | (0xFUL << ((14 - 8) * 4)) | (0xFUL << ((15 - 8) * 4)));
        GPIOB->AFR[1] |= (5UL << ((13 - 8) * 4)) | (5UL << ((14 - 8) * 4)) | (5UL << ((15 - 8) * 4));
    }
}

void spi_init(SPI_TypeDef *spi, uint32_t prescaler) {
    spi_gpio_init(spi);

    if (spi == SPI2) {
        RCC->APB1ENR1 |= RCC_APB1ENR1_SPI2EN;
    }

    /* Software NSS, forced high (SSI=1) since we never let the peripheral
     * own chip select - a floating/low NSS in software-managed mode would
     * force the peripheral into multi-master fault state. */
    spi->CR1 = SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI | prescaler;
    spi->CR2 = SPI_CR2_DS_8BIT | SPI_CR2_FRXTH;
    spi->CR1 |= SPI_CR1_SPE;
}

void spi_set_prescaler(SPI_TypeDef *spi, uint32_t prescaler) {
    spi->CR1 &= ~SPI_CR1_SPE;
    spi->CR1 = (spi->CR1 & ~(7UL << SPI_CR1_BR_Pos)) | prescaler;
    spi->CR1 |= SPI_CR1_SPE;
}

uint8_t spi_transfer(SPI_TypeDef *spi, uint8_t out_byte) {
    while (!(spi->SR & SPI_SR_TXE)) {}
    *(volatile uint8_t *)&spi->DR = out_byte;
    while (!(spi->SR & SPI_SR_RXNE)) {}
    return *(volatile uint8_t *)&spi->DR;
}
