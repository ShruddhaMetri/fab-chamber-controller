/*
 * spi.c
 * SPI2 master, mode 0 (clock idles low, data sampled on the rising edge),
 * 16 MHz / 4 = 4 MHz clock.
 *
 * SPI is full-duplex: every byte sent out on MOSI clocks a byte in on MISO.
 * To read, you send a dummy byte (0xFF) and keep what comes back.
 */
#include "spi.h"
#include "stm32f407_regs.h"

#define CS_PIN     12U     /* PB12, plain GPIO output */
#define SCK_PIN    13U
#define MISO_PIN   14U
#define MOSI_PIN   15U
#define AF5_SPI2   5UL
#define BR_DIV4    1UL     /* baud-rate bits 001 = clock / 4 */

void spi2_init(void)
{
    uint32_t pin;

    RCC_AHB1ENR |= RCC_AHB1ENR_GPIOBEN;
    RCC_APB1ENR |= RCC_APB1ENR_SPI2EN;

    /* PB13-15: alternate function 5 (SPI2). These pins are 8-15, so AFRH. */
    for (pin = SCK_PIN; pin <= MOSI_PIN; pin++) {
        GPIO_MODER(GPIOB_BASE) &= ~(3UL << (pin * 2U));
        GPIO_MODER(GPIOB_BASE) |=  (2UL << (pin * 2U));
        GPIO_AFRH(GPIOB_BASE)  &= ~(0xFUL    << ((pin - 8U) * 4U));
        GPIO_AFRH(GPIOB_BASE)  |=  (AF5_SPI2 << ((pin - 8U) * 4U));
    }

    /* PB12: chip select as a normal output, idle HIGH (= not selected) */
    GPIO_BSRR(GPIOB_BASE)  = (1UL << CS_PIN);
    GPIO_MODER(GPIOB_BASE) &= ~(3UL << (CS_PIN * 2U));
    GPIO_MODER(GPIOB_BASE) |=  (1UL << (CS_PIN * 2U));

    /* Master, clock / 4, mode 0, software chip select (SSM + SSI) */
    SPI2_CR1 = SPI_CR1_MSTR | (BR_DIV4 << SPI_CR1_BR_POS) | SPI_CR1_SSM | SPI_CR1_SSI;
}

void spi2_select(void)
{
    SPI2_CR1 |= SPI_CR1_SPE;                      /* enable SPI     */
    GPIO_BSRR(GPIOB_BASE) = (1UL << (CS_PIN + 16U)); /* CS low      */
}

void spi2_deselect(void)
{
    while (SPI2_SR & SPI_SR_BSY) {
        /* wait for the last bit to leave */
    }
    GPIO_BSRR(GPIOB_BASE) = (1UL << CS_PIN);      /* CS high        */
    SPI2_CR1 &= ~SPI_CR1_SPE;                     /* disable SPI: also marks the
                                                     end of the transaction */
}

uint8_t spi2_transfer(uint8_t out)
{
    while ((SPI2_SR & SPI_SR_TXE) == 0U) {
        /* wait until the transmit buffer is free */
    }
    *(volatile uint8_t *)&SPI2_DR = out;          /* 8-bit write */
    while ((SPI2_SR & SPI_SR_RXNE) == 0U) {
        /* wait for the byte clocked in at the same time */
    }
    return (uint8_t)SPI2_DR;
}
