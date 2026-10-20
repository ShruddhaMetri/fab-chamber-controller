/*
 * Fab Chamber Controller - Step 2
 * First program: UART boot message + LED heartbeat
 * Target: STM32F407VG (STM32F4 Discovery), simulated in Renode
 * Style : register-level C (no HAL), so every line maps to real hardware
 */

#include <stdint.h>

/* ------------------------------------------------------------------ */
/* 1. Register map (addresses from the STM32F407 reference manual)     */
/* ------------------------------------------------------------------ */

/* RCC: Reset and Clock Control - turns peripheral clocks on/off */
#define RCC_BASE        0x40023800UL
#define RCC_AHB1ENR     (*(volatile uint32_t *)(RCC_BASE + 0x30))
#define RCC_APB1ENR     (*(volatile uint32_t *)(RCC_BASE + 0x40))

/* GPIOA: used for USART2 TX pin (PA2) */
#define GPIOA_BASE      0x40020000UL
#define GPIOA_MODER     (*(volatile uint32_t *)(GPIOA_BASE + 0x00))
#define GPIOA_AFRL      (*(volatile uint32_t *)(GPIOA_BASE + 0x20))

/* GPIOD: the Discovery board LEDs are on PD12..PD15 */
#define GPIOD_BASE      0x40020C00UL
#define GPIOD_MODER     (*(volatile uint32_t *)(GPIOD_BASE + 0x00))
#define GPIOD_ODR       (*(volatile uint32_t *)(GPIOD_BASE + 0x14))

/* USART2: our debug console */
#define USART2_BASE     0x40004400UL
#define USART2_SR       (*(volatile uint32_t *)(USART2_BASE + 0x00))
#define USART2_DR       (*(volatile uint32_t *)(USART2_BASE + 0x04))
#define USART2_BRR      (*(volatile uint32_t *)(USART2_BASE + 0x08))
#define USART2_CR1      (*(volatile uint32_t *)(USART2_BASE + 0x0C))

/* Bit positions */
#define RCC_AHB1ENR_GPIOAEN   (1UL << 0)
#define RCC_AHB1ENR_GPIODEN   (1UL << 3)
#define RCC_APB1ENR_USART2EN  (1UL << 17)
#define USART_SR_TXE          (1UL << 7)   /* transmit data register empty */
#define USART_CR1_TE          (1UL << 3)   /* transmitter enable */
#define USART_CR1_UE          (1UL << 13)  /* USART enable */

#define STATUS_LED_PIN        12           /* PD12 = green LED */

/* ------------------------------------------------------------------ */
/* 2. Drivers                                                          */
/* ------------------------------------------------------------------ */

static void gpio_led_init(void)
{
    RCC_AHB1ENR |= RCC_AHB1ENR_GPIODEN;              /* clock on for GPIOD */

    GPIOD_MODER &= ~(3UL << (STATUS_LED_PIN * 2));   /* clear 2 mode bits */
    GPIOD_MODER |=  (1UL << (STATUS_LED_PIN * 2));   /* 01 = output mode   */
}

static void led_toggle(void)
{
    GPIOD_ODR ^= (1UL << STATUS_LED_PIN);
}

static int led_is_on(void)
{
    return (GPIOD_ODR >> STATUS_LED_PIN) & 1UL;
}

static void uart2_init(void)
{
    RCC_AHB1ENR |= RCC_AHB1ENR_GPIOAEN;    /* clock on for GPIOA  */
    RCC_APB1ENR |= RCC_APB1ENR_USART2EN;   /* clock on for USART2 */

    /* PA2 -> alternate function mode (10), AF7 = USART2_TX */
    GPIOA_MODER &= ~(3UL << (2 * 2));
    GPIOA_MODER |=  (2UL << (2 * 2));
    GPIOA_AFRL  &= ~(0xFUL << (2 * 4));
    GPIOA_AFRL  |=  (7UL   << (2 * 4));

    /* Baud rate: 16 MHz HSI clock / 115200 = 138.9 -> mantissa 8, fraction 11 */
    USART2_BRR = (8UL << 4) | 11UL;

    USART2_CR1 = USART_CR1_TE | USART_CR1_UE;  /* enable transmitter + USART */
}

static void uart2_putc(char c)
{
    while ((USART2_SR & USART_SR_TXE) == 0) {
        /* wait until the transmit register is empty */
    }
    USART2_DR = (uint32_t)c;
}

static void uart2_puts(const char *s)
{
    while (*s) {
        if (*s == '\n') {
            uart2_putc('\r');   /* terminals expect CR+LF */
        }
        uart2_putc(*s++);
    }
}

/* Crude delay: just burns CPU cycles. Replaced by a hardware timer in Step 3. */
static void delay(volatile uint32_t count)
{
    while (count--) {
        /* do nothing */
    }
}

/* ------------------------------------------------------------------ */
/* 3. Application                                                      */
/* ------------------------------------------------------------------ */

int main(void)
{
    uint32_t beat = 0;

    gpio_led_init();
    uart2_init();

    uart2_puts("\n=================================\n");
    uart2_puts(" Fab Chamber Controller v0.1\n");
    uart2_puts(" Chamber controller booting...\n");
    uart2_puts("=================================\n");

    while (1) {
        led_toggle();

        uart2_puts("Heartbeat ");
        uart2_putc('0' + (char)(beat % 10));
        uart2_puts(led_is_on() ? " : status LED ON\n" : " : status LED OFF\n");

        beat++;
        delay(2000000);
    }
}
