/*
 * sensors.c
 * Scaling (counts -> degrees, Torr) and the over-temperature interlock.
 */
#include "sensors.h"
#include "adc.h"
#include "safety.h"
#include "app_config.h"

#define TEMP_FULL_SCALE_DC        8000U    /* 800.0 C at 3.3 V   */
#define PRESSURE_FULL_SCALE_MTORR 10000U   /* 10.000 Torr at 3.3 V */

static uint32_t temp_dc        = 0U;
static uint32_t pressure_mtorr = 0U;
static uint32_t over_count     = 0U;      /* readings in a row above the limit */

/* Linear scaling: value = raw x full_scale / 4095.
 * Multiply first, then divide, so integer maths keeps its precision.
 * Largest product = 4095 x 10000 = 40 950 000: fits easily in 32 bits. */
static uint32_t scale(uint16_t raw, uint32_t full_scale)
{
    return ((uint32_t)raw * full_scale) / ADC_MAX_COUNT;
}

void sensors_init(void)
{
    adc_init();
    adc_start_scan();           /* first readings are ready well before the first poll */
}

void sensors_poll(void)
{
    /* Use the results of the scan started 100 ms ago, then start the next one */
    temp_dc        = scale(adc_get_raw(ADC_IDX_TEMP),     TEMP_FULL_SCALE_DC);
    pressure_mtorr = scale(adc_get_raw(ADC_IDX_PRESSURE), PRESSURE_FULL_SCALE_MTORR);
    adc_start_scan();

    /* Over-temperature: trip only after several readings in a row,
     * so one noisy sample cannot stop the process */
    if (temp_dc > OVERTEMP_LIMIT_DC) {
        if (over_count < OVERTEMP_SAMPLES) {
            over_count++;
        }
        if (over_count == OVERTEMP_SAMPLES) {
            safety_trip_from_task(FAULT_OVERTEMP);
        }
    } else {
        over_count = 0U;
    }
}

uint32_t sensors_temp_dc(void)        { return temp_dc; }
uint32_t sensors_pressure_mtorr(void) { return pressure_mtorr; }

/* Hysteresis: the fault may only be cleared once it is clearly cooler */
int sensors_overtemp_active(void)
{
    return temp_dc >= OVERTEMP_RESET_DC;
}
