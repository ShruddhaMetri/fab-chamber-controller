/*
 * crc32.h
 * CRC-32 (the same one used by Ethernet, ZIP and PNG), for whole firmware
 * images. Check value: crc32("123456789") = 0xCBF43926.
 * (Recipes in Step 9 use the shorter CRC-16: fine for 30 bytes; for 60 KB
 *  a 32-bit CRC makes an undetected corruption far less likely.)
 */
#ifndef CRC32_H
#define CRC32_H

#include <stdint.h>

/* Start with crc = 0, feed the data in pieces if you like */
uint32_t crc32_update(uint32_t crc, const uint8_t *data, uint32_t len);

#endif /* CRC32_H */
