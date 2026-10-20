/*
 * Fab Chamber Controller - Step 3
 * SysTick timer interrupt + non-blocking scheduling
 * Target: STM32F407VG (STM32F4 Discovery), simulated in Renode
 */

#include <stdint.h>

/* ------------------------------------------------------------------ */
/* 1. Register map                                                     */
/* ------------------------------------------------------------------ */

/* RCC: Reset and Clock Control */
#define RCC_BASE        0x40023800UL
#define RCC_AHB1ENR     (*(volatile uint32_t *)(RCC_BASE + 0x30))
#define RCC_APB1ENR     (*(volatile uint32_t *)(RCC_BASE + 0x40))

/* GPIOA: USART2 TX pin (PA2) */
#define GPIOA_BASE      0x40020000UL
#define GPIOA_MODER     (*(volatile uint32_t *)(GPIOA_BASE + 0x00))
#define GPIOA_AFRL      (*(volatile uint32_t *)(GPIOA_BASE + 0x20))

/* GPIOD: Discovery board LEDs on PD12..PD15 */
#define GPIOD_BASE      0x40020C00UL
#define GPIOD_MODER     (*(volatile uint32_t *)(GPIOD_BASE + 0x00))
#define GPIOD_ODR       (*(volatile uint32_t *)(GPIOD_BASE + 0x14))

/* USART2: debug console */
#define USART2_BASE     0x40004400UL
#define USART2_SR       (*(volatile uint32_t *)(USART2_BASE + 0x00))
#define USART2_DR       (*(volatile uint32_t *)(USART2_BASE + 0x04))
#define USART2_BRR      (*(volatile uint32_t *)(USART2_BASE + 0x08))
#define USART2_CR1      (*(volatile uint32_t *)(USART2_BASE + 0x0C))

/* SysTick: timer inside the ARM Cortex-M core (same address on every Cortex-M) */
#define SYSTICK_BASE    0xE000E010UL
#define SYST_CSR        (*(volatile uint32_t *)(SYSTICK_BASE + 0x00)) /* control and status */
#define SYST_RVR        (*(volatile uint32_t *)(SYSTICK_BASE + 0x04)) /* reload value       */
#define SYST_CVR        (*(volatile uint32_t *)(SYSTICK_BASE + 0x08)) /* current value      */

/* Bit positions */
#define RCC_AHB1ENR_GPIOAEN   (1UL << 0)
#define RCC_AHB1ENR_GPIODEN   (1UL << 3)
#define RCC_APB1ENR_USART2EN  (1UL << 17)
#define USART_SR_TXE          (1UL << 7)
#define USART_CR1_TE          (1UL << 3)
#define USART_CR1_UE          (1UL << 13)
#define SYST_CSR_ENABLE       (1UL << 0)   /* start counting             */
#define SYST_CSR_TICKINT      (1UL << 1)   /* raise interrupt at zero    */
#define SYST_CSR_CLKSOURCE    (1UL << 2)   /* 1 = use CPU clock directly */

/* ------------------------------------------------------------------ */
/* 2. Configuration                                                    */
/* ------------------------------------------------------------------ */

#define CPU_CLOCK_HZ          16000000UL   /* internal HSI oscillator after reset */
#define TICK_RATE_HZ          1000UL       /* 1 tick = 1 ms */

#define HEARTBEAT_LED_PIN     12           /* PD12 = green  */
#define ACTIVITY_LED_PIN      13           /* PD13 = orange */

#define HEARTBEAT_PERIOD_MS   1000UL
#define ACTIVITY_PERIOD_MS    250UL

/* ------------------------------------------------------------------ */
/* 3. SysTick driver                                                   */
/* ------------------------------------------------------------------ */

/* Milliseconds since boot. Written by the interrupt, read by main. */
static volatile uint32_t g_ms_ticks = 0;

/*
 * Interrupt Service Routine (ISR).
 * The name must match the vector table in the startup file exactly.
 * The CPU calls this automatically every 1 ms - we never call it ourselves.
 */
void SysTick_Handler(void)
{
    g_ms_ticks++;
}

static void systick_init(void)
{
    SYST_RVR = (CPU_CLOCK_HZ / TICK_RATE_HZ) - 1UL;   /* 16000 - 1 = 15999 */
    SYST_CVR = 0;                                      /* clear current count */
    SYST_CSR = SYST_CSR_CLKSOURCE | SYST_CSR_TICKINT | SYST_CSR_ENABLE;
}

