/*
 * systick.c
 * SysTick driver: interrupt every 1 ms, counts milliseconds since boot.
 *
 * From Step 11, SysTick is shared with FreeRTOS:
 *   - before the scheduler starts, it only counts milliseconds (used by
 *     timeouts during start-up);
 *   - after the scheduler starts, it ALSO runs the kernel's tick, which is
 *     what wakes up delayed tasks and switches between equal-priority tasks.
 * FreeRTOS reprograms SysTick when it starts (same 1 ms rate, lowest
 * interrupt priority), so millis() keeps counting without a jump.
 */
#include "systick.h"
#include "stm32f407_regs.h"
#include "app_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TICK_RATE_HZ   1000UL

/* The kernel's tick handler (in port.c); not declared in any public header */
extern void xPortSysTickHandler(void);

/* Written by the interrupt, read by everyone else */
static volatile uint32_t g_ms_ticks = 0;

/* Name must match the vector table in the startup file exactly */
void SysTick_Handler(void)
{
    g_ms_ticks++;
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        xPortSysTickHandler();
    }
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

/* Busy-wait. Inside a task, prefer vTaskDelay(): it lets other tasks run. */
void delay_ms(uint32_t ms)
{
    uint32_t start = millis();
    while ((millis() - start) < ms) {
        /* wait */
    }
}
