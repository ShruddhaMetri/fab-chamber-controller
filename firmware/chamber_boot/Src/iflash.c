/*
 * iflash.c
 * Internal flash erase / program, register level.
 *
 * While the flash is busy erasing or programming, the CPU cannot fetch
 * instructions from it and simply stalls (this chip has one flash bank).
 * Programming a word takes ~16 us; erasing a 128 KB sector takes 1-2 s,
 * which is longer than our 1 s watchdog - so the caller must stretch the
 * watchdog before erasing a big sector.
 */
#include "iflash.h"

#define FLASH_BASE_R   0x40023C00UL
#define FLASH_KEYR     (*(volatile uint32_t *)(FLASH_BASE_R + 0x04))
#define FLASH_SR       (*(volatile uint32_t *)(FLASH_BASE_R + 0x0C))
#define FLASH_CR       (*(volatile uint32_t *)(FLASH_BASE_R + 0x10))

#define KEY1           0x45670123UL
#define KEY2           0xCDEF89ABUL

#define CR_PG          (1UL << 0)     /* program                       */
#define CR_SER         (1UL << 1)     /* sector erase                  */
#define CR_SNB_POS     3U             /* sector number, bits 3-6       */
#define CR_PSIZE_X32   (2UL << 8)     /* program 32 bits at a time (needs 2.7-3.6 V supply) */
#define CR_STRT        (1UL << 16)    /* start the erase               */
#define CR_LOCK        (1UL << 31)
#define SR_BSY         (1UL << 16)
#define SR_ERRORS      (0xF0UL)       /* WRPERR, PGAERR, PGPERR, PGSERR */

static int wait_ready(void)
{
    uint32_t n;
    for (n = 0U; n < 50000000UL; n++) {
        if ((FLASH_SR & SR_BSY) == 0U) {
            break;
        }
    }
    if (FLASH_SR & SR_ERRORS) {
        FLASH_SR = SR_ERRORS;          /* write 1 to clear */
        return 0;
    }
    return 1;
}

void iflash_unlock(void)
{
    if (FLASH_CR & CR_LOCK) {
        FLASH_KEYR = KEY1;             /* the exact two values, in this order */
        FLASH_KEYR = KEY2;
    }
}

void iflash_lock(void)
{
    FLASH_CR = CR_LOCK;
}

int iflash_erase_sector(uint32_t sector)
{
    int ok;
    if (!wait_ready()) { return 0; }
    FLASH_CR = CR_PSIZE_X32 | CR_SER | (sector << CR_SNB_POS);
    FLASH_CR = CR_PSIZE_X32 | CR_SER | (sector << CR_SNB_POS) | CR_STRT;
    ok = wait_ready();
    FLASH_CR = 0U;                     /* clear SER so the next operation starts clean */
    return ok;
}

int iflash_program_word(uint32_t addr, uint32_t word)
{
    int ok;
    if (!wait_ready()) { return 0; }
    FLASH_CR = CR_PSIZE_X32 | CR_PG;
    *(volatile uint32_t *)addr = word; /* the write itself starts programming */
    ok = wait_ready();
    FLASH_CR = 0U;
    return ok && (*(volatile uint32_t *)addr == word);   /* read back to verify */
}
