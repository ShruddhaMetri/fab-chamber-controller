/*
 * eeprom.c
 * Reading is simple: send the 2-byte memory address, then read.
 * Writing has two rules:
 *   1. A write may not cross a 32-byte page boundary (it would wrap around
 *      inside the page), so long writes are split into page-sized pieces.
 *   2. After each write the chip is busy for up to 5 ms and does not answer
 *      (NACK). We poll it until it acknowledges again ("ACK polling").
 */
#include "eeprom.h"
#include "i2c.h"
#include "systick.h"

#define EEPROM_ADDR        0x50U
#define WRITE_CYCLE_MS     10U      /* datasheet max is 5 ms; allow margin */

int eeprom_read(uint16_t mem_addr, uint8_t *buf, uint32_t len)
{
    uint8_t a[2];
    if ((mem_addr + len) > EEPROM_SIZE) {
        return I2C_ERR_NACK;
    }
    a[0] = (uint8_t)(mem_addr >> 8);           /* high byte first */
    a[1] = (uint8_t)(mem_addr & 0xFFU);
    return i2c1_write_read(EEPROM_ADDR, a, 2U, buf, len);
}

static int wait_write_done(void)
{
    uint32_t start = millis();
    uint8_t  dummy = 0U;
    /* While busy the chip NACKs its address; a successful 1-byte read means ready */
    while ((millis() - start) < WRITE_CYCLE_MS) {
        uint8_t a[2] = {0U, 0U};
        if (i2c1_write_read(EEPROM_ADDR, a, 2U, &dummy, 1U) == I2C_OK) {
            return I2C_OK;
        }
    }
    return I2C_ERR_TIMEOUT;
}

int eeprom_write(uint16_t mem_addr, const uint8_t *data, uint32_t len)
{
    uint8_t  buf[2U + EEPROM_PAGE_SIZE];       /* address + one page of data */
    uint32_t i;
    int      err;

    if ((mem_addr + len) > EEPROM_SIZE) {
        return I2C_ERR_NACK;
    }
    while (len > 0U) {
        /* how many bytes fit before the end of the current page */
        uint32_t room  = EEPROM_PAGE_SIZE - (mem_addr % EEPROM_PAGE_SIZE);
        uint32_t chunk = (len < room) ? len : room;

        buf[0] = (uint8_t)(mem_addr >> 8);
        buf[1] = (uint8_t)(mem_addr & 0xFFU);
        for (i = 0U; i < chunk; i++) {
            buf[2U + i] = data[i];
        }
        err = i2c1_write(EEPROM_ADDR, buf, 2U + chunk);
        if (err != I2C_OK) { return err; }
        err = wait_write_done();
        if (err != I2C_OK) { return err; }

        mem_addr += (uint16_t)chunk;
        data     += chunk;
        len      -= chunk;
    }
    return I2C_OK;
}
