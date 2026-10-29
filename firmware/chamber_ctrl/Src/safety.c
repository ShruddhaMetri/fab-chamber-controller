/*
 * safety.c
 * Emergency-stop and door interlocks using external interrupts (EXTI).
 *
 * - The interrupt trips the fault the instant an input becomes active,
 *   and forces the actuators into the safe state (heater off, valve open).
 * - The main loop samples the inputs every 5 ms to debounce them, reports
 *   events on the console, and drives the red fault LED.
 * - The fault stays latched until the operator types "clear" AND both
 *   inputs have been stably inactive.
 */
#include "safety.h"
#include "stm32f407_regs.h"
#include "app_config.h"
#include "gpio.h"
#include "uart.h"
#include "actuators.h"

#define ESTOP_PIN   0U      /* PC0 */
#define DOOR_PIN    1U      /* PC1 */

/* ---------------- Interrupt enable / disable (critical sections) ---------------- */
/* cpsid i = "disable interrupts", cpsie i = "enable interrupts" (ARM instructions) */
static inline void irq_disable(void) { __asm volatile ("cpsid i" ::: "memory"); }
static inline void irq_enable(void)  { __asm volatile ("cpsie i" ::: "memory"); }

/* ---------------- Debouncing ---------------- */
typedef struct {
    uint8_t stable;   /* the accepted (debounced) value: 0 or 1      */
    uint8_t count;    /* how many samples in a row disagreed with it */
} debounce_t;

static debounce_t estop_db = {0U, 0U};
static debounce_t door_db  = {0U, 0U};

/* Accept a new value only after it has been seen DEBOUNCE_SAMPLES times in a row */
static int debounce_update(debounce_t *d, uint8_t raw)
{
    if (raw == d->stable) {
        d->count = 0U;                 /* agrees with current state: nothing to do */
        return 0;
    }
    d->count++;
    if (d->count >= DEBOUNCE_SAMPLES) {
        d->stable = raw;               /* stable long enough: accept the change */
        d->count  = 0U;
        return 1;                      /* report "state changed" */
    }
    return 0;
}

/* ---------------- Shared state (written by ISRs and main code) ---------------- */
static volatile uint8_t  g_fault_source = FAULT_NONE;  /* which inputs tripped       */
static volatile uint8_t  g_new_trip     = FAULT_NONE;  /* trips not yet reported     */
static volatile uint32_t g_trip_count   = 0U;

/* Called from an ISR: latch the fault immediately */
static void trip(uint8_t source)
{
    if ((g_fault_source & source) == 0U) {
        g_trip_count++;
    }
    g_fault_source |= source;
    g_new_trip     |= source;
    actuators_safe_state();            /* heater OFF, valve OPEN: within microseconds */
    led_on(LED_RED);                   /* fast visual alarm, a single atomic write */
}

/* ---------------- Raw pin reads ---------------- */
static uint8_t read_estop_raw(void) { return (uint8_t)((GPIO_IDR(GPIOC_BASE) >> ESTOP_PIN) & 1UL); }
static uint8_t read_door_raw(void)  { return (uint8_t)((GPIO_IDR(GPIOC_BASE) >> DOOR_PIN)  & 1UL); }

/* ---------------- Interrupt handlers ---------------- */
/* Names must match the vector table in the startup file exactly */
void EXTI0_IRQHandler(void)
{
    if (EXTI_PR & (1UL << ESTOP_PIN)) {
        EXTI_PR = (1UL << ESTOP_PIN);  /* clear pending flag: write 1 to clear */
        trip(FAULT_ESTOP);
    }
}

void EXTI1_IRQHandler(void)
{
    if (EXTI_PR & (1UL << DOOR_PIN)) {
        EXTI_PR = (1UL << DOOR_PIN);
        trip(FAULT_DOOR);
    }
}

/* ---------------- Initialisation ---------------- */
static void input_pin_init(uint32_t base, uint32_t pin)
{
    GPIO_MODER(base) &= ~(3UL << (pin * 2U));     /* 00 = input                    */
    GPIO_PUPDR(base) &= ~(3UL << (pin * 2U));
    GPIO_PUPDR(base) |=  (2UL << (pin * 2U));     /* 10 = pull-down: idle reads 0  */
}

