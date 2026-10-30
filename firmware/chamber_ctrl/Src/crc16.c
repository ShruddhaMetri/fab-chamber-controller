/*
 * crc16.c
 * Bit-by-bit CRC-16/CCITT. For each byte: mix it into the top of the CRC,
 * then for each of the 8 bits, shift left and, if a 1 fell out of the top,
 * XOR with the polynomial. Small and simple; a lookup table would be faster.
 * Check value: crc16("123456789") = 0x29B1.
 */
#include "crc16.h"

uint16_t crc16(const uint8_t *data, uint32_t len)
{
    uint16_t crc = 0xFFFFU;
    uint32_t i;
    uint32_t bit;

    for (i = 0U; i < len; i++) {
        crc ^= (uint16_t)((uint16_t)data[i] << 8);
        for (bit = 0U; bit < 8U; bit++) {
            if (crc & 0x8000U) {
                crc = (uint16_t)((crc << 1) ^ 0x1021U);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}
