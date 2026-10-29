/*
 * safety.h
 * Chamber safety inputs: emergency-stop button and door switch.
 *
 *   E-stop : PC0, EXTI line 0, high = pressed
 *   Door   : PC1, EXTI line 1, high = open
 *   Over-temperature: from the sensors module (ADC), checked every 100 ms
 *
 * Rule: trip instantly (interrupt), recover deliberately (debounced + operator clear).
 */
#ifndef SAFETY_H
#define SAFETY_H

#include <stdint.h>

/* Bit flags saying which input caused the fault */
#define FAULT_NONE    0x00U
#define FAULT_ESTOP   0x01U
#define FAULT_DOOR    0x02U
#define FAULT_OVERTEMP 0x04U

void     safety_init(void);        /* configure pins, EXTI and NVIC                 */
void     safety_poll(void);        /* call every SAFETY_SAMPLE_MS from the main loop */

int      safety_is_faulted(void);  /* 1 while a fault is latched                    */
uint8_t  safety_fault_source(void);/* FAULT_ESTOP and/or FAULT_DOOR                 */
int      safety_estop_pressed(void); /* debounced state                              */
int      safety_door_open(void);     /* debounced state                              */
uint32_t safety_trip_count(void);  /* how many times a fault has tripped            */

/* Called by the sensors module (main loop, not an ISR) */
void     safety_trip_overtemp(void);

/* Try to clear the fault. Returns 1 if cleared, 0 if a hazard is still present. */
int      safety_clear(void);

#endif /* SAFETY_H */
