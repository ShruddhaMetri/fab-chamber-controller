/*
 * health.h
 * Reliability: what happens when the SOFTWARE fails?
 *
 *   1. Independent watchdog (IWDG): a hardware timer that resets the chip
 *      unless it is "refreshed" at least once a second.
 *   2. Task supervision: the watchdog is refreshed ONLY if every critical
 *      task has checked in recently. One stuck task = no refresh = reset.
 *   3. HardFault handler: if the CPU hits an illegal operation, record WHERE
 *      (program counter, fault registers), make the chamber safe, reset.
 *   4. Reset cause: the reason is stored in RTC backup registers, which
 *      survive a reset, and reported at the next start-up.
 *   5. Low power: when no task has work, the CPU sleeps (WFI) until the next
 *      interrupt instead of spinning in the idle task.
 */
#ifndef HEALTH_H
#define HEALTH_H

#include <stdint.h>

/* Tasks that must check in regularly (one flag each) */
typedef enum {
    HC_SAFETY = 0,
    HC_CONTROL,
    HC_PROCESS,
    HC_CANLINK,
    HC_LOGGER,
    HC_COUNT
} health_client_t;

/* Why the controller restarted (kept in RTC backup register 0) */
typedef enum {
    RR_POWER_ON = 0,      /* first start: backup registers were empty       */
    RR_RUNNING,           /* marker while running: if found at boot, the reset
                             came without warning (watchdog, power dip)        */
    RR_OPERATOR,          /* 'reset' command                                 */
    RR_SUPERVISOR,        /* a task stopped checking in -> watchdog reset    */
    RR_HARDFAULT,         /* CPU fault                                       */
    RR_FATAL,             /* stack overflow, out of memory, failed assert    */
    RR_UPDATE             /* restart to let the bootloader install an update */
} reset_reason_t;

void health_boot_report(void);             /* call early in main(): prints why we restarted */
void health_watchdog_start(void);          /* call just before the scheduler starts         */
void health_watchdog_set_timeout(uint32_t ms);   /* longer while erasing flash (max 4095)  */
void health_checkin(health_client_t c);    /* each supervised task, every cycle             */
void health_task(void *arg);               /* the Watchdog supervisor task                  */

/* Store a reason, then reset the chip. Never returns. */
void health_reset(reset_reason_t why, uint32_t detail, const char *text);

/* For the console */
void health_print(void);
void health_test_hang(void);               /* 'crash hang'  : Control task stops      */
void health_test_fault(void);              /* 'crash fault' : illegal instruction     */
int  health_hang_requested(void);

#endif /* HEALTH_H */
