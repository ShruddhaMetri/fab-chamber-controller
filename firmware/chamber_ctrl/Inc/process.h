/*
 * process.h
 * Automatic recipe execution: a state machine that runs one complete CVD
 * deposition from a stored recipe, step by step, with no operator action.
 *
 *   IDLE -> PUMPDOWN -> LEAKCHECK -> HEAT -> STABILIZE -> GAS_ON
 *        -> DEPOSIT -> PURGE -> COOL -> DONE
 *
 * Any fault, timeout, out-of-range reading or 'abort' command sends it to
 * ABORTED, which makes the chamber safe (gases off, heater off, valve open).
 *
 * The state machine runs in its own FreeRTOS task (Process). The console
 * never calls it directly: it sends a message through a queue.
 */
#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>
#include "recipe.h"

typedef enum {
    PS_IDLE = 0,
    PS_PUMPDOWN,       /* valve open, pump the chamber down to base pressure     */
    PS_LEAKCHECK,      /* valve closed: pressure must not rise (rate-of-rise)    */
    PS_HEAT,           /* PID heats to the recipe temperature                    */
    PS_STABILIZE,      /* temperature must stay in band for a while              */
    PS_GAS_ON,         /* gases on, pressure controller brings pressure in band  */
    PS_DEPOSIT,        /* the film grows: hold everything for the recipe time    */
    PS_PURGE,          /* gases off, valve open: remove remaining process gas    */
    PS_COOL,           /* heater off, wait until safe to handle                  */
    PS_DONE,           /* finished successfully                                  */
    PS_ABORTED,        /* stopped early; chamber made safe                       */
    PS_COUNT
} proc_state_t;

/* Snapshot for the 'proc' command */
typedef struct {
    proc_state_t state;
    char         recipe_name[RECIPE_NAME_LEN];
    uint32_t     run_s;            /* seconds since 'run'                        */
    uint32_t     state_s;          /* seconds in the current state               */
    uint32_t     remaining_s;      /* DEPOSIT only: seconds still to go          */
    uint32_t     pressure_sp;      /* pressure setpoint, mTorr (0 = no control)  */
    char         reason[40];       /* why it aborted, or empty                   */
} proc_status_t;

void        process_init(void);              /* creates the command queue        */
void        process_task(void *arg);         /* the FreeRTOS task body           */

/* Called by the console. Return 1 if the message was queued. */
int         process_request_run(const recipe_t *r);
int         process_request_abort(void);

int         process_is_running(void);        /* 1 from PUMPDOWN to COOL          */
void        process_get_status(proc_status_t *st);
const char *process_state_name(proc_state_t s);

#endif /* PROCESS_H */