static uint32_t millis(void)
{
    return g_ms_ticks;
}

/* Blocking wait, but now accurate. Use only during startup. */
static void delay_ms(uint32_t ms)
{
    uint32_t start = millis();
    while ((millis() - start) < ms) {
        /* wait */
    }
}

/* ------------------------------------------------------------------ */
/* 4. GPIO driver (LEDs)                                               */
/* ------------------------------------------------------------------ */

static void led_init(uint32_t pin)
{
    RCC_AHB1ENR |= RCC_AHB1ENR_GPIODEN;
    GPIOD_MODER &= ~(3UL << (pin * 2));
    GPIOD_MODER |=  (1UL << (pin * 2));
}

static void led_toggle(uint32_t pin)
{
    GPIOD_ODR ^= (1UL << pin);
}

static int led_is_on(uint32_t pin)
{
    return (GPIOD_ODR >> pin) & 1UL;
}

/* ------------------------------------------------------------------ */
/* 5. UART driver                                                      */
/* ------------------------------------------------------------------ */

static void uart2_init(void)
{
    RCC_AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC_APB1ENR |= RCC_APB1ENR_USART2EN;

    GPIOA_MODER &= ~(3UL << (2 * 2));
    GPIOA_MODER |=  (2UL << (2 * 2));
    GPIOA_AFRL  &= ~(0xFUL << (2 * 4));
    GPIOA_AFRL  |=  (7UL   << (2 * 4));

    USART2_BRR = (8UL << 4) | 11UL;            /* 115200 baud at 16 MHz */
    USART2_CR1 = USART_CR1_TE | USART_CR1_UE;
}

static void uart2_putc(char c)
{
    while ((USART2_SR & USART_SR_TXE) == 0) {
        /* wait */
    }
    USART2_DR = (uint32_t)c;
}

static void uart2_puts(const char *s)
{
    while (*s) {
        if (*s == '\n') {
            uart2_putc('\r');
        }
        uart2_putc(*s++);
    }
}

/* Print an unsigned number in decimal, e.g. 1234 -> "1234" */
static void uart2_put_uint(uint32_t value)
{
    char buf[10];          /* max 4294967295 = 10 digits */
    int  i = 0;

    do {
        buf[i++] = (char)('0' + (value % 10));   /* take the last digit */
        value /= 10;                              /* drop the last digit */
    } while (value > 0);

    while (i > 0) {
        uart2_putc(buf[--i]);                     /* digits were stored backwards */
    }
}

/* ------------------------------------------------------------------ */
/* 6. Application                                                      */
/* ------------------------------------------------------------------ */

int main(void)
{
    uint32_t last_heartbeat = 0;
    uint32_t last_activity  = 0;
    uint32_t uptime_s       = 0;

    led_init(HEARTBEAT_LED_PIN);
    led_init(ACTIVITY_LED_PIN);
    uart2_init();
    systick_init();

    uart2_puts("\n=================================\n");
    uart2_puts(" Fab Chamber Controller v0.2\n");
    uart2_puts(" SysTick running at 1 kHz\n");
    uart2_puts("=================================\n");

    uart2_puts("Self-test: waiting 500 ms... ");
    delay_ms(500);
    uart2_puts("done at t = ");
    uart2_put_uint(millis());
    uart2_puts(" ms\n");

    while (1) {
        uint32_t now = millis();

        /* Task 1: heartbeat every 1000 ms */
        if ((now - last_heartbeat) >= HEARTBEAT_PERIOD_MS) {
            last_heartbeat += HEARTBEAT_PERIOD_MS;
            uptime_s++;

            led_toggle(HEARTBEAT_LED_PIN);

            uart2_puts("[t=");
            uart2_put_uint(now);
            uart2_puts(" ms] Uptime ");
            uart2_put_uint(uptime_s);
            uart2_puts(" s, heartbeat LED ");
            uart2_puts(led_is_on(HEARTBEAT_LED_PIN) ? "ON\n" : "OFF\n");
        }

        /* Task 2: activity LED every 250 ms */
        if ((now - last_activity) >= ACTIVITY_PERIOD_MS) {
            last_activity += ACTIVITY_PERIOD_MS;
            led_toggle(ACTIVITY_LED_PIN);
        }
    }
}
