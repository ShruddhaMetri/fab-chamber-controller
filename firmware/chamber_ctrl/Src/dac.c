/*
 * dac.c
 * DAC driver. Writing a 12-bit number to a "data holding register" (DHR)
 * makes the DAC put the matching voltage on its pin:
 *     voltage = value x 3.3 V / 4095
 * The "data output register" (DOR) shows what is actually being output.
 */
#include "dac.h"
#include "stm32f407_regs.h"

#define DAC1_PIN   4U    /* PA4 */
#define DAC2_PIN   5U    /* PA5 */

void dac_init(void)
{
    /* 1. Clocks: GPIOA for the pins, DAC */
    RCC_AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC_APB1ENR |= RCC_APB1ENR_DACEN;

    /* 2. PA4 and PA5 in analog mode (11), so the DAC can drive them */
    GPIO_MODER(GPIOA_BASE) |= (3UL << (DAC1_PIN * 2U)) | (3UL << (DAC2_PIN * 2U));

    /* 3. Start at 0 V, then switch both channels on.
     *    No trigger is enabled, so a new value is output as soon as it is written. */
    DAC_DHR12R1 = 0U;
    DAC_DHR12R2 = 0U;
    DAC_CR      = DAC_CR_EN1 | DAC_CR_EN2;
}

void dac_write(uint32_t channel, uint32_t value)
{
    if (value > DAC_MAX_COUNT) {
        value = DAC_MAX_COUNT;         /* clamp to 12 bits */
    }
    if (channel == DAC_CH1) {
        DAC_DHR12R1 = value;
    } else if (channel == DAC_CH2) {
        DAC_DHR12R2 = value;
    }
}

uint32_t dac_read_output(uint32_t channel)
{
    if (channel == DAC_CH1) {
        return DAC_DOR1 & 0x0FFFU;
    }
    if (channel == DAC_CH2) {
        return DAC_DOR2 & 0x0FFFU;
    }
    return 0U;
}
