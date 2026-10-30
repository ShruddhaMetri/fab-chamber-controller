/*
 * flash.c
 * Every flash operation is: CS low -> command byte -> (3 address bytes)
 * -> data bytes -> CS high.  Command codes come from the chip's datasheet.
 */
#include "flash.h"
#include "spi.h"

#define CMD_WRITE_ENABLE   0x06U   /* must precede every program or erase */
#define CMD_READ_STATUS    0x05U
#define CMD_READ_DATA      0x03U
#define CMD_PAGE_PROGRAM   0x02U
#define CMD_SECTOR_ERASE   0x20U   /* 4 KB */
#define CMD_READ_ID        0x9FU
#define STATUS_WIP         0x01U   /* write in progress = busy */

static void send_addr(uint32_t addr)
{
    (void)spi2_transfer((uint8_t)(addr >> 16));   /* 24-bit address, high byte first */
    (void)spi2_transfer((uint8_t)(addr >> 8));
    (void)spi2_transfer((uint8_t)addr);
}

static void write_enable(void)
{
    spi2_select();
    (void)spi2_transfer(CMD_WRITE_ENABLE);
    spi2_deselect();
}

void flash_init(void)
{
    spi2_init();
}

uint32_t flash_read_id(void)
{
    uint32_t id;
    spi2_select();
    (void)spi2_transfer(CMD_READ_ID);
    id  = (uint32_t)spi2_transfer(0xFFU) << 16;   /* manufacturer   */
    id |= (uint32_t)spi2_transfer(0xFFU) << 8;    /* memory type    */
    id |= (uint32_t)spi2_transfer(0xFFU);         /* capacity code  */
    spi2_deselect();
    return id;
}

int flash_is_busy(void)
{
    uint8_t status;
    spi2_select();
    (void)spi2_transfer(CMD_READ_STATUS);
    status = spi2_transfer(0xFFU);
    spi2_deselect();
    return (status & STATUS_WIP) != 0U;
}

void flash_read(uint32_t addr, uint8_t *buf, uint32_t len)
{
    uint32_t i;
    spi2_select();
    (void)spi2_transfer(CMD_READ_DATA);
    send_addr(addr);
    for (i = 0U; i < len; i++) {
        buf[i] = spi2_transfer(0xFFU);            /* dummy out, data in */
    }
    spi2_deselect();
}

void flash_program(uint32_t addr, const uint8_t *data, uint32_t len)
{
    uint32_t i;
    write_enable();
    spi2_select();
    (void)spi2_transfer(CMD_PAGE_PROGRAM);
    send_addr(addr);
    for (i = 0U; i < len; i++) {
        (void)spi2_transfer(data[i]);
    }
    spi2_deselect();                              /* programming starts now */
}

void flash_erase_sector(uint32_t addr)
{
    write_enable();
    spi2_select();
    (void)spi2_transfer(CMD_SECTOR_ERASE);
    send_addr(addr);
    spi2_deselect();                              /* erase starts now (~50-400 ms) */
}
