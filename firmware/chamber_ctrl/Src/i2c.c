/*
 * i2c.c
 * I2C1 master, polled, with timeouts.
 *
 * One I2C transaction:
 *   START -> address + R/W bit -> device ACKs -> data bytes (each ACKed) -> STOP
 * A "write then read" uses a repeated START between the two parts.
 */
#include "i2c.h"
#include "stm32f407_regs.h"
#include "systick.h"

#define SCL_PIN         6U      /* PB6 */
#define SDA_PIN         7U      /* PB7 */
#define AF4_I2C1        4UL
#define I2C_TIMEOUT_MS  10U

/* Wait until a status-register flag is set; give up after a timeout */
static int wait_flag(uint32_t flag)
{
    uint32_t start = millis();
    while ((I2C1_SR1 & flag) == 0U) {
        if (I2C1_SR1 & I2C_SR1_AF) {           /* device said NACK */
            I2C1_SR1 &= ~I2C_SR1_AF;
            return I2C_ERR_NACK;
        }
        if ((millis() - start) > I2C_TIMEOUT_MS) {
            return I2C_ERR_TIMEOUT;
        }
    }
    return I2C_OK;
}

static int fail(int err)
{
    I2C1_CR1 |= I2C_CR1_STOP;                   /* always release the bus */
    return err;
}

/* START, then send the address with the R/W bit (0 = write, 1 = read) */
static int start_and_address(uint8_t addr, uint8_t read)
{
    int err;

    I2C1_CR1 |= I2C_CR1_START;
    err = wait_flag(I2C_SR1_SB);
    if (err != I2C_OK) { return err; }

    I2C1_DR = (uint32_t)((addr << 1) | read);   /* 7-bit address + R/W */
    err = wait_flag(I2C_SR1_ADDR);
    if (err != I2C_OK) { return err; }

    (void)I2C1_SR1;                             /* reading SR1 then SR2 */
    (void)I2C1_SR2;                             /* clears the ADDR flag */
    return I2C_OK;
}

static int send_bytes(const uint8_t *data, uint32_t len)
{
    uint32_t i;
    int err;
    for (i = 0U; i < len; i++) {
        err = wait_flag(I2C_SR1_TXE);
        if (err != I2C_OK) { return err; }
        I2C1_DR = data[i];
    }
    return wait_flag(I2C_SR1_BTF);              /* last byte fully sent */
}

void i2c1_init(void)
{
    /* 1. Clocks */
    RCC_AHB1ENR |= RCC_AHB1ENR_GPIOBEN;
    RCC_APB1ENR |= RCC_APB1ENR_I2C1EN;

    /* 2. PB6, PB7: alternate function 4, open-drain, pull-up.
     *    Open-drain: a pin can only pull the line LOW; a resistor pulls it HIGH.
     *    That lets several devices share the same two wires safely. */
    GPIO_MODER(GPIOB_BASE)  &= ~((3UL << (SCL_PIN * 2U)) | (3UL << (SDA_PIN * 2U)));
    GPIO_MODER(GPIOB_BASE)  |=  ((2UL << (SCL_PIN * 2U)) | (2UL << (SDA_PIN * 2U)));
    GPIO_OTYPER(GPIOB_BASE) |=  ((1UL << SCL_PIN) | (1UL << SDA_PIN));
    GPIO_PUPDR(GPIOB_BASE)  &= ~((3UL << (SCL_PIN * 2U)) | (3UL << (SDA_PIN * 2U)));
    GPIO_PUPDR(GPIOB_BASE)  |=  ((1UL << (SCL_PIN * 2U)) | (1UL << (SDA_PIN * 2U)));
    GPIO_AFRL(GPIOB_BASE)   &= ~((0xFUL << (SCL_PIN * 4U)) | (0xFUL << (SDA_PIN * 4U)));
    GPIO_AFRL(GPIOB_BASE)   |=  ((AF4_I2C1 << (SCL_PIN * 4U)) | (AF4_I2C1 << (SDA_PIN * 4U)));

    /* 3. Reset the peripheral, then set timing for 100 kHz "standard mode"
     *    FREQ  = APB1 clock in MHz         = 16
     *    CCR   = 16 MHz / (2 x 100 kHz)    = 80
     *    TRISE = (1000 ns / 62.5 ns) + 1   = 17  (max rise time + 1) */
    I2C1_CR1 = I2C_CR1_SWRST;
    I2C1_CR1 = 0U;
    I2C1_CR2   = 16U;
    I2C1_CCR   = 80U;
    I2C1_TRISE = 17U;
    I2C1_CR1   = I2C_CR1_PE;
}

int i2c1_write(uint8_t addr, const uint8_t *data, uint32_t len)
{
    int err = start_and_address(addr, 0U);
    if (err == I2C_OK) {
        err = send_bytes(data, len);
    }
    if (err != I2C_OK) {
        return fail(err);
    }
    I2C1_CR1 |= I2C_CR1_STOP;
    return I2C_OK;
}

int i2c1_write_read(uint8_t addr, const uint8_t *wdata, uint32_t wlen,
                    uint8_t *rdata, uint32_t rlen)
{
    uint32_t i;
    int err;

    /* Part 1: write (e.g. the memory address inside the EEPROM) */
    err = start_and_address(addr, 0U);
    if (err == I2C_OK) {
        err = send_bytes(wdata, wlen);
    }
    if (err != I2C_OK) {
        return fail(err);
    }

    /* Part 2: repeated START, then read. ACK every byte except the last one:
     * the NACK on the last byte tells the device "that's enough". */
    I2C1_CR1 |= I2C_CR1_ACK;
    err = start_and_address(addr, 1U);
    if (err != I2C_OK) {
        return fail(err);
    }
    for (i = 0U; i < rlen; i++) {
        if (i == (rlen - 1U)) {
            I2C1_CR1 &= ~I2C_CR1_ACK;           /* NACK the last byte */
            I2C1_CR1 |= I2C_CR1_STOP;           /* then STOP          */
        }
        err = wait_flag(I2C_SR1_RXNE);
        if (err != I2C_OK) {
            return fail(err);
        }
        rdata[i] = (uint8_t)I2C1_DR;
    }
    return I2C_OK;
}
