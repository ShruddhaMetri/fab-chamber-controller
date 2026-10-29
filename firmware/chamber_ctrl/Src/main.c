/*
 * Fab Chamber Controller - Step 4
 * Code split into driver files + interactive UART command console.
 * Target: STM32F407VG (STM32F4 Discovery), simulated in Renode
 */
#include <stdint.h>
#include "app_config.h"
#include "gpio.h"
#include "systick.h"
#include "uart.h"
#include "console.h"

int main(void)
{
    uint32_t last_heartbeat = 0;
    uint32_t last_activity  = 0;

    /* 1. Initialise hardware */
    led_init(LED_GREEN);
    led_init(LED_ORANGE);
    led_init(LED_BLUE);
    uart2_init();
    systick_init();

    /* 2. Boot banner */
    uart2_puts("\n=================================\n");
    uart2_puts(" " FW_NAME " v" FW_VERSION "\n");
    uart2_puts(" Drivers + command console\n");
    uart2_puts("=================================\n");
    console_init();

    /* 3. Main loop: three independent jobs, none of them blocks */
    while (1) {
        uint32_t now = millis();

        /* Job 1: heartbeat LED every 1000 ms */
        if ((now - last_heartbeat) >= HEARTBEAT_PERIOD_MS) {
            last_heartbeat += HEARTBEAT_PERIOD_MS;
            led_toggle(LED_GREEN);
        }

        /* Job 2: activity LED every 250 ms */
        if ((now - last_activity) >= ACTIVITY_PERIOD_MS) {
            last_activity += ACTIVITY_PERIOD_MS;
            led_toggle(LED_ORANGE);
        }

        /* Job 3: handle any typed characters */
        console_poll();
    }
}
