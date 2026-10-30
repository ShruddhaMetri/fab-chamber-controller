/*
 * crc16.h
 * CRC-16/CCITT checksum (polynomial 0x1021, start value 0xFFFF).
 * Used to detect corrupted data in the EEPROM (and later, firmware images).
 */
#ifndef CRC16_H
#define CRC16_H

#include <stdint.h>

uint16_t crc16(const uint8_t *data, uint32_t len);

#endif /* CRC16_H */
