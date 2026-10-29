/*
 * systick.c
 * SysTick driver: interrupt every 1 ms, counts milliseconds since boot.
 */
#include "systick.h"
#include "stm32f407_regs.h"
#include "app_config.h"

#define TICK_RATE_HZ   1000UL

/* Written by the interrupt, read by everyone else */
static volatile uint32_t g_ms_ticks = 0;

/* Name must match the vector table in the startup file exactly */
void SysTick_Handler(void)
{
    g_ms_ticks++;
}

void systick_init(void)
{
    SYST_RVR = (CPU_CLOCK_HZ / TICK_RATE_HZ) - 1UL;   /* 15999 at 16 MHz */
    SYST_CVR = 0;
    SYST_CSR = SYST_CSR_CLKSOURCE | SYST_CSR_TICKINT | SYST_CSR_ENABLE;
}

uint32_t millis(void)
{
    return g_ms_ticks;
}

void delay_ms(uint32_t ms)
{
    uint32_t start = millis();
    while ((millis() - start) < ms) {
        /* wait */
    }
}
