/*
 * gpio.h
 * LED driver for the four LEDs on port D of the STM32F4 Discovery board.
 */
#ifndef GPIO_H
#define GPIO_H

#include <stdint.h>

#define LED_GREEN    12U   /* heartbeat          */
#define LED_ORANGE   13U   /* activity           */
#define LED_RED      14U   /* fault (later step) */
#define LED_BLUE     15U   /* controlled from the console */

void led_init(uint32_t pin);
void led_on(uint32_t pin);
void led_off(uint32_t pin);
void led_toggle(uint32_t pin);
int  led_is_on(uint32_t pin);

#endif /* GPIO_H */
