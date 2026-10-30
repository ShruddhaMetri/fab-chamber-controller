/*
 * process.c
 * Recipe execution state machine + chamber pressure controller.
 *
 * A STATE MACHINE is a program that is always in exactly one named state,
 * does that state's job every cycle, and moves to another state only when
 * a clear condition becomes true ("temperature in band for 10 s"). Every
 * state also has a TIMEOUT, so a stuck process can never wait forever.
 *
 * This task wakes every 100 ms OR as soon as a command message arrives:
 * xQueueReceive() with a timeout does both jobs in one call.
 */
#include <string.h>
#include "process.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "app_config.h"
#include "uart.h"
#include "sensors.h"
#include "actuators.h"
#include "gas.h"
#include "safety.h"
#include "temp_ctrl.h"

#define PERIOD_MS            100U

/* ---- Limits for each step (would be part of the tool's qualification) ---- */
#define BASE_PRESSURE_MTORR   50U      /* pump-down target                     */
#define PUMPDOWN_TIMEOUT_S    90U
#define LEAK_SETTLE_S         1U       /* let the gauge settle after closing   */
#define LEAK_TEST_S           10U      /* measure pressure rise for this long  */
#define LEAK_MAX_RISE_MTORR   20U      /* more than this = a leak              */
#define TEMP_BAND_DC          30U      /* +/-3.0 C counts as "at temperature"  */
#define HEAT_TIMEOUT_S        300U
#define STABLE_HOLD_S         10U      /* must stay in band this long          */
#define STABLE_TIMEOUT_S      120U
#define PRESS_BAND_PCT        5U       /* +/-5 % counts as "at pressure"       */
#define PRESS_HOLD_S          3U
#define GAS_TIMEOUT_S         60U
#define DEP_TEMP_BAND_DC      100U     /* during deposition: +/-10 C allowed   */
#define DEP_PRESS_BAND_PCT    20U      /* ... and +/-20 % pressure             */
#define EXCURSION_MAX_S       5U       /* out of band longer than this = abort */
#define PURGE_MIN_S           10U
#define PURGE_DONE_MTORR      100U
#define PURGE_TIMEOUT_S       60U
#define COOL_DONE_DC          1000U    /* 100.0 C: safe to unload the wafer    */
#define COOL_TIMEOUT_S        600U

/* ---- Pressure controller (PI on the throttle valve) ----
 * More pressure than wanted -> open the valve (the pump removes gas faster).
 * Units: milli-per-mille of valve opening, as in the temperature PID.
 * Gains chosen by simulating the chamber model offline. */
#define PC_KP                 400      /* per mTorr of error                   */
#define PC_KI                 100      /* per mTorr of error per second        */
#define PC_MIN                (20L * 1000L)    /* never below 2 % open          */
#define PC_MAX                (1000L * 1000L)  /* 100 % open                    */

/* ---- Command messages from the console ---- */
typedef enum { CMD_RUN = 1, CMD_ABORT = 2 } cmd_id_t;

typedef struct {
    cmd_id_t cmd;
    recipe_t recipe;            /* copied INTO the queue: no shared memory */
} proc_msg_t;

static QueueHandle_t g_queue = NULL;

/* ---- State (owned by the Process task) ---- */
static volatile proc_state_t state = PS_IDLE;   /* read by the console too */
static recipe_t     rcp;
static uint32_t     run_start   = 0U;   /* ms */
static uint32_t     run_end     = 0U;   /* ms, when it finished or aborted */
static uint32_t     state_start = 0U;   /* ms */
static int          cond_on     = 0;    /* is the "ok" condition currently true?   */
static uint32_t     cond_since  = 0U;   /* ms: when it became true                 */
static int          bad_on      = 0;    /* is a deposition excursion in progress?  */
static uint32_t     bad_since   = 0U;   /* ms: when it began                       */
static int          leak_on     = 0;    /* leak measurement started?               */
static uint32_t     leak_p0     = 0U;   /* mTorr at start of the leak test         */
static uint32_t     pc_sp       = 0U;   /* pressure setpoint, 0 = controller off */
static int32_t      pc_i        = 0;
static char         reason[40]  = "";

static const char *const names[PS_COUNT] = {
    "IDLE", "PUMPDOWN", "LEAKCHECK", "HEAT", "STABILIZE",
    "GAS_ON", "DEPOSIT", "PURGE", "COOL", "DONE", "ABORTED"
};

const char *process_state_name(proc_state_t s)
{
    return (s < PS_COUNT) ? names[s] : "?";
}

