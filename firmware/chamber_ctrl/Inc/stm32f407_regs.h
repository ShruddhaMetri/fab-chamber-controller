/*
 * stm32f407_regs.h
 * Register addresses and bit definitions for the STM32F407 peripherals we use.
 * Source: STM32F407 Reference Manual (RM0090) and ARM Cortex-M4 documentation.
 */
#ifndef STM32F407_REGS_H
#define STM32F407_REGS_H

#include <stdint.h>

/* Turn an address into a 32-bit hardware register we can read and write */
#define REG32(addr)            (*(volatile uint32_t *)(uintptr_t)(addr))

/* ---------------- RCC: Reset and Clock Control ---------------- */
#define RCC_BASE               0x40023800UL
#define RCC_AHB1ENR            REG32(RCC_BASE + 0x30)
#define RCC_APB1ENR            REG32(RCC_BASE + 0x40)
#define RCC_APB2ENR            REG32(RCC_BASE + 0x44)

#define RCC_AHB1ENR_GPIOAEN    (1UL << 0)
#define RCC_AHB1ENR_GPIOBEN    (1UL << 1)
#define RCC_AHB1ENR_GPIOCEN    (1UL << 2)
#define RCC_AHB1ENR_GPIODEN    (1UL << 3)
#define RCC_AHB1ENR_DMA2EN     (1UL << 22)
#define RCC_APB1ENR_TIM3EN     (1UL << 1)
#define RCC_APB1ENR_SPI2EN     (1UL << 14)
#define RCC_APB1ENR_I2C1EN     (1UL << 21)
#define RCC_APB1ENR_USART2EN   (1UL << 17)
#define RCC_APB1ENR_DACEN      (1UL << 29)
#define RCC_APB2ENR_ADC1EN     (1UL << 8)
#define RCC_APB2ENR_SYSCFGEN   (1UL << 14)

/* ---------------- GPIO ---------------- */
#define GPIOA_BASE             0x40020000UL
#define GPIOB_BASE             0x40020400UL
#define GPIOC_BASE             0x40020800UL
#define GPIOD_BASE             0x40020C00UL

#define GPIO_MODER(base)       REG32((base) + 0x00)  /* pin mode           */
#define GPIO_OTYPER(base)      REG32((base) + 0x04)  /* push-pull / open-drain */
#define GPIO_PUPDR(base)       REG32((base) + 0x0C)  /* pull-up/pull-down  */
#define GPIO_IDR(base)         REG32((base) + 0x10)  /* input data         */
#define GPIO_ODR(base)         REG32((base) + 0x14)  /* output data        */
#define GPIO_BSRR(base)        REG32((base) + 0x18)  /* atomic set / reset */
#define GPIO_AFRL(base)        REG32((base) + 0x20)  /* alternate function, pins 0-7 */
#define GPIO_AFRH(base)        REG32((base) + 0x24)  /* alternate function, pins 8-15 */

/* ---------------- USART2 ---------------- */
#define USART2_BASE            0x40004400UL
#define USART2_SR              REG32(USART2_BASE + 0x00)
#define USART2_DR              REG32(USART2_BASE + 0x04)
#define USART2_BRR             REG32(USART2_BASE + 0x08)
#define USART2_CR1             REG32(USART2_BASE + 0x0C)

#define USART_SR_RXNE          (1UL << 5)   /* a received byte is waiting  */
#define USART_SR_TXE           (1UL << 7)   /* transmit register is empty  */
#define USART_CR1_RE           (1UL << 2)   /* receiver enable             */
#define USART_CR1_TE           (1UL << 3)   /* transmitter enable          */
#define USART_CR1_RXNEIE       (1UL << 5)   /* interrupt when byte arrives */
#define USART_CR1_UE           (1UL << 13)  /* USART enable                */

/* ---------------- TIM3: general-purpose timer used for PWM ---------------- */
#define TIM3_BASE              0x40000400UL
#define TIM3_CR1               REG32(TIM3_BASE + 0x00)  /* control: start/stop        */
#define TIM3_EGR               REG32(TIM3_BASE + 0x14)  /* event generation           */
#define TIM3_CCMR1             REG32(TIM3_BASE + 0x18)  /* channel 1-2 mode           */
#define TIM3_CCER              REG32(TIM3_BASE + 0x20)  /* channel output enable      */
#define TIM3_CNT               REG32(TIM3_BASE + 0x24)  /* the counter itself         */
#define TIM3_PSC               REG32(TIM3_BASE + 0x28)  /* prescaler: divides clock   */
#define TIM3_ARR               REG32(TIM3_BASE + 0x2C)  /* auto-reload: the period    */
#define TIM3_CCR1              REG32(TIM3_BASE + 0x34)  /* compare value, channel 1   */
#define TIM3_CCR2              REG32(TIM3_BASE + 0x38)  /* compare value, channel 2   */

