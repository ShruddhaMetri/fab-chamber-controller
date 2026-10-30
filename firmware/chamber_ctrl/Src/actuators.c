/*
 * actuators.c
 * Turns "heater 40 %" into a PWM duty, and enforces the safety lock-out.
 * This is a service layer: it knows about the chamber, not about registers.
 */
#include "actuators.h"
#include "pwm.h"
#include "safety.h"
#include "gas.h"
#include "irq.h"

#define HEATER_CH   PWM_CH1
#define VALVE_CH    PWM_CH2

void actuators_init(void)
{
    pwm_init();
    pwm_set_duty(HEATER_CH, 0U);    /* heater off at power-on   */
    pwm_set_duty(VALVE_CH,  0U);    /* valve closed at power-on */
}

int heater_set_percent(uint32_t percent)
{
    if (percent > 100U) {
        percent = 100U;
    }
    return heater_set_permille(percent * 10U);   /* percent -> per-mille */
}

int heater_set_permille(uint32_t permille)
{
    int ok = 1;

    if (permille > PWM_MAX_DUTY) {
        permille = PWM_MAX_DUTY;
    }
    /* Critical section: the fault check and the register write must happen
     * together. Otherwise an e-stop interrupt between them could switch the
     * heater off, and this code would then switch it back on. */
    irq_disable();
    if (safety_is_faulted() && (permille > 0U)) {
        ok = 0;                       /* refuse to heat while a fault is latched */
    } else {
        pwm_set_duty(HEATER_CH, permille);
    }
    irq_enable();
    return ok;
}

int valve_set_percent(uint32_t percent)
{
    if (percent > 100U) {
        percent = 100U;
    }
    return valve_set_permille(percent * 10U);
}

/* The valve is the mirror image of the heater: its SAFE position is fully
 * open (the pump removes the process gas). So during a fault the valve may
 * be opened but never closed, checked together with the write as above. */
int valve_set_permille(uint32_t permille)
{
    int ok = 1;

    if (permille > PWM_MAX_DUTY) {
        permille = PWM_MAX_DUTY;
    }
    irq_disable();
    if (safety_is_faulted() && (permille < PWM_MAX_DUTY)) {
        ok = 0;                       /* refuse to close while a fault is latched */
    } else {
        pwm_set_duty(VALVE_CH, permille);
    }
    irq_enable();
    return ok;
}

uint32_t heater_get_permille(void) { return pwm_get_duty(HEATER_CH); }
uint32_t valve_get_permille(void)  { return pwm_get_duty(VALVE_CH);  }

/* A few register writes: fast and safe to call from an interrupt */
void actuators_safe_state(void)
{
    pwm_set_duty(HEATER_CH, 0U);              /* heater off            */
    pwm_set_duty(VALVE_CH,  PWM_MAX_DUTY);    /* valve fully open      */
    gas_all_off();                            /* process gases off     */
}
