/*
 * Fab Chamber Controller - Step 12
 * FreeRTOS tasks (rtos_app.c) + automatic recipe execution (process.c).
 * Target: STM32F407VG (STM32F4 Discovery), simulated in Renode
 */
#include <stdint.h>
#include "app_config.h"
#include "gpio.h"
#include "systick.h"
#include "uart.h"
#include "safety.h"
#include "actuators.h"
#include "sensors.h"
#include "gas.h"
#include "i2c.h"
#include "datalog.h"
#include "temp_ctrl.h"
#include "rtos_app.h"

int main(void)
{
    /* 1. Initialise hardware - exactly as before.
     *    LEDs and actuators come before safety: a fault found at boot
     *    switches on the red LED and forces the actuators safe. */
    led_init(LED_GREEN);
    led_init(LED_ORANGE);
    led_init(LED_RED);
    led_init(LED_BLUE);
    uart2_init();
    systick_init();
    actuators_init();
    gas_init();
    sensors_init();
    safety_init();
    i2c1_init();
    tc_init();

    /* 2. Boot banner */
    uart2_puts("\n=================================\n");
    uart2_puts(" " FW_NAME " v" FW_VERSION "\n");
    uart2_puts(" FreeRTOS + recipe automation\n");
    uart2_puts("=================================\n");
    datalog_init();          /* prints how many records it found */

    /* 3. Hand over to the RTOS. The old while(1) loop is gone: each of its
     *    jobs is now a task, and the kernel's scheduler decides which one
     *    runs. This call never returns. */
    rtos_app_start();

    return 0;                /* never reached */
}
