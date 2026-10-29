/*
 * gpio.c
 * LED driver implementation (port D, output mode).
 */
#include "gpio.h"
#include "stm32f407_regs.h"

void led_init(uint32_t pin)
{
    RCC_AHB1ENR |= RCC_AHB1ENR_GPIODEN;                 /* clock on for GPIOD */

    GPIO_MODER(GPIOD_BASE) &= ~(3UL << (pin * 2U));     /* clear mode bits    */
    GPIO_MODER(GPIOD_BASE) |=  (1UL << (pin * 2U));     /* 01 = output        */
}

/* BSRR: writing 1 to bit n sets pin n, writing 1 to bit n+16 clears it.
 * One single write, so it can never be interrupted halfway (atomic). */
void led_on(uint32_t pin)
{
    GPIO_BSRR(GPIOD_BASE) = (1UL << pin);
}

void led_off(uint32_t pin)
{
    GPIO_BSRR(GPIOD_BASE) = (1UL << (pin + 16U));
}

void led_toggle(uint32_t pin)
{
    if (led_is_on(pin)) {
        led_off(pin);
    } else {
        led_on(pin);
    }
}

int led_is_on(uint32_t pin)
{
    return (int)((GPIO_ODR(GPIOD_BASE) >> pin) & 1UL);
}