#define TIM_CR1_CEN            (1UL << 0)    /* counter enable                 */
#define TIM_CR1_ARPE           (1UL << 7)    /* buffer ARR until next period   */
#define TIM_EGR_UG             (1UL << 0)    /* update: load buffered values   */
#define TIM_CCMR1_OC1PE        (1UL << 3)    /* buffer CCR1 until next period  */
#define TIM_CCMR1_OC2PE        (1UL << 11)   /* buffer CCR2 until next period  */
#define TIM_CCMR_PWM1          6UL           /* output-compare mode 110 = PWM mode 1 */
#define TIM_CCMR1_OC1M_POS     4U
#define TIM_CCMR1_OC2M_POS     12U
#define TIM_CCER_CC1E          (1UL << 0)    /* channel 1 output on            */
#define TIM_CCER_CC2E          (1UL << 4)    /* channel 2 output on            */

/* ---------------- ADC1: analog-to-digital converter ---------------- */
#define ADC1_BASE              0x40012000UL
#define ADC1_SR                REG32(ADC1_BASE + 0x00)  /* status                    */
#define ADC1_CR1               REG32(ADC1_BASE + 0x04)  /* control 1: scan mode      */
#define ADC1_CR2               REG32(ADC1_BASE + 0x08)  /* control 2: on, DMA, start */
#define ADC1_SMPR1             REG32(ADC1_BASE + 0x0C)  /* sample time, ch 10-18     */
#define ADC1_SQR1              REG32(ADC1_BASE + 0x2C)  /* sequence length           */
#define ADC1_SQR3              REG32(ADC1_BASE + 0x34)  /* sequence order 1-6        */
#define ADC1_DR                REG32(ADC1_BASE + 0x4C)  /* data: the result          */
#define ADC1_DR_ADDR           (ADC1_BASE + 0x4C)       /* its address, for the DMA  */

#define ADC_CR1_SCAN           (1UL << 8)    /* convert several channels in turn  */
#define ADC_CR2_ADON           (1UL << 0)    /* ADC power on                      */
#define ADC_CR2_DMA            (1UL << 8)    /* request DMA after each conversion */
#define ADC_CR2_DDS            (1UL << 9)    /* keep requesting DMA continuously  */
#define ADC_CR2_SWSTART        (1UL << 30)   /* software start                    */

/* ---------------- I2C1: two-wire bus (EEPROM) ---------------- */
#define I2C1_BASE              0x40005400UL
#define I2C1_CR1               REG32(I2C1_BASE + 0x00)
#define I2C1_CR2               REG32(I2C1_BASE + 0x04)
#define I2C1_DR                REG32(I2C1_BASE + 0x10)
#define I2C1_SR1               REG32(I2C1_BASE + 0x14)
#define I2C1_SR2               REG32(I2C1_BASE + 0x18)
#define I2C1_CCR               REG32(I2C1_BASE + 0x1C)
#define I2C1_TRISE             REG32(I2C1_BASE + 0x20)

#define I2C_CR1_PE             (1UL << 0)    /* peripheral enable            */
#define I2C_CR1_START          (1UL << 8)    /* generate START condition     */
#define I2C_CR1_STOP           (1UL << 9)    /* generate STOP condition      */
#define I2C_CR1_ACK            (1UL << 10)   /* acknowledge received bytes   */
#define I2C_CR1_SWRST          (1UL << 15)   /* software reset               */
#define I2C_SR1_SB             (1UL << 0)    /* START sent                   */
#define I2C_SR1_ADDR           (1UL << 1)    /* address sent and acknowledged */
#define I2C_SR1_BTF            (1UL << 2)    /* byte transfer finished       */
#define I2C_SR1_RXNE           (1UL << 6)    /* a received byte is waiting   */
#define I2C_SR1_TXE            (1UL << 7)    /* ready for the next byte      */
#define I2C_SR1_AF             (1UL << 10)   /* acknowledge failure (NACK)   */

/* ---------------- SPI2: four-wire bus (data-log flash) ---------------- */
#define SPI2_BASE              0x40003800UL
#define SPI2_CR1               REG32(SPI2_BASE + 0x00)
#define SPI2_SR                REG32(SPI2_BASE + 0x08)
#define SPI2_DR                REG32(SPI2_BASE + 0x0C)

#define SPI_CR1_MSTR           (1UL << 2)    /* this chip is the master       */
#define SPI_CR1_BR_POS         3U            /* baud-rate divider bits 5:3    */
#define SPI_CR1_SPE            (1UL << 6)    /* SPI enable                    */
#define SPI_CR1_SSI            (1UL << 8)    /* internal chip-select level    */
#define SPI_CR1_SSM            (1UL << 9)    /* chip-select managed by software */
#define SPI_SR_RXNE            (1UL << 0)    /* a received byte is waiting    */
#define SPI_SR_TXE             (1UL << 1)    /* ready for the next byte       */
#define SPI_SR_BSY             (1UL << 7)    /* transfer in progress          */

