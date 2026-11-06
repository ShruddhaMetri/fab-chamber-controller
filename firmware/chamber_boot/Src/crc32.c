/*
 * crc32.c
 * Bit-by-bit CRC-32 (reflected, polynomial 0xEDB88320). Slower than a
 * 1 KB lookup table but tiny - in a 32 KB bootloader every byte counts.
 */
#include "crc32.h"

uint32_t crc32_update(uint32_t crc, const uint8_t *data, uint32_t len)
{
    uint32_t i;
    int      b;

    crc = ~crc;                                  /* start value 0xFFFFFFFF */
    for (i = 0U; i < len; i++) {
        crc ^= data[i];
        for (b = 0; b < 8; b++) {
            crc = (crc & 1U) ? ((crc >> 1) ^ 0xEDB88320UL) : (crc >> 1);
        }
    }
    return ~crc;                                 /* final inversion */
}