void safety_init(void)
{
    /* 1. Clocks: GPIOC, and SYSCFG (needed to route pins to EXTI) */
    RCC_AHB1ENR |= RCC_AHB1ENR_GPIOCEN;
    RCC_APB2ENR |= RCC_APB2ENR_SYSCFGEN;

    /* 2. Pins as inputs with pull-downs */
    input_pin_init(GPIOC_BASE, ESTOP_PIN);
    input_pin_init(GPIOC_BASE, DOOR_PIN);

    /* 3. Route EXTI lines 0 and 1 to port C (4 bits per line) */
    SYSCFG_EXTICR1 &= ~((0xFUL << 0) | (0xFUL << 4));
    SYSCFG_EXTICR1 |=  ((EXTI_PORT_C << 0) | (EXTI_PORT_C << 4));

    /* 4. Interrupt on the rising edge (input going 0 -> 1 = becoming active) */
    EXTI_RTSR |=  ((1UL << ESTOP_PIN) | (1UL << DOOR_PIN));
    EXTI_FTSR &= ~((1UL << ESTOP_PIN) | (1UL << DOOR_PIN));
    EXTI_PR    =  ((1UL << ESTOP_PIN) | (1UL << DOOR_PIN));   /* clear anything old */
    EXTI_IMR  |=  ((1UL << ESTOP_PIN) | (1UL << DOOR_PIN));   /* unmask the lines   */

    /* 5. Priorities: safety inputs most urgent (0), console less urgent (8) */
    NVIC_IPR(EXTI0_IRQn)  = 0x00U;
    NVIC_IPR(EXTI1_IRQn)  = 0x00U;
    NVIC_IPR(USART2_IRQn) = 0x80U;

    /* 6. Enable the two interrupts in the NVIC (both are in register 0) */
    NVIC_ISER(0) = (1UL << EXTI0_IRQn) | (1UL << EXTI1_IRQn);

    /* 7. An input already active at power-on gives no edge, so check it now */
    estop_db.stable = read_estop_raw();
    door_db.stable  = read_door_raw();

    irq_disable();
    if (estop_db.stable) { trip(FAULT_ESTOP); }
    if (door_db.stable)  { trip(FAULT_DOOR);  }
    irq_enable();
}

/* ---------------- Periodic work (main loop) ---------------- */
void safety_poll(void)
{
    uint8_t new_trip;

    /* Take the "not yet reported" flags safely: the ISR could change them mid-read */
    irq_disable();
    new_trip   = g_new_trip;
    g_new_trip = FAULT_NONE;
    irq_enable();

    /* Report trips here, not in the ISR: printing is slow */
    if (new_trip & FAULT_ESTOP) {
        uart2_puts("\n!!! FAULT: EMERGENCY STOP pressed. Chamber in safe state.\n> ");
    }
    if (new_trip & FAULT_DOOR) {
        uart2_puts("\n!!! FAULT: DOOR opened. Chamber in safe state.\n> ");
    }

    /* Debounce both inputs and report when they settle back to inactive */
    if (debounce_update(&estop_db, read_estop_raw()) && (estop_db.stable == 0U)) {
        uart2_puts("\nE-stop released. Type 'clear' to reset the fault.\n> ");
    }
    if (debounce_update(&door_db, read_door_raw()) && (door_db.stable == 0U)) {
        uart2_puts("\nDoor closed. Type 'clear' to reset the fault.\n> ");
    }
}

/* ---------------- Queries for the console ---------------- */
int      safety_is_faulted(void)    { return g_fault_source != FAULT_NONE; }
uint8_t  safety_fault_source(void)  { return g_fault_source; }
int      safety_estop_pressed(void) { return estop_db.stable; }
int      safety_door_open(void)     { return door_db.stable; }
uint32_t safety_trip_count(void)    { return g_trip_count; }

int safety_clear(void)
{
    /* Never clear while an input is still active: that would hide a real hazard.
     * Check both the debounced state and the live pin. */
    if (estop_db.stable || door_db.stable || read_estop_raw() || read_door_raw()) {
        return 0;
    }
    irq_disable();
    g_fault_source = FAULT_NONE;
    irq_enable();
    led_off(LED_RED);
    return 1;
}
