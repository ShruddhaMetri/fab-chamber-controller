/*
 * pwm.c
 * TIM3 PWM driver.
 *
 * Timer clock = 16 MHz (APB1 runs at the 16 MHz HSI after reset).
 *   Prescaler 15  -> 16 MHz / (15 + 1)   = 1 MHz   (1 count every 1 us)
 *   Period   999  -> 1 MHz  / (999 + 1)  = 1 kHz   (one PWM cycle every 1 ms)
 * So the compare value (CCR) is the ON-time in microseconds, 0..1000,
 * which is exactly the duty in per-mille.
 */
#include "pwm.h"
#include "stm32f407_regs.h"

#define PWM_PRESCALER   15U     /* 16 MHz -> 1 MHz      */
#define PWM_PERIOD      999U    /* 1000 counts per cycle */

#define PWM_CH1_PIN     6U      /* PA6 = TIM3_CH1 */
#define PWM_CH2_PIN     7U      /* PA7 = TIM3_CH2 */
#define AF2_TIM3        2UL

static void pin_to_tim3(uint32_t pin)
{
    GPIO_MODER(GPIOA_BASE) &= ~(3UL << (pin * 2U));
    GPIO_MODER(GPIOA_BASE) |=  (2UL << (pin * 2U));       /* 10 = alternate function */
    GPIO_AFRL(GPIOA_BASE)  &= ~(0xFUL  << (pin * 4U));
    GPIO_AFRL(GPIOA_BASE)  |=  (AF2_TIM3 << (pin * 4U));  /* AF2 = TIM3              */
}

void pwm_init(void)
{
    /* 1. Clocks: GPIOA for the pins, TIM3 for the timer */
    RCC_AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC_APB1ENR |= RCC_APB1ENR_TIM3EN;

    /* 2. Hand PA6 and PA7 to TIM3 */
    pin_to_tim3(PWM_CH1_PIN);
    pin_to_tim3(PWM_CH2_PIN);

    /* 3. Timing: 1 MHz count rate, 1000 counts per cycle = 1 kHz PWM */
    TIM3_PSC = PWM_PRESCALER;
    TIM3_ARR = PWM_PERIOD;

    /* 4. Both channels in PWM mode 1 with buffered (preloaded) compare values:
     *    output is HIGH while CNT < CCR, LOW for the rest of the cycle */
    TIM3_CCMR1 = (TIM_CCMR_PWM1 << TIM_CCMR1_OC1M_POS) | TIM_CCMR1_OC1PE
               | (TIM_CCMR_PWM1 << TIM_CCMR1_OC2M_POS) | TIM_CCMR1_OC2PE;

    /* 5. Start with 0% duty: actuators off */
    TIM3_CCR1 = 0U;
    TIM3_CCR2 = 0U;

    /* 6. Connect the channels to their pins */
    TIM3_CCER = TIM_CCER_CC1E | TIM_CCER_CC2E;

    /* 7. Load all buffered values now, then start counting */
    TIM3_EGR = TIM_EGR_UG;
    TIM3_CR1 = TIM_CR1_ARPE | TIM_CR1_CEN;
}

void pwm_set_duty(uint32_t channel, uint32_t permille)
{
    if (permille > PWM_MAX_DUTY) {
        permille = PWM_MAX_DUTY;            /* clamp: never more than 100% */
    }
    if (channel == PWM_CH1) {
        TIM3_CCR1 = permille;
    } else if (channel == PWM_CH2) {
        TIM3_CCR2 = permille;
    }
}

uint32_t pwm_get_duty(uint32_t channel)
{
    if (channel == PWM_CH1) {
        return TIM3_CCR1;
    }
    if (channel == PWM_CH2) {
        return TIM3_CCR2;
    }
    return 0U;
}
