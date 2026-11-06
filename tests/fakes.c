/*
 * fakes.c
 * PC replacements for the hardware-facing functions used by the modules
 * under test.
 */
#include <string.h>
#include "fakes.h"

/* ---------------- Thermal plant + heater + sensor ---------------- */
double   fake_temp_c          = 25.0;
uint32_t fake_heater_permille = 0U;
int      fake_fault           = 0;

void fake_plant_reset(void)
{
    fake_temp_c          = 25.0;
    fake_heater_permille = 0U;
    fake_fault           = 0;
}

void fake_plant_step(void)
{
    /* dT/dt = (K * heater% - (T - 25)) / TAU, K = 6 C/%, TAU = 30 s, dt = 0.1 s */
    double heater_pct = fake_heater_permille / 10.0;
    fake_temp_c += 0.1 / 30.0 * (6.0 * heater_pct - (fake_temp_c - 25.0));
}

/* The real one converts an ADC reading; this one reads the plant */
uint32_t sensors_temp_dc(void)
{
    return (uint32_t)(fake_temp_c * 10.0 + 0.5);
}

/* Same rule as the real driver: no heating while a fault is latched */
int heater_set_permille(uint32_t permille)
{
    if (permille > 1000U) { permille = 1000U; }
    if (fake_fault && permille > 0U) {
        return 0;
    }
    fake_heater_permille = permille;
    return 1;
}

int safety_is_faulted(void) { return fake_fault; }

/* Console output is not needed in tests */
void uart2_puts(const char *s) { (void)s; }

/* FreeRTOS critical sections: nothing to protect on a single-threaded PC */
void vPortEnterCritical(void) { }
void vPortExitCritical(void)  { }
void rtos_assert_failed(const char *file, int line) { (void)file; (void)line; }

/* ---------------- EEPROM ---------------- */
uint8_t fake_eeprom[4096];
int     fake_eeprom_fail          = 0;
int     fake_eeprom_corrupt_write = 0;

int eeprom_read(uint16_t mem_addr, uint8_t *buf, uint32_t len)
{
    if (fake_eeprom_fail || (uint32_t)mem_addr + len > sizeof(fake_eeprom)) {
        return -1;
    }
    memcpy(buf, &fake_eeprom[mem_addr], len);
    return 0;
}

int eeprom_write(uint16_t mem_addr, const uint8_t *data, uint32_t len)
{
    if (fake_eeprom_fail || (uint32_t)mem_addr + len > sizeof(fake_eeprom)) {
        return -1;
    }
    memcpy(&fake_eeprom[mem_addr], data, len);
    if (fake_eeprom_corrupt_write && len > 5U) {
        fake_eeprom[mem_addr + 5U] ^= 0x01U;     /* a bad write the chip did not report */
    }
    return 0;
}
