/*
 * uart.h
 * USART2 driver: interrupt-driven receive, polled transmit.
 * PA2 = TX, PA3 = RX, 115200 baud, 8 data bits, no parity, 1 stop bit.
 */
#ifndef UART_H
#define UART_H

#include <stdint.h>

void uart2_init(void);
void uart2_putc(char c);
void uart2_puts(const char *s);
void uart2_put_uint(uint32_t value);

/* Returns the next received byte (0-255), or -1 if nothing has arrived */
int  uart2_getc(void);

/* Step 11: connect to FreeRTOS - creates the transmit mutex and remembers
 * which task to wake when a byte arrives. Call before the scheduler starts. */
void uart2_rtos_init(void *console_task);   /* pass the console's TaskHandle_t */

/* Unprotected output for fatal-error messages only */
void uart2_panic_puts(const char *s);

#endif /* UART_H */
