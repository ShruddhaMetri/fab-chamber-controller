/*
 * pwm.h
 * PWM driver on TIM3, two channels, 1 kHz.
 *   Channel 1 : PA6 (AF2)
 *   Channel 2 : PA7 (AF2)
 * Duty is given in per-mille (0-1000 = 0.0% - 100.0%) for 0.1% resolution.
 */
#ifndef PWM_H
#define PWM_H

#include <stdint.h>

#define PWM_CH1        1U
#define PWM_CH2        2U
#define PWM_MAX_DUTY   1000U    /* = 100.0 % */

void     pwm_init(void);
void     pwm_set_duty(uint32_t channel, uint32_t permille);
uint32_t pwm_get_duty(uint32_t channel);

#endif /* PWM_H */