/* ---------------- DAC: digital-to-analog converter ---------------- */
#define DAC_BASE               0x40007400UL
#define DAC_CR                 REG32(DAC_BASE + 0x00)  /* control: channel enables  */
#define DAC_DHR12R1            REG32(DAC_BASE + 0x08)  /* channel 1 input, 12-bit   */
#define DAC_DHR12R2            REG32(DAC_BASE + 0x14)  /* channel 2 input, 12-bit   */
#define DAC_DOR1               REG32(DAC_BASE + 0x2C)  /* channel 1 output (read)   */
#define DAC_DOR2               REG32(DAC_BASE + 0x30)  /* channel 2 output (read)   */

#define DAC_CR_EN1             (1UL << 0)    /* channel 1 on (pin PA4) */
#define DAC_CR_EN2             (1UL << 16)   /* channel 2 on (pin PA5) */

/* ---------------- DMA2: direct memory access controller ---------------- */
#define DMA2_BASE              0x40026400UL
#define DMA2_LIFCR             REG32(DMA2_BASE + 0x08)  /* clear flags, streams 0-3 */
#define DMA2_S0CR              REG32(DMA2_BASE + 0x10)  /* stream 0 configuration   */
#define DMA2_S0NDTR            REG32(DMA2_BASE + 0x14)  /* number of items to copy  */
#define DMA2_S0PAR             REG32(DMA2_BASE + 0x18)  /* peripheral (source) addr */
#define DMA2_S0M0AR            REG32(DMA2_BASE + 0x1C)  /* memory (destination) addr */

#define DMA_SCR_EN             (1UL << 0)    /* stream enable                     */
#define DMA_SCR_MINC           (1UL << 10)   /* memory address increments         */
#define DMA_SCR_PSIZE_32       (2UL << 11)   /* peripheral item = 32 bits         */
#define DMA_SCR_MSIZE_32       (2UL << 13)   /* memory item = 32 bits             */
#define DMA_LIFCR_S0_ALL       0x3DUL        /* clear all stream 0 flags          */
#define DMA_SCR_CHSEL_POS      25U           /* request channel select            */

/* ---------------- SysTick (inside the Cortex-M4 core) ---------------- */
#define SYSTICK_BASE           0xE000E010UL
#define SYST_CSR               REG32(SYSTICK_BASE + 0x00)
#define SYST_RVR               REG32(SYSTICK_BASE + 0x04)
#define SYST_CVR               REG32(SYSTICK_BASE + 0x08)

#define SYST_CSR_ENABLE        (1UL << 0)
#define SYST_CSR_TICKINT       (1UL << 1)
#define SYST_CSR_CLKSOURCE     (1UL << 2)

/* ---------------- SYSCFG: chooses which port drives each EXTI line ---------------- */
#define SYSCFG_BASE            0x40013800UL
#define SYSCFG_EXTICR1         REG32(SYSCFG_BASE + 0x08)  /* lines 0-3, 4 bits each */

#define EXTI_PORT_A            0U
#define EXTI_PORT_B            1U
#define EXTI_PORT_C            2U

/* ---------------- EXTI: external interrupt controller ---------------- */
#define EXTI_BASE              0x40013C00UL
#define EXTI_IMR               REG32(EXTI_BASE + 0x00)  /* interrupt mask: 1 = enabled  */
#define EXTI_RTSR              REG32(EXTI_BASE + 0x08)  /* rising-edge trigger          */
#define EXTI_FTSR              REG32(EXTI_BASE + 0x0C)  /* falling-edge trigger         */
#define EXTI_PR                REG32(EXTI_BASE + 0x14)  /* pending: write 1 to clear    */

/* ---------------- NVIC: interrupt controller ---------------- */
/* ISER = Interrupt Set-Enable Registers; each holds 32 interrupts */
#define NVIC_ISER(n)           REG32(0xE000E100UL + 4UL * (n))
/* IPR = one priority byte per interrupt; STM32 uses the top 4 bits; 0 = most urgent */
#define NVIC_IPR(irq)          (*(volatile uint8_t *)(0xE000E400UL + (irq)))

#define EXTI0_IRQn             6U           /* EXTI line 0 interrupt number */
#define EXTI1_IRQn             7U           /* EXTI line 1 interrupt number */
#define USART2_IRQn            38U          /* USART2's interrupt number    */

/* ---------------- SCB: system control ---------------- */
#define SCB_AIRCR              REG32(0xE000ED0CUL)
#define SCB_AIRCR_RESET        ((0x5FAUL << 16) | (1UL << 2))  /* key + SYSRESETREQ */

#endif /* STM32F407_REGS_H */
