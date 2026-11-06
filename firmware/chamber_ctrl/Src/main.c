/*
 * Fab Chamber Controller - Step 15
 * Runs from 0x08020000, started by the bootloader; can be updated over CAN.
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
#include "can.h"
#include "health.h"
#include "fw_image.h"
#include "stm32f407_regs.h"

int main(void)
{
    /* 0. This program lives at APP_BASE, not at the start of flash: tell the
     *    CPU where OUR interrupt vector table is (VTOR = Vector Table Offset
     *    Register). The bootloader does this too; doing it here as well means
     *    the app also works when a debugger starts it directly. */
    REG32(0xE000ED08UL) = APP_BASE;

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
    can1_init();             /* Step 13: CAN bus to the fab host */
    tc_init();

    /* 2. Boot banner */
    uart2_puts("\n=================================\n");
    uart2_puts(" " FW_NAME " v" FW_VERSION "\n");
    uart2_puts(" Bootloader + CAN firmware update\n");
    uart2_puts("=================================\n");
    health_boot_report();    /* Step 14: why did we restart? */
    datalog_init();          /* prints how many records it found */

    /* 3. Hand over to the RTOS. The old while(1) loop is gone: each of its
     *    jobs is now a task, and the kernel's scheduler decides which one
     *    runs. This call never returns. */
    rtos_app_start();

    return 0;                /* never reached */
}
