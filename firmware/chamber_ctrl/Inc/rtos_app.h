/*
 * rtos_app.h
 * The application's FreeRTOS tasks, and the shared resources between them.
 *
 *   Task       Priority  Runs                 Job
 *   ---------  --------  -------------------  ---------------------------------
 *   Safety     4 (top)   every 5 ms           debounce e-stop/door, report trips
 *   Control    3         every 100 ms         gas check, sensors, PID
 *   Process    2         100 ms + commands    recipe state machine, pressure control
 *   Logger     2         every 100 ms         data log to SPI flash
 *   Console    1         when a key arrives   commands
 *   Heartbeat  1         every 250 ms         status LEDs
 *   (Idle      0         when nothing else    created by FreeRTOS itself)
 *
 * The actual safety ACTION (heater off, valve open) still happens in the
 * e-stop/door interrupts, which sit above the kernel and are never delayed
 * by any task.
 */
#ifndef RTOS_APP_H
#define RTOS_APP_H

#include <stdint.h>

/* Creates mutexes and tasks and starts the scheduler. Never returns. */
void rtos_app_start(void);

/* The SPI flash is used by the Logger task and by console 'log' commands.
 * Hold this lock for the whole of any flash access. */
void flash_lock(void);
void flash_unlock(void);

/* For the 'tasks' command */
void rtos_print_tasks(void);

#endif /* RTOS_APP_H */
