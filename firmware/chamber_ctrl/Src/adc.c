/*
 * adc.c
 * ADC1 in scan mode, results moved by DMA2 Stream 0 (channel 0) into RAM.
 *
 *   adc_start_scan():  ADC1 converts ch14, ch15, ch12, ch13 in turn.
 *   After each conversion, ADC1 asks the DMA to copy ADC1_DR into the next
 *   slot of adc_buf[]. After 4 items the DMA stops until the next scan.
 *   The sensors module starts a scan every 100 ms and reads the previous
 *   scan's results, so the CPU never waits for a conversion.
 */
#include "adc.h"
#include "stm32f407_regs.h"

#define TEMP_CH          14U    /* PC4 = ADC123_IN14 */
#define PRESSURE_CH      15U    /* PC5 = ADC123_IN15 */
#define FLOW1_CH         12U    /* PC2 = ADC123_IN12 */
#define FLOW2_CH         13U    /* PC3 = ADC123_IN13 */
#define TEMP_PIN         4U
#define PRESSURE_PIN     5U
#define FLOW1_PIN        2U
#define FLOW2_PIN        3U
#define SAMPLE_480_CYC   7UL    /* longest sample time: most stable reading */
#define DMA2_ADC1_CHSEL  0UL    /* ADC1 is request channel 0 on DMA2 stream 0 */

/* Written by the DMA hardware, read by the CPU: so it must be volatile.
 * 32-bit slots: the DMA reads ADC1_DR as a full 32-bit register. */
static volatile uint32_t adc_buf[ADC_NUM_CHANNELS];

void adc_init(void)
{
    /* 1. Clocks: GPIOC (pins), DMA2, ADC1 */
    RCC_AHB1ENR |= RCC_AHB1ENR_GPIOCEN | RCC_AHB1ENR_DMA2EN;
    RCC_APB2ENR |= RCC_APB2ENR_ADC1EN;

    /* 2. PC2-PC5 in analog mode (11): digital input circuits switched off */
    GPIO_MODER(GPIOC_BASE) |= (3UL << (TEMP_PIN * 2U)) | (3UL << (PRESSURE_PIN * 2U))
                            | (3UL << (FLOW1_PIN * 2U)) | (3UL << (FLOW2_PIN * 2U));

    /* 3. DMA2 Stream 0 (fixed settings): ADC1_DR -> adc_buf[], 32-bit items,
     *    memory address moves to the next slot after each item */
    DMA2_S0CR   = 0U;                              /* stream off while configuring */
    DMA2_S0PAR  = ADC1_DR_ADDR;                    /* source: the ADC data register */
    DMA2_S0M0AR = (uint32_t)(uintptr_t)adc_buf;    /* destination: our array        */
    DMA2_S0CR   = (DMA2_ADC1_CHSEL << DMA_SCR_CHSEL_POS)
                | DMA_SCR_MSIZE_32 | DMA_SCR_PSIZE_32
                | DMA_SCR_MINC;                    /* direction 00 = periph -> mem  */

    /* 4. ADC1: sample times, 3 bits per channel (channels 10-18 live in SMPR1) */
    ADC1_SMPR1 |= (SAMPLE_480_CYC << ((TEMP_CH - 10U) * 3U))
               |  (SAMPLE_480_CYC << ((PRESSURE_CH - 10U) * 3U))
               |  (SAMPLE_480_CYC << ((FLOW1_CH - 10U) * 3U))
               |  (SAMPLE_480_CYC << ((FLOW2_CH - 10U) * 3U));

    /* 5. Sequence: 4 conversions (L = 3), 5 bits per position in SQR3 */
    ADC1_SQR1 = ((ADC_NUM_CHANNELS - 1U) << 20);
    ADC1_SQR3 = (TEMP_CH << 0) | (PRESSURE_CH << 5) | (FLOW1_CH << 10) | (FLOW2_CH << 15);

    /* 6. Scan mode; DMA requests on; power on */
    ADC1_CR1 = ADC_CR1_SCAN;
    ADC1_CR2 = ADC_CR2_ADON | ADC_CR2_DMA | ADC_CR2_DDS;
}

void adc_start_scan(void)
{
    /* Re-arm the DMA for 4 more items */
    DMA2_S0CR &= ~DMA_SCR_EN;
    while (DMA2_S0CR & DMA_SCR_EN) {
        /* wait until the stream has really stopped */
    }
    DMA2_LIFCR  = DMA_LIFCR_S0_ALL;               /* clear old flags        */
    DMA2_S0NDTR = ADC_NUM_CHANNELS;               /* 4 items                */
    DMA2_S0CR  |= DMA_SCR_EN;

    /* Start converting: ch14, ch15, ch12, ch13 */
    ADC1_CR2 |= ADC_CR2_SWSTART;
}

uint16_t adc_get_raw(uint32_t index)
{
    if (index >= ADC_NUM_CHANNELS) {
        return 0U;
    }
    return (uint16_t)(adc_buf[index] & 0x0FFFU);  /* 12-bit result */
}
