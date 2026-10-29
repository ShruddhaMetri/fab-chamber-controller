/*
 * Fab Chamber Controller - Step 7
 * Temperature and pressure sensing with ADC1 + DMA2, and an
 * over-temperature interlock that forces the same safe state as the e-stop.
 * Target: STM32F407VG (STM32F4 Discovery), simulated in Renode
 */
#include <stdint.h>
#include "app_config.h"
#include "gpio.h"
#include "systick.h"
#include "uart.h"
#include "console.h"
#include "safety.h"
#include "actuators.h"
#include "sensors.h"

int main(void)
{
    uint32_t last_heartbeat = 0;
    uint32_t last_activity  = 0;
    uint32_t last_safety    = 0;
    uint32_t last_sensors   = 0;

    /* 1. Initialise hardware.
     *    LEDs and actuators come before safety: a fault found at boot
     *    switches on the red LED and forces the actuators safe. */
    led_init(LED_GREEN);
    led_init(LED_ORANGE);
    led_init(LED_RED);
    led_init(LED_BLUE);
    uart2_init();
    systick_init();
    actuators_init();
    sensors_init();
    safety_init();

    /* 2. Boot banner */
    uart2_puts("\n=================================\n");
    uart2_puts(" " FW_NAME " v" FW_VERSION "\n");
    uart2_puts(" ADC + DMA sensors, PWM actuators, interlocks\n");
    uart2_puts("=================================\n");
    console_init();

    /* 3. Main loop: five independent jobs, none of them blocks */
    while (1) {
        uint32_t now = millis();

        /* Job 1: safety inputs every 5 ms (debounce + report) */
        if ((now - last_safety) >= SAFETY_SAMPLE_MS) {
            last_safety += SAFETY_SAMPLE_MS;
            safety_poll();
        }

        /* Job 2: sensors every 100 ms (scale + over-temperature check) */
        if ((now - last_sensors) >= SENSOR_PERIOD_MS) {
            last_sensors += SENSOR_PERIOD_MS;
            sensors_poll();
        }

        /* Job 3: heartbeat LED every 1000 ms */
        if ((now - last_heartbeat) >= HEARTBEAT_PERIOD_MS) {
            last_heartbeat += HEARTBEAT_PERIOD_MS;
            led_toggle(LED_GREEN);
        }

        /* Job 4: activity LED every 250 ms */
        if ((now - last_activity) >= ACTIVITY_PERIOD_MS) {
            last_activity += ACTIVITY_PERIOD_MS;
            led_toggle(LED_ORANGE);
        }

        /* Job 5: handle any typed characters */
        console_poll();
    }
}