static uint32_t now_ms(void)       { return (uint32_t)xTaskGetTickCount(); }   /* 1 tick = 1 ms */
static uint32_t in_state_s(void)   { return (now_ms() - state_start) / 1000U; }

/* ---------------- Printing helpers ---------------- */
static void put_mtorr(uint32_t m)       /* 2000 -> "2.000" */
{
    uint32_t f = m % 1000U;
    uart2_put_uint(m / 1000U);
    uart2_putc('.');
    uart2_putc((char)('0' + f / 100U));
    uart2_putc((char)('0' + (f / 10U) % 10U));
    uart2_putc((char)('0' + f % 10U));
}

static void log_line(const char *text)  /* "\n[t=  37 s] text" */
{
    uart2_puts("\n[t=");
    uart2_put_uint((now_ms() - run_start) / 1000U);
    uart2_puts(" s] ");
    uart2_puts(text);
}

/* ---------------- Pressure controller ---------------- */
static void pc_enable(uint32_t setpoint_mtorr)
{
    pc_sp = setpoint_mtorr;
    pc_i  = (int32_t)valve_get_permille() * 1000;   /* bumpless: start from where the valve is */
}

static void pc_disable(void)
{
    pc_sp = 0U;
}

static void pc_poll(void)
{
    int32_t e, p, di, trial, out;

    if (pc_sp == 0U) {
        return;
    }
    e  = (int32_t)sensors_pressure_mtorr() - (int32_t)pc_sp;   /* + = too high -> open */
    p  = PC_KP * e;
    di = (PC_KI * e * (int32_t)PERIOD_MS) / 1000;

    trial = p + pc_i + di;                           /* anti-windup, as in the PID */
    if (!((trial >= PC_MAX && e > 0) || (trial <= PC_MIN && e < 0))) {
        pc_i += di;
    }
    if (pc_i < PC_MIN) { pc_i = PC_MIN; }
    if (pc_i > PC_MAX) { pc_i = PC_MAX; }

    out = p + pc_i;
    if (out < PC_MIN) { out = PC_MIN; }
    if (out > PC_MAX) { out = PC_MAX; }
    (void)valve_set_permille((uint32_t)(out / 1000));   /* refused if a fault is active */
}

/* Normal gas shut-off: set both setpoints to 0 through the gas module, so
 * the flow check expects 0. (gas_all_off() only writes the DAC registers; it
 * is meant for the safety interrupt, where a fault is already latched.) */
static void gases_off(void)
{
    (void)gas_set_sccm(GAS_MFC1, 0U);
    (void)gas_set_sccm(GAS_MFC2, 0U);
}

/* ---------------- State changes ---------------- */
static void enter(proc_state_t s)
{
    taskENTER_CRITICAL();               /* 'proc' may read these at any moment */
    state       = s;
    state_start = now_ms();
    cond_on     = 0;
    bad_on      = 0;
    leak_on     = 0;
    if (s == PS_DONE || s == PS_ABORTED) {
        run_end = state_start;          /* freeze the run time */
    }
    taskEXIT_CRITICAL();
}

/* Make the chamber safe and stop. Used for every kind of abort. */
static void abort_run(const char *why, int prompt)
{
    pc_disable();
    gases_off();
    (void)tc_set_setpoint_c(0U);        /* PID off, heater 0 */
    (void)valve_set_permille(1000U);    /* pump the gas away */

    taskENTER_CRITICAL();
    strncpy(reason, why, sizeof(reason) - 1U);
    reason[sizeof(reason) - 1U] = '\0';
    taskEXIT_CRITICAL();

    uart2_puts("\n[t=");
    uart2_put_uint((now_ms() - run_start) / 1000U);
    uart2_puts(" s] *** PROCESS ABORTED in ");
    uart2_puts(names[state]);
    uart2_puts(": ");
    uart2_puts(why);
    uart2_puts("\n    Chamber safe: gases off, heater off, valve open.\n");
    if (prompt) {
        uart2_puts("> ");               /* not when the console prints its own */
    }
    enter(PS_ABORTED);
}

/* A condition must be continuously true for hold_s seconds */
static int held_for(int ok, uint32_t hold_s)
{
    if (!ok) {
        cond_on = 0;                    /* any break restarts the hold time */
        return 0;
    }
    if (!cond_on) {
        cond_on    = 1;
        cond_since = now_ms();
    }
    return (now_ms() - cond_since) >= (hold_s * 1000U);
}

static int temp_in_band(uint32_t band_dc)
{
    int32_t d = (int32_t)sensors_temp_dc() - (int32_t)rcp.temp_c * 10;
    return (d >= -(int32_t)band_dc) && (d <= (int32_t)band_dc);
}

