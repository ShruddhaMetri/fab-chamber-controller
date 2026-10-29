/*
 * dac.h
 * DAC driver: two 12-bit analog outputs.
 *   Channel 1 : PA4 -> MFC 1 setpoint
 *   Channel 2 : PA5 -> MFC 2 setpoint
 * 0 = 0 V, 4095 = 3.3 V.
 */
#ifndef DAC_H
#define DAC_H

#include <stdint.h>

#define DAC_CH1        1U
#define DAC_CH2        2U
#define DAC_MAX_COUNT  4095U

void     dac_init(void);
void     dac_write(uint32_t channel, uint32_t value);  /* 0-4095, clamped           */
uint32_t dac_read_output(uint32_t channel);            /* what the pin is outputting */

#endif /* DAC_H */
