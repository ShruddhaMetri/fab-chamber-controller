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
int      valve_set_percent(uint32_t percent);   /* refused (0) if closing during a fault */

/* Same, in per-mille (0-1000 = 0.0-100.0 %), used by the PID controller */
int      heater_set_permille(uint32_t permille);
int      valve_set_permille(uint32_t permille);  /* used by the pressure controller */

uint32_t heater_get_permille(void);
uint32_t valve_get_permille(void);

/* Safe state: heater off, valve fully open, gases off. ISR-safe. */
void     actuators_safe_state(void);

#endif /* ACTUATORS_H */
