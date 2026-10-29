/*
 * Fab Chamber Controller - Step 5
 * Safety inputs: emergency-stop button and door switch on external interrupts,
 * with debouncing, a latched fault and a red fault LED.
 * Target: STM32F407VG (STM32F4 Discovery), simulated in Renode
 */
#include <stdint.h>
#include "app_config.h"
#include "gpio.h"
#include "systick.h"
#include "uart.h"
#include "console.h"
#include "safety.h"

int main(void)
{
    uint32_t last_heartbeat = 0;
    uint32_t last_activity  = 0;
    uint32_t last_safety    = 0;

    /* 1. Initialise hardware (LEDs first: the safety code may switch on the red LED) */
    led_init(LED_GREEN);
    led_init(LED_ORANGE);
    led_init(LED_RED);
    led_init(LED_BLUE);
    uart2_init();
    systick_init();
    safety_init();

    /* 2. Boot banner */
    uart2_puts("\n=================================\n");
    uart2_puts(" " FW_NAME " v" FW_VERSION "\n");
    uart2_puts(" Safety inputs: e-stop + door\n");
    uart2_puts("=================================\n");
    console_init();

    /* 3. Main loop: four independent jobs, none of them blocks */
    while (1) {
        uint32_t now = millis();

        /* Job 1: safety inputs every 5 ms (debounce + report) */
        if ((now - last_safety) >= SAFETY_SAMPLE_MS) {
            last_safety += SAFETY_SAMPLE_MS;
            safety_poll();
        }

        /* Job 2: heartbeat LED every 1000 ms */
        if ((now - last_heartbeat) >= HEARTBEAT_PERIOD_MS) {
            last_heartbeat += HEARTBEAT_PERIOD_MS;
            led_toggle(LED_GREEN);
        }

        /* Job 3: activity LED every 250 ms */
        if ((now - last_activity) >= ACTIVITY_PERIOD_MS) {
            last_activity += ACTIVITY_PERIOD_MS;
            led_toggle(LED_ORANGE);
        }

        /* Job 4: handle any typed characters */
        console_poll();
    }
}