static int press_in_band(uint32_t pct)
{
    int32_t sp = (int32_t)rcp.pressure_mtorr;
    int32_t d  = (int32_t)sensors_pressure_mtorr() - sp;
    int32_t b  = (sp * (int32_t)pct) / 100;
    return (d >= -b) && (d <= b);
}

static void timeout_check(uint32_t limit_s)
{
    if (in_state_s() >= limit_s) {
        abort_run("step timed out", 1);
    }
}

/* ---------------- Start a run ---------------- */
static void start_run(const recipe_t *r)
{
    if ((state != PS_IDLE) && (state != PS_DONE) && (state != PS_ABORTED)) {
        uart2_puts("\nRefused: a process is already running.\n");
        return;
    }
    if (safety_is_faulted()) {
        uart2_puts("\nRefused: fault active. Clear it first.\n");
        return;
    }
    rcp = *r;
    taskENTER_CRITICAL();
    reason[0] = '\0';
    taskEXIT_CRITICAL();
    run_start = now_ms();

    log_line("RUN recipe '");
    uart2_puts(rcp.name);
    uart2_puts("'\n");

    gases_off();
    (void)tc_set_setpoint_c(0U);
    (void)valve_set_permille(1000U);
    log_line("PUMPDOWN: valve open, pumping to base pressure\n");
    enter(PS_PUMPDOWN);
}

/* ---------------- One cycle of the state machine (every 100 ms) ---------------- */
static void step(void)
{
    uint32_t p = sensors_pressure_mtorr();

    /* A latched fault always wins, in every running state */
    if (process_is_running() && safety_is_faulted()) {
        uint8_t f = safety_fault_source();
        abort_run((f & FAULT_ESTOP)    ? "fault - emergency stop" :
                  (f & FAULT_DOOR)     ? "fault - door opened" :
                  (f & FAULT_OVERTEMP) ? "fault - over-temperature" :
                                         "fault - gas flow", 1);
        return;
    }

    switch (state) {
    case PS_PUMPDOWN:
        if (p < BASE_PRESSURE_MTORR) {
            (void)valve_set_permille(0U);
            log_line("LEAKCHECK: valve closed, base ");
            put_mtorr(p);
            uart2_puts(" Torr, watching for pressure rise\n> ");
            enter(PS_LEAKCHECK);
        } else {
            timeout_check(PUMPDOWN_TIMEOUT_S);
        }
        break;

    case PS_LEAKCHECK:
        if (!leak_on && in_state_s() >= LEAK_SETTLE_S) {
            leak_on = 1;                               /* start of the measurement */
            leak_p0 = p;
        } else if (leak_on && in_state_s() >= (LEAK_SETTLE_S + LEAK_TEST_S)) {
            uint32_t rise = (p > leak_p0) ? (p - leak_p0) : 0U;
            (void)valve_set_permille(1000U);
            if (rise > LEAK_MAX_RISE_MTORR) {
                abort_run("leak check failed", 1);
            } else {
                log_line("Leak check passed (rise ");
                uart2_put_uint(rise);
                uart2_puts(" mTorr in 10 s)\n");
                (void)tc_set_setpoint_c(rcp.temp_c);
                log_line("HEAT: PID setpoint ");
                uart2_put_uint(rcp.temp_c);
                uart2_puts(" C\n> ");
                enter(PS_HEAT);
            }
        }
        break;

    case PS_HEAT:
        if (temp_in_band(TEMP_BAND_DC)) {
            log_line("STABILIZE: at temperature, holding 10 s\n> ");
            enter(PS_STABILIZE);
        } else {
            timeout_check(HEAT_TIMEOUT_S);
        }
        break;

    case PS_STABILIZE:
        if (held_for(temp_in_band(TEMP_BAND_DC), STABLE_HOLD_S)) {
            (void)gas_set_sccm(GAS_MFC1, rcp.gas1_sccm);
            (void)gas_set_sccm(GAS_MFC2, rcp.gas2_sccm);
            pc_enable(rcp.pressure_mtorr);
            log_line("GAS_ON: SiH4 ");
            uart2_put_uint(rcp.gas1_sccm);
            uart2_puts(" + N2O ");
            uart2_put_uint(rcp.gas2_sccm);
            uart2_puts(" sccm, pressure control to ");
            put_mtorr(rcp.pressure_mtorr);
            uart2_puts(" Torr\n> ");
            enter(PS_GAS_ON);
        } else {
            timeout_check(STABLE_TIMEOUT_S);
        }
        break;

    case PS_GAS_ON:
        pc_poll();
        if (held_for(press_in_band(PRESS_BAND_PCT), PRESS_HOLD_S)) {
            log_line("DEPOSIT: ");
            uart2_put_uint(rcp.time_s);
            uart2_puts(" s\n> ");
            enter(PS_DEPOSIT);
        } else {
            timeout_check(GAS_TIMEOUT_S);
        }
        break;

    case PS_DEPOSIT:
        pc_poll();
        /* Excursion check: a short wobble is tolerated, a lasting one is not */
        if (temp_in_band(DEP_TEMP_BAND_DC) && press_in_band(DEP_PRESS_BAND_PCT)) {
            bad_on = 0;
        } else if (!bad_on) {
            bad_on    = 1;
            bad_since = now_ms();
        } else if ((now_ms() - bad_since) >= (EXCURSION_MAX_S * 1000U)) {
            abort_run(temp_in_band(DEP_TEMP_BAND_DC) ? "pressure out of range"
                                                     : "temperature out of range", 1);
            break;
        }
        if (in_state_s() >= rcp.time_s) {
            pc_disable();
            gases_off();
            (void)valve_set_permille(1000U);
            log_line("PURGE: gases off, valve open\n> ");
            enter(PS_PURGE);
        }
        break;

    case PS_PURGE:
        if ((in_state_s() >= PURGE_MIN_S) && (p < PURGE_DONE_MTORR)) {
            (void)tc_set_setpoint_c(0U);
            log_line("COOL: heater off, waiting for 100 C\n> ");
            enter(PS_COOL);
        } else {
            timeout_check(PURGE_TIMEOUT_S);
        }
        break;

    case PS_COOL:
        if (sensors_temp_dc() < COOL_DONE_DC) {
            log_line("DONE: recipe '");
            uart2_puts(rcp.name);
            uart2_puts("' completed in ");
            uart2_put_uint((now_ms() - run_start) / 1000U);
            uart2_puts(" s\n> ");
            enter(PS_DONE);
        } else {
            timeout_check(COOL_TIMEOUT_S);
        }
        break;

    default:                            /* IDLE, DONE, ABORTED: nothing to do */
        break;
    }
}

