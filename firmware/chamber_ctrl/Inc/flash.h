/*
 * flash.h
 * Micron MT25Q serial NOR flash on SPI2.
 * Rules of NOR flash:
 *   - Erasing sets every bit of a 4 KB sector to 1 (bytes read 0xFF).
 *   - Programming can only change bits from 1 to 0, at most 256 bytes
 *     (one page) at a time, and must stay inside that page.
 *   - Erase and program take time; the chip reports "busy" until done.
 */
#ifndef FLASH_H
#define FLASH_H

#include <stdint.h>

#define FLASH_PAGE_SIZE    256U
#define FLASH_SECTOR_SIZE  4096U

void     flash_init(void);
uint32_t flash_read_id(void);                          /* JEDEC ID: maker, type, size */
int      flash_is_busy(void);
void     flash_read(uint32_t addr, uint8_t *buf, uint32_t len);
void     flash_program(uint32_t addr, const uint8_t *data, uint32_t len); /* within one page */
void     flash_erase_sector(uint32_t addr);            /* starts the erase, does not wait */

#endif /* FLASH_H */
