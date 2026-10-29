/*
 * adc.h
 * ADC1 driver: each call to adc_start_scan() converts four analog channels
 * one after the other; DMA2 Stream 0 copies each result into memory,
 * with no CPU work.
 *   Index 0 : channel 14 (PC4) - temperature sensor
 *   Index 1 : channel 15 (PC5) - pressure gauge
 *   Index 2 : channel 12 (PC2) - MFC 1 flow readback
 *   Index 3 : channel 13 (PC3) - MFC 2 flow readback
 * Results are 12-bit: 0 = 0 V, 4095 = 3.3 V.
 */
#ifndef ADC_H
#define ADC_H

#include <stdint.h>

#define ADC_IDX_TEMP       0U
#define ADC_IDX_PRESSURE   1U
#define ADC_IDX_FLOW1      2U
#define ADC_IDX_FLOW2      3U
#define ADC_NUM_CHANNELS   4U
#define ADC_MAX_COUNT      4095U

void     adc_init(void);
void     adc_start_scan(void);           /* begin a new scan of all channels */
uint16_t adc_get_raw(uint32_t index);   /* result of the last scan, 0-4095 */

#endif /* ADC_H */
