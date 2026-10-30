/*
 * temp_ctrl.h
 * Closed-loop chamber temperature control: a PID controller that reads the
 * temperature every 100 ms and sets the heater power to reach the setpoint.
 *
 *   MANUAL mode : the operator sets the heater directly ("heater N")
 *   AUTO mode   : the PID sets the heater ("temp N", N > 0)
 *
 * Gains are integers in thousandths ("milli") to avoid floating point:
 *   KP_M = 2000  means  Kp = 2.000 % heater per degree C of error
 *   KI_M = 200   means  Ki = 0.200 % per degree C per second
 *   KD_M = 2000  means  Kd = 2.000 % per (degree C per second)
 */
#ifndef TEMP_CTRL_H
#define TEMP_CTRL_H

#include <stdint.h>

void     tc_init(void);
void     tc_poll(void);                       /* every SENSOR_PERIOD_MS, after sensors_poll() */

/* setpoint in whole degrees C; 0 = switch off AUTO (heater to 0, MANUAL mode).
 * Returns 1 if accepted, 0 if refused (out of range or fault active). */
int      tc_set_setpoint_c(uint32_t c);
int      tc_is_auto(void);
uint32_t tc_setpoint_dc(void);                /* tenths of a degree */

#define TC_GAIN_MAX  50000U                  /* = 50.000, upper limit for each gain */

void     tc_set_gains(uint32_t kp_m, uint32_t ki_m, uint32_t kd_m);
void     tc_get_gains(uint32_t *kp_m, uint32_t *ki_m, uint32_t *kd_m);

/* Latest P, I and D contributions and total output, in per-mille (0.1 %) */
void     tc_get_terms(int32_t *p, int32_t *i, int32_t *d, int32_t *out);

#endif /* TEMP_CTRL_H */
