/*
 * temp_ctrl.c
 * Discrete PID controller in fixed-point (integer) arithmetic.
 *
 * Every 100 ms:
 *   error  e = setpoint - measured                       (tenths of a degree)
 *   P      = Kp x e                                      (react to the present)
 *   I     += Ki x e x dt                                 (remember the past)
 *   D      = -Kd x (change in measured temperature)/dt   (anticipate the future)
 *   heater = P + I + D, limited to 0..100 %
 *
 * Internal unit: "milli-per-mille" (1/1000 of 0.1 %), so integer maths keeps
 * three extra decimal places. Divide by 1000 to get per-mille for the PWM.
 *
 * Design details that matter in practice:
 *   - Anti-windup: the integral stops growing while the heater is already at
 *     0 % or 100 %. Without this it would keep growing during the long
 *     heat-up and cause a big overshoot later.
 *   - Derivative on measurement: D uses the change in TEMPERATURE, not in
 *     error, so changing the setpoint does not cause a sudden spike ("kick").
 *   - Bumpless start: when AUTO is switched on, the controller starts from
 *     the current temperature with an empty integral.
 *   - Fault: any latched fault switches AUTO off. The operator must clear the
 *     fault and set the temperature again; it never restarts on its own.
 */
#include "temp_ctrl.h"
#include "sensors.h"
#include "actuators.h"
#include "safety.h"
#include "uart.h"
#include "app_config.h"

#define DT_MS          SENSOR_PERIOD_MS          /* 100 ms between updates     */
#define OUT_MAX        (1000L * 1000L)           /* 100.0 % in milli-per-mille */

/* Products like Kd x change x 1000 can exceed the 32-bit range (about 2.1e9)
 * and silently wrap around to a wrong sign. They are computed in 64 bits and
 * then limited, so a large gain or a sensor glitch can never flip the heater. */
static int32_t clamp64(int64_t v, int32_t lo, int32_t hi)
{
    if (v < lo) { return lo; }
    if (v > hi) { return hi; }
    return (int32_t)v;
}

static int      auto_on      = 0;
static uint32_t setpoint_dc  = 0U;
static int32_t  i_acc        = 0;                /* integral, milli-per-mille  */
static int32_t  prev_meas    = 0;

static uint32_t kp_m = 2000U;                    /* tuned offline: see README  */
static uint32_t ki_m = 200U;
static uint32_t kd_m = 2000U;

static int32_t  last_p = 0, last_i = 0, last_d = 0, last_out = 0;

static void stop_auto(void)
{
    auto_on     = 0;
    setpoint_dc = 0U;
    i_acc       = 0;
    last_p = 0;  last_i = 0;  last_d = 0;  last_out = 0;   /* nothing stale on screen */
}

void tc_init(void)
{
    stop_auto();
}

int tc_set_setpoint_c(uint32_t c)
{
    if (c == 0U) {                               /* back to MANUAL */
        stop_auto();
        (void)heater_set_permille(0U);
        return 1;
    }
    if ((c * 10U) > OVERTEMP_RESET_DC || safety_is_faulted()) {
        return 0;                                /* above safe limit, or faulted */
    }
    if (!auto_on) {                              /* bumpless start */
        i_acc     = 0;
        prev_meas = (int32_t)sensors_temp_dc();
    }
    setpoint_dc = c * 10U;
    auto_on     = 1;
    return 1;
}

void tc_poll(void)
{
    int32_t meas, e, p, d, di, trial, out;

    if (!auto_on) {
        return;
    }
    if (safety_is_faulted()) {                   /* fault: hand back to MANUAL */
        stop_auto();
        uart2_puts("\nTemperature control stopped by fault (heater locked off).\n> ");
        return;
    }

    meas = (int32_t)sensors_temp_dc();
    e    = (int32_t)setpoint_dc - meas;

    p  = clamp64((int64_t)kp_m * e, -10 * OUT_MAX, 10 * OUT_MAX);
    d  = clamp64(-((int64_t)kd_m * (meas - prev_meas) * 1000) / (int64_t)DT_MS,
                 -10 * OUT_MAX, 10 * OUT_MAX);
    di = clamp64(((int64_t)ki_m * e * (int64_t)DT_MS) / 1000, -OUT_MAX, OUT_MAX);
    prev_meas = meas;

    /* Anti-windup (conditional integration): only integrate if doing so
     * would not push an already-saturated output further into saturation */
    trial = clamp64((int64_t)p + i_acc + di + d, -10 * OUT_MAX, 10 * OUT_MAX);
    if (!((trial >= OUT_MAX && e > 0) || (trial <= 0 && e < 0))) {
        i_acc += di;
    }
    if (i_acc < 0)       { i_acc = 0; }
    if (i_acc > OUT_MAX) { i_acc = OUT_MAX; }

    out = clamp64((int64_t)p + i_acc + d, 0, OUT_MAX);

    last_p = p / 1000;  last_i = i_acc / 1000;  last_d = d / 1000;  last_out = out / 1000;
    (void)heater_set_permille((uint32_t)(out / 1000));
}

int      tc_is_auto(void)      { return auto_on; }
uint32_t tc_setpoint_dc(void)  { return auto_on ? setpoint_dc : 0U; }

void tc_set_gains(uint32_t kp, uint32_t ki, uint32_t kd)
{
    kp_m = kp;  ki_m = ki;  kd_m = kd;
    i_acc = 0;                                   /* restart integral with new gains */
}

void tc_get_gains(uint32_t *kp, uint32_t *ki, uint32_t *kd)
{
    *kp = kp_m;  *ki = ki_m;  *kd = kd_m;
}

void tc_get_terms(int32_t *p, int32_t *i, int32_t *d, int32_t *out)
{
    *p = last_p;  *i = last_i;  *d = last_d;  *out = last_out;
}
