/*
 * fakes.h
 * "Fakes" replace the hardware so the real firmware logic can run on a PC.
 * The module under test (for example temp_ctrl.c) is compiled UNCHANGED;
 * only the functions it calls (sensors_temp_dc, heater_set_permille, ...)
 * are provided here instead of by the real drivers.
 */
#ifndef FAKES_H
#define FAKES_H

#include <stdint.h>

/* ---- Chamber thermal plant, the same model as renode/chamber_model.py ---- */
extern double   fake_temp_c;          /* chamber temperature, C              */
extern uint32_t fake_heater_permille; /* last heater command                  */
extern int      fake_fault;           /* 1 = a safety fault is latched        */

void fake_plant_reset(void);
void fake_plant_step(void);           /* advance 100 ms with the current heater */

/* ---- I2C EEPROM, a 4 KB byte array ---- */
extern uint8_t fake_eeprom[4096];
extern int     fake_eeprom_fail;      /* 1 = every access returns an error    */
extern int     fake_eeprom_corrupt_write;  /* 1 = flip one bit while writing */

#endif /* FAKES_H */
