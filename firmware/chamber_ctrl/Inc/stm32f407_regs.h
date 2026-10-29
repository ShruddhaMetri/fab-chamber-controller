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
#define RCC_APB1ENR_USART2EN   (1UL << 17)
#define RCC_APB2ENR_SYSCFGEN   (1UL << 14)

/* ---------------- GPIO ---------------- */
#define GPIOA_BASE             0x40020000UL
#define GPIOB_BASE             0x40020400UL
#define GPIOC_BASE             0x40020800UL
#define GPIOD_BASE             0x40020C00UL

#define GPIO_MODER(base)       REG32((base) + 0x00)  /* pin mode           */
#define GPIO_PUPDR(base)       REG32((base) + 0x0C)  /* pull-up/pull-down  */
#define GPIO_IDR(base)         REG32((base) + 0x10)  /* input data         */
#define GPIO_ODR(base)         REG32((base) + 0x14)  /* output data        */
#define GPIO_BSRR(base)        REG32((base) + 0x18)  /* atomic set / reset */
#define GPIO_AFRL(base)        REG32((base) + 0x20)  /* alternate function, pins 0-7 */

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
