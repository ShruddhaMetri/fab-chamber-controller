/*
 * systick.h
 * 1 ms system timebase using the Cortex-M SysTick timer.
 */
#ifndef SYSTICK_H
#define SYSTICK_H

#include <stdint.h>

void     systick_init(void);
uint32_t millis(void);
void     delay_ms(uint32_t ms);

#endif /* SYSTICK_H */
