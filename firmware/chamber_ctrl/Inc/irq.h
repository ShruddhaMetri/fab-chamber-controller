/*
 * irq.h
 * Disable / enable all interrupts, for short critical sections.
 *   cpsid i = "change processor state: interrupts disabled"
 *   cpsie i = "change processor state: interrupts enabled"
 * Keep the code between them as short as possible: while interrupts are off,
 * the e-stop, UART and SysTick all have to wait.
 */
#ifndef IRQ_H
#define IRQ_H

static inline void irq_disable(void) { __asm volatile ("cpsid i" ::: "memory"); }
static inline void irq_enable(void)  { __asm volatile ("cpsie i" ::: "memory"); }

#endif /* IRQ_H */
