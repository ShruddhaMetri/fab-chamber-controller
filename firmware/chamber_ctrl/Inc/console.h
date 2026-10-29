/*
 * console.h
 * Simple command console on USART2: type a command, press Enter.
 */
#ifndef CONSOLE_H
#define CONSOLE_H

void console_init(void);   /* print the first prompt                         */
void console_poll(void);   /* call often from the main loop: handles input   */

#endif /* CONSOLE_H */
