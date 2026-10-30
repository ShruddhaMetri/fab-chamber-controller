/*
 * recipe.c
 * Save: fill in the header, compute the CRC, write one EEPROM page,
 *       then read it back and compare (never trust a write blindly).
 * Load: read the page, check the magic number, then check the CRC.
 */
#include <string.h>
#include "recipe.h"
#include "eeprom.h"
#include "crc16.h"

#define RECIPE_MAGIC     0x52C3U       /* any fixed pattern unlikely to appear by chance */
#define RECIPE_VERSION   1U
#define RECIPE_BYTES     32U

/* The compiler checks the size at build time: if someone adds a field and
 * the struct stops being exactly one 32-byte page, the build fails. */
_Static_assert(sizeof(recipe_t) == RECIPE_BYTES, "recipe_t must be exactly 32 bytes");

static uint16_t slot_addr(uint32_t slot)
{
    return (uint16_t)(slot * RECIPE_BYTES);    /* slot 0 at 0x000, slot 1 at 0x020, ... */
}

static uint16_t recipe_crc(const recipe_t *r)
{
    /* CRC over everything except the CRC field itself (the last 2 bytes) */
    return crc16((const uint8_t *)r, RECIPE_BYTES - 2U);
}

int recipe_load(uint32_t slot, recipe_t *r)
{
    if (slot >= RECIPE_SLOTS) {
        return RECIPE_IO_ERROR;
    }
    if (eeprom_read(slot_addr(slot), (uint8_t *)r, RECIPE_BYTES) != 0) {
        return RECIPE_IO_ERROR;
    }
    if (r->magic != RECIPE_MAGIC) {
        return RECIPE_EMPTY;
    }
    if (r->crc != recipe_crc(r)) {
        return RECIPE_CORRUPT;
    }
    r->name[RECIPE_NAME_LEN - 1U] = '\0';      /* never trust stored text to be terminated */
    return RECIPE_OK;
}

int recipe_save(uint32_t slot, recipe_t *r)
{
    recipe_t check;

    if (slot >= RECIPE_SLOTS) {
        return RECIPE_IO_ERROR;
    }
    r->magic   = RECIPE_MAGIC;
    r->version = RECIPE_VERSION;
    r->slot    = (uint8_t)slot;
    memset(r->reserved, 0, sizeof(r->reserved));
    r->crc     = recipe_crc(r);

    if (eeprom_write(slot_addr(slot), (const uint8_t *)r, RECIPE_BYTES) != 0) {
        return RECIPE_IO_ERROR;
    }
    /* Read-after-write verification */
    if (eeprom_read(slot_addr(slot), (uint8_t *)&check, RECIPE_BYTES) != 0) {
        return RECIPE_IO_ERROR;
    }
    if (memcmp(&check, r, RECIPE_BYTES) != 0) {
        return RECIPE_VERIFY_FAIL;
    }
    return RECIPE_OK;
}

int recipe_erase(uint32_t slot)
{
    uint8_t blank[RECIPE_BYTES];
    if (slot >= RECIPE_SLOTS) {
        return RECIPE_IO_ERROR;
    }
    memset(blank, 0xFF, sizeof(blank));         /* erased EEPROM reads 0xFF */
    return (eeprom_write(slot_addr(slot), blank, RECIPE_BYTES) == 0) ? RECIPE_OK : RECIPE_IO_ERROR;
}