/* ---------------- The task ---------------- */
void process_init(void)
{
    g_queue = xQueueCreate(2U, sizeof(proc_msg_t));
    configASSERT(g_queue != NULL);
}

void process_task(void *arg)
{
    proc_msg_t msg;
    TickType_t next = xTaskGetTickCount() + pdMS_TO_TICKS(PERIOD_MS);
    (void)arg;

    for (;;) {
        TickType_t now  = xTaskGetTickCount();
        TickType_t wait = ((int32_t)(next - now) > 0) ? (next - now) : 0U;

        /* Sleep until a command arrives OR the next 100 ms period is due */
        if (xQueueReceive(g_queue, &msg, wait) == pdPASS) {
            if (msg.cmd == CMD_RUN) {
                start_run(&msg.recipe);
            } else if (process_is_running()) {
                abort_run("operator abort", 0);
            }
        } else {
            next += pdMS_TO_TICKS(PERIOD_MS);
            step();
        }
    }
}

/* ---------------- Called from the Console task ---------------- */
int process_request_run(const recipe_t *r)
{
    proc_msg_t m;
    m.cmd    = CMD_RUN;
    m.recipe = *r;
    return xQueueSend(g_queue, &m, 0U) == pdPASS;     /* 0 = do not wait if full */
}

int process_request_abort(void)
{
    proc_msg_t m;
    memset(&m, 0, sizeof(m));
    m.cmd = CMD_ABORT;
    return xQueueSend(g_queue, &m, 0U) == pdPASS;
}

int process_is_running(void)
{
    proc_state_t s = state;              /* one 32-bit read: atomic */
    return (s >= PS_PUMPDOWN) && (s <= PS_COOL);
}

void process_get_status(proc_status_t *st)
{
    taskENTER_CRITICAL();                /* a consistent snapshot */
    st->state       = state;
    st->run_s       = (state == PS_IDLE) ? 0U :
                      (((state == PS_DONE || state == PS_ABORTED) ? run_end : now_ms()) - run_start) / 1000U;
    st->state_s     = in_state_s();
    st->remaining_s = (state == PS_DEPOSIT && rcp.time_s > st->state_s)
                      ? (rcp.time_s - st->state_s) : 0U;
    st->pressure_sp = pc_sp;
    memcpy(st->recipe_name, rcp.name, RECIPE_NAME_LEN);
    memcpy(st->reason, reason, sizeof(st->reason));
    taskEXIT_CRITICAL();
}
