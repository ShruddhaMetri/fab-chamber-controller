/*
 * iflash.h
 * Driver for the STM32's INTERNAL flash (where the program itself lives).
 * Not to be confused with flash.h, the external SPI flash for the data log.
 *
 * Flash rules: an erased bit is 1. Programming can only change 1 -> 0.
 * To get 1s back, a whole SECTOR must be erased. Writing is locked after
 * reset and must be unlocked with two "key" values first.
 */
#ifndef IFLASH_H
#define IFLASH_H

#include <stdint.h>

void iflash_unlock(void);
void iflash_lock(void);
int  iflash_erase_sector(uint32_t sector);                 /* 1 = OK */
int  iflash_program_word(uint32_t addr, uint32_t word);    /* 1 = OK and verified */

#endif /* IFLASH_H */
