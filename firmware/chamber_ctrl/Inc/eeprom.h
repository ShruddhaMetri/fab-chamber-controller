/*
 * eeprom.h
 * 24C32-style I2C EEPROM: 4 KB, bus address 0x50, 32-byte pages,
 * 2-byte memory addresses. Keeps its contents with the power off.
 */
#ifndef EEPROM_H
#define EEPROM_H

#include <stdint.h>

#define EEPROM_SIZE        4096U
#define EEPROM_PAGE_SIZE   32U

int eeprom_read(uint16_t mem_addr, uint8_t *buf, uint32_t len);
int eeprom_write(uint16_t mem_addr, const uint8_t *data, uint32_t len);

#endif /* EEPROM_H */
