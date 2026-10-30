/*
 * recipe.h
 * Process recipes stored in the I2C EEPROM, so they survive power-off.
 * 4 slots of 32 bytes each (one EEPROM page per recipe), each protected
 * by a magic number and a CRC-16.
 */
#ifndef RECIPE_H
#define RECIPE_H

#include <stdint.h>

#define RECIPE_SLOTS      4U
#define RECIPE_NAME_LEN   12U          /* including the terminating '\0' */

/* Exactly 32 bytes, no hidden padding: every field sits on a natural boundary */
typedef struct {
    uint16_t magic;                    /* RECIPE_MAGIC when the slot holds a recipe */
    uint8_t  version;                  /* layout version, for future changes        */
    uint8_t  slot;                     /* which slot it was saved to                */
    char     name[RECIPE_NAME_LEN];
    uint16_t temp_c;                   /* process temperature, degrees C            */
    uint16_t pressure_mtorr;           /* process pressure, milli-Torr              */
    uint16_t gas1_sccm;                /* SiH4 flow                                 */
    uint16_t gas2_sccm;                /* N2O flow                                  */
    uint16_t time_s;                   /* process time, seconds                     */
    uint8_t  reserved[4];
    uint16_t crc;                      /* CRC-16 of the 30 bytes above              */
} recipe_t;

#define RECIPE_OK          0
#define RECIPE_EMPTY       1           /* slot never written or erased  */
#define RECIPE_CORRUPT     2           /* CRC check failed              */
#define RECIPE_IO_ERROR    3           /* EEPROM did not respond        */
#define RECIPE_VERIFY_FAIL 4           /* read-back after save differed */

int recipe_load(uint32_t slot, recipe_t *r);
int recipe_save(uint32_t slot, recipe_t *r);   /* fills in magic, version, slot, crc */
int recipe_erase(uint32_t slot);

#endif /* RECIPE_H */
