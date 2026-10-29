/*
 * Fab Chamber Controller - Step 8
 * Process-gas control: DAC setpoints to two mass flow controllers,
 * ADC readback of the real flow, and a gas-flow interlock.
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
#include "gas.h"

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
    gas_init();
    sensors_init();
    safety_init();

    /* 2. Boot banner */
    uart2_puts("\n=================================\n");
    uart2_puts(" " FW_NAME " v" FW_VERSION "\n");
    uart2_puts(" Gas MFCs (DAC), sensors, heater, interlocks\n");
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

        /* Job 2: every 100 ms: sensors (temperature, pressure) and gas flow check */
        if ((now - last_sensors) >= SENSOR_PERIOD_MS) {
            last_sensors += SENSOR_PERIOD_MS;
            gas_poll();       /* uses the scan that sensors_poll() started 100 ms ago */
            sensors_poll();   /* reads that scan, then starts the next one            */
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
