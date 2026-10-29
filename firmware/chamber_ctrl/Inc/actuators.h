/*
 * actuators.h
 * Chamber actuators, in engineering units (percent).
 *   Heater        : PWM channel 1 (PA6), 0% = off
 *   Throttle valve: PWM channel 2 (PA7), 0% = closed, 100% = fully open
 */
#ifndef ACTUATORS_H
#define ACTUATORS_H

#include <stdint.h>

void     actuators_init(void);

/* Return 1 if applied, 0 if refused (heater is locked out while a fault is active) */
int      heater_set_percent(uint32_t percent);
void     valve_set_percent(uint32_t percent);

uint32_t heater_get_permille(void);
uint32_t valve_get_permille(void);

/* Safe state: heater off, valve fully open (pump the chamber out). ISR-safe. */
void     actuators_safe_state(void);

#endif /* ACTUATORS_H */
