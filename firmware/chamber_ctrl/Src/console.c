/*
 * console.c
 * Reads characters from the UART, builds a line, and runs the command
 * when Enter is pressed.
 *
 * Step 11: runs as the lowest-priority FreeRTOS task (Console). New commands:
 *   tasks    - list the tasks, their stacks and the control-loop timing
 *   busy N   - keep the console busy for N ms, to show that the PID and
 *              the safety task keep running on time anyway
 *
 * Step 12: run / abort / proc control the automatic recipe (process.c).
 * Step 13: 'can' shows the CAN link; recipe commands hold the storage mutex
 *          because the CANlink task can now also read recipes.
 * While a recipe runs, manual commands that move the chamber (temp, heater,
 * valve, gas, pid gains) are refused: one master at a time.
 */
#include <string.h>
#include "console.h"
#include "uart.h"
#include "gpio.h"
#include "systick.h"
#include "stm32f407_regs.h"
#include "app_config.h"
#include "safety.h"
#include "actuators.h"
#include "sensors.h"
#include "adc.h"
#include "gas.h"
#include "recipe.h"
#include "datalog.h"
#include "temp_ctrl.h"
#include "rtos_app.h"
#include "process.h"
#include "canlink.h"

#define LINE_MAX   64U

static char     line[LINE_MAX];
static uint32_t line_len = 0;
static int      last_was_cr = 0;

/* Forward declaration: parse_uint() is defined further down, but used earlier */
static int parse_uint(const char *s, uint32_t max, uint32_t *out);

static void prompt(void)
{
    uart2_puts("> ");
}

/* ---------------- Commands ---------------- */

static void cmd_help(void)
{
    uart2_puts("Commands:\n");
    uart2_puts("  help      show this list\n");
    uart2_puts("  status    uptime, safety inputs, fault and LEDs\n");
    uart2_puts("  clear     reset a fault (only if e-stop and door are safe)\n");
    uart2_puts("  sensors   raw ADC counts and converted readings\n");
    uart2_puts("  temp N    AUTO: hold chamber at N C with PID (0 = off, MANUAL)\n");
    uart2_puts("  pid       show PID setpoint, terms and gains\n");
    uart2_puts("  pid P I D set gains in thousandths, e.g. pid 2000 200 2000\n");
    uart2_puts("  heater N  MANUAL: set heater power, N = 0-100 %\n");
    uart2_puts("  valve N   set throttle-valve opening, N = 0-100 %\n");
    uart2_puts("  gas1 N    SiH4 flow, N = 0-100 sccm\n");
    uart2_puts("  gas2 N    N2O flow,  N = 0-1000 sccm\n");
    uart2_puts("  recipe                          list stored recipes\n");
    uart2_puts("  recipe show N                   show recipe N (1-4)\n");
    uart2_puts("  recipe save N NAME T P G1 G2 S  T=C, P=mTorr, G1/G2=sccm, S=seconds\n");
    uart2_puts("  recipe erase N                  delete recipe N\n");
    uart2_puts("  log                             data-log summary and last records\n");
    uart2_puts("  log erase                       erase the data log\n");
    uart2_puts("  log csv                         print the whole log as CSV (for Excel)\n");
    uart2_puts("  run N     run recipe N automatically (pump, leak check, heat, gas, deposit...)\n");
    uart2_puts("  abort     stop the running recipe and make the chamber safe\n");
    uart2_puts("  proc      show the recipe step, timers and pressure control\n");
    uart2_puts("  can       CAN bus statistics and fab-host link\n");
    uart2_puts("  tasks     FreeRTOS tasks, stack use, control-loop timing\n");
    uart2_puts("  busy N    keep the console busy for N ms (RTOS demo)\n");
    uart2_puts("  version   firmware version\n");
    uart2_puts("  led on    blue LED on\n");
    uart2_puts("  led off   blue LED off\n");
    uart2_puts("  reset     restart the controller\n");
}

static void print_led(const char *name, uint32_t pin)
{
    uart2_puts(name);
    uart2_puts(led_is_on(pin) ? "ON\n" : "OFF\n");
}

/* Print a per-mille value as a percentage with one decimal, e.g. 405 -> "40.5 %" */
static void print_permille(uint32_t pm)
{
    uart2_put_uint(pm / 10U);
    uart2_putc('.');
    uart2_put_uint(pm % 10U);
    uart2_puts(" %\n");
}

/* 4005 -> "400.5" */
static void print_tenths(uint32_t v)
{
    uart2_put_uint(v / 10U);
    uart2_putc('.');
    uart2_put_uint(v % 10U);
}

/* 2050 -> "2.050" (always three digits after the point) */
static void print_thousandths(uint32_t v)
{
    uint32_t frac = v % 1000U;
    uart2_put_uint(v / 1000U);
    uart2_putc('.');
    uart2_putc((char)('0' + (frac / 100U)));
    uart2_putc((char)('0' + ((frac / 10U) % 10U)));
    uart2_putc((char)('0' + (frac % 10U)));
}

/* "Gas SiH4   : set 50 / meas 50 sccm" */
static void print_gas(uint32_t mfc)
{
    uart2_puts("Gas ");
    uart2_puts(gas_name(mfc));
    uart2_puts(mfc == GAS_MFC1 ? "   : set " : "    : set ");
    uart2_put_uint(gas_setpoint_sccm(mfc));
    uart2_puts(" / meas ");
    uart2_put_uint(gas_measured_sccm(mfc));
    uart2_puts(" sccm\n");
}

static void cmd_gas(uint32_t mfc, const char *arg)
{
    uint32_t sccm;
    uint32_t fs = gas_full_scale_sccm(mfc);

    if (!parse_uint(arg, fs, &sccm)) {
        uart2_puts("Usage: gas");
        uart2_putc((char)('0' + mfc));
        uart2_puts(" N   (N = 0-");
        uart2_put_uint(fs);
        uart2_puts(" sccm)\n");
    } else if (gas_set_sccm(mfc, sccm)) {
        uart2_puts(gas_name(mfc));
        uart2_puts(" set to ");
        uart2_put_uint(sccm);
        uart2_puts(" sccm\n");
    } else {
        uart2_puts("Refused: fault active. Gas locked off until 'clear'.\n");
    }
}

/* ---------------- Word splitting ---------------- */
#define MAX_ARGS  9U

/* Split "recipe save 1 OXIDE" into argv[0]="recipe", argv[1]="save", ...
 * by replacing spaces with '\0'. Works on a copy, never on the line itself. */
static uint32_t split_args(char *s, char *argv[], uint32_t max)
{
    uint32_t argc = 0U;
    while (*s != '\0' && argc < max) {
        while (*s == ' ') { s++; }              /* skip spaces       */
        if (*s == '\0') { break; }
        argv[argc++] = s;                        /* start of a word   */
        while (*s != ' ' && *s != '\0') { s++; }
        if (*s == ' ') { *s++ = '\0'; }         /* end the word here */
    }
    return argc;
}

static void print_hex(uint32_t v, uint32_t digits)
{
    const char *hex = "0123456789ABCDEF";
    uart2_puts("0x");
    while (digits > 0U) {
        digits--;
        uart2_putc(hex[(v >> (digits * 4U)) & 0xFU]);
    }
}

/* ---------------- Recipes (I2C EEPROM) ---------------- */
static const char *recipe_status_text(int rc)
{
    switch (rc) {
    case RECIPE_OK:          return "OK";
    case RECIPE_EMPTY:       return "(empty)";
    case RECIPE_CORRUPT:     return "CORRUPT (CRC mismatch)";
    case RECIPE_VERIFY_FAIL: return "write verify failed";
    default:                 return "EEPROM not responding";
    }
}

static void print_recipe(uint32_t slot, const recipe_t *r)
{
    uart2_puts("Recipe ");
    uart2_put_uint(slot + 1U);
    uart2_puts(": ");
    uart2_puts(r->name);
    uart2_puts("\n  Temperature : "); uart2_put_uint(r->temp_c);         uart2_puts(" C");
    uart2_puts("\n  Pressure    : "); print_thousandths(r->pressure_mtorr); uart2_puts(" Torr");
    uart2_puts("\n  SiH4        : "); uart2_put_uint(r->gas1_sccm);      uart2_puts(" sccm");
    uart2_puts("\n  N2O         : "); uart2_put_uint(r->gas2_sccm);      uart2_puts(" sccm");
    uart2_puts("\n  Time        : "); uart2_put_uint(r->time_s);         uart2_puts(" s");
    uart2_puts("\n  CRC         : "); print_hex(r->crc, 4U);             uart2_puts("\n");
}

static void recipe_list(void)
{
    uint32_t i;
    recipe_t r;
    for (i = 0U; i < RECIPE_SLOTS; i++) {
        int rc = recipe_load(i, &r);
        uart2_puts("  ");
        uart2_put_uint(i + 1U);
        uart2_puts(": ");
        uart2_puts(rc == RECIPE_OK ? r.name : recipe_status_text(rc));
        uart2_puts("\n");
    }
}

static int parse_slot(const char *s, uint32_t *slot)
{
    uint32_t n;
    if (!parse_uint(s, RECIPE_SLOTS, &n) || n == 0U) {
        uart2_puts("Slot must be 1-4.\n");
        return 0;
    }
    *slot = n - 1U;                             /* humans count from 1, arrays from 0 */
    return 1;
}

static void recipe_cmd_save(char *argv[])
{
    recipe_t r;
    uint32_t slot, t, p, g1, g2, s, len;
    int rc;

    if (!parse_slot(argv[2], &slot)) { return; }
    len = (uint32_t)strlen(argv[3]);
    if (len == 0U || len >= RECIPE_NAME_LEN) {
        uart2_puts("Name must be 1-11 characters.\n");
        return;
    }
    /* Process temperature must stay below the over-temperature reset limit */
    if (!parse_uint(argv[4], OVERTEMP_RESET_DC / 10U, &t) || t == 0U ||
        !parse_uint(argv[5], 10000U, &p) ||
        !parse_uint(argv[6], gas_full_scale_sccm(GAS_MFC1), &g1) ||
        !parse_uint(argv[7], gas_full_scale_sccm(GAS_MFC2), &g2) ||
        !parse_uint(argv[8], 3600U, &s) || s == 0U) {
        uart2_puts("Ranges: T 1-480 C, P 0-10000 mTorr, G1 0-100, G2 0-1000 sccm, S 1-3600 s\n");
        return;
    }
    memset(&r, 0, sizeof(r));
    memcpy(r.name, argv[3], len + 1U);          /* includes the '\0' */
    r.temp_c         = (uint16_t)t;
    r.pressure_mtorr = (uint16_t)p;
    r.gas1_sccm      = (uint16_t)g1;
    r.gas2_sccm      = (uint16_t)g2;
    r.time_s         = (uint16_t)s;

    rc = recipe_save(slot, &r);
    if (rc == RECIPE_OK) {
        uart2_puts("Saved and verified.\n");
        print_recipe(slot, &r);
    } else {
        uart2_puts("Save failed: ");
        uart2_puts(recipe_status_text(rc));
        uart2_puts("\n");
    }
}

static void cmd_recipe(const char *cmd)
{
    char     buf[LINE_MAX];
    char    *argv[MAX_ARGS];
    uint32_t argc, slot;
    recipe_t r;
    int      rc;

    memcpy(buf, cmd, strlen(cmd) + 1U);
    argc = split_args(buf, argv, MAX_ARGS);

    if (argc == 1U) {
        recipe_list();
    } else if (argc == 3U && strcmp(argv[1], "show") == 0) {
        if (!parse_slot(argv[2], &slot)) { return; }
        rc = recipe_load(slot, &r);
        if (rc == RECIPE_OK) {
            print_recipe(slot, &r);
        } else {
            uart2_puts("Recipe ");
            uart2_put_uint(slot + 1U);
            uart2_puts(": ");
            uart2_puts(recipe_status_text(rc));
            uart2_puts("\n");
        }
    } else if (argc == 3U && strcmp(argv[1], "erase") == 0) {
        if (!parse_slot(argv[2], &slot)) { return; }
        rc = recipe_erase(slot);
        uart2_puts(rc == RECIPE_OK ? "Erased.\n" : "Erase failed.\n");
    } else if (argc == 9U && strcmp(argv[1], "save") == 0) {
        recipe_cmd_save(argv);
    } else {
        uart2_puts("Usage: recipe | recipe show N | recipe erase N |\n"
                   "       recipe save N NAME T P G1 G2 S   (e.g. recipe save 1 OXIDE 400 2000 50 400 120)\n");
    }
}

/* ---------------- Data log (SPI flash) ---------------- */
static void print_log_record(const log_record_t *r)
{
    uart2_puts("  t=");      uart2_put_uint(r->uptime_s);
    uart2_puts("s  T=");     print_tenths(r->temp_dc);
    uart2_puts("C  P=");     print_thousandths(r->pressure_mtorr);
    uart2_puts("  H=");      print_tenths(r->heater_permille);
    uart2_puts("%  SiH4=");  uart2_put_uint(r->gas1_sccm);
    uart2_puts("  N2O=");    uart2_put_uint(r->gas2_sccm);
    uart2_puts("  faults="); print_hex(r->faults, 2U);
    uart2_puts("\n");
}

static void cmd_log_locked(const char *cmd);

/* The flash is shared with the Logger task: hold the flash mutex for the
 * whole command so the two never talk to the chip at the same time. */
static void cmd_log(const char *cmd)
{
    flash_lock();
    cmd_log_locked(cmd);
    flash_unlock();
}

static void cmd_log_locked(const char *cmd)
{
    uint32_t n, first, i;
    log_record_t r;

    if (strcmp(cmd, "log erase") == 0) {
        if (datalog_ok()) {
            datalog_erase_all();
            uart2_puts("Erasing data log in the background...\n");
        } else {
            uart2_puts("No flash.\n");
        }
        return;
    }
    if (strcmp(cmd, "log csv") == 0) {
        /* Comma-separated values: paste into Excel or any plotting tool */
        n = datalog_count();
        uart2_puts("time_s,temp_C,pressure_Torr,heater_pct,sih4_sccm,n2o_sccm,faults\n");
        for (i = 0U; i < n; i++) {
            if (datalog_get(i, &r)) {
                uart2_put_uint(r.uptime_s);        uart2_putc(',');
                print_tenths(r.temp_dc);           uart2_putc(',');
                print_thousandths(r.pressure_mtorr); uart2_putc(',');
                print_tenths(r.heater_permille);   uart2_putc(',');
                uart2_put_uint(r.gas1_sccm);       uart2_putc(',');
                uart2_put_uint(r.gas2_sccm);       uart2_putc(',');
                uart2_put_uint(r.faults);          uart2_puts("\n");
            }
        }
        return;
    }
    if (strcmp(cmd, "log") != 0) {
        uart2_puts("Usage: log | log erase | log csv\n");
        return;
    }
    uart2_puts("Flash JEDEC ID : ");
    print_hex(datalog_flash_id(), 6U);
    uart2_puts(datalog_ok() ? " (Micron)\n" : " (not found)\n");
    if (datalog_busy_erasing()) {
        uart2_puts("Erase in progress...\n");
        return;
    }
    n = datalog_count();
    uart2_puts("Records        : ");
    uart2_put_uint(n);
    uart2_puts(" / ");
    uart2_put_uint(datalog_capacity());
    uart2_puts("\n");
    first = (n > 5U) ? (n - 5U) : 0U;           /* show the newest 5 */
    for (i = first; i < n; i++) {
        if (datalog_get(i, &r)) {
            print_log_record(&r);
        }
    }
}

/* ---------------- Temperature control (PID) ---------------- */
static void print_int(int32_t v)               /* signed number */
{
    if (v < 0) {
        uart2_putc('-');
        v = -v;
    }
    uart2_put_uint((uint32_t)v);
}

static void print_signed_tenths(int32_t v)     /* e.g. -125 -> "-12.5" */
{
    if (v < 0) {
        uart2_putc('-');
        v = -v;
    }
    print_tenths((uint32_t)v);
}

static void cmd_temp(const char *arg)
{
    uint32_t c;
    if (!parse_uint(arg, 800U, &c)) {
        uart2_puts("Usage: temp N   (N = 1-480 C, or 0 for MANUAL)\n");
    } else if (tc_set_setpoint_c(c)) {
        if (c == 0U) {
            uart2_puts("MANUAL mode. Heater off.\n");
        } else {
            uart2_puts("AUTO: holding ");
            uart2_put_uint(c);
            uart2_puts(" C\n");
        }
    } else if (safety_is_faulted()) {
        uart2_puts("Refused: fault active. Clear it first.\n");
    } else {
        uart2_puts("Refused: maximum is 480 C (over-temperature reset limit).\n");
    }
}

static void cmd_pid(const char *cmd)
{
    char     buf[LINE_MAX];
    char    *argv[MAX_ARGS];
    uint32_t argc, kp, ki, kd;
    int32_t  p, i, d, out;

    memcpy(buf, cmd, strlen(cmd) + 1U);
    argc = split_args(buf, argv, MAX_ARGS);

    if (argc == 4U) {
        if (!parse_uint(argv[1], TC_GAIN_MAX, &kp) ||
            !parse_uint(argv[2], TC_GAIN_MAX, &ki) ||
            !parse_uint(argv[3], TC_GAIN_MAX, &kd)) {
            uart2_puts("Each gain must be 0-50000 (thousandths).\n");
            return;
        }
        tc_set_gains(kp, ki, kd);
        uart2_puts("Gains updated.\n");
    } else if (argc != 1U) {
        uart2_puts("Usage: pid | pid P I D\n");
        return;
    }

    tc_get_gains(&kp, &ki, &kd);
    tc_get_terms(&p, &i, &d, &out);
    uart2_puts("Mode       : ");
    uart2_puts(tc_is_auto() ? "AUTO\n" : "MANUAL\n");
    uart2_puts("Setpoint   : "); print_tenths(tc_setpoint_dc());   uart2_puts(" C\n");
    uart2_puts("Measured   : "); print_tenths(sensors_temp_dc());  uart2_puts(" C\n");
    uart2_puts("Error      : ");
    print_signed_tenths(tc_is_auto() ? ((int32_t)tc_setpoint_dc() - (int32_t)sensors_temp_dc()) : 0);
    uart2_puts(" C\n");
    uart2_puts("P term     : "); print_signed_tenths(p);   uart2_puts(" %\n");
    uart2_puts("I term     : "); print_signed_tenths(i);   uart2_puts(" %\n");
    uart2_puts("D term     : "); print_signed_tenths(d);   uart2_puts(" %\n");
    uart2_puts("Heater out : "); print_signed_tenths(out); uart2_puts(" %\n");
    uart2_puts("Gains      : Kp ");
    print_int((int32_t)kp); uart2_puts("  Ki "); print_int((int32_t)ki);
    uart2_puts("  Kd "); print_int((int32_t)kd); uart2_puts("  (thousandths)\n");
}

static void cmd_sensors(void)
{
    uart2_puts("Temp     raw ");
    uart2_put_uint(adc_get_raw(ADC_IDX_TEMP));
    uart2_puts(" / 4095 = ");
    print_tenths(sensors_temp_dc());
    uart2_puts(" C\n");
    uart2_puts("Pressure raw ");
    uart2_put_uint(adc_get_raw(ADC_IDX_PRESSURE));
    uart2_puts(" / 4095 = ");
    print_thousandths(sensors_pressure_mtorr());
    uart2_puts(" Torr\n");
    uart2_puts("Flow 1   raw ");
    uart2_put_uint(adc_get_raw(ADC_IDX_FLOW1));
    uart2_puts(" / 4095 = ");
    uart2_put_uint(gas_measured_sccm(GAS_MFC1));
    uart2_puts(" sccm SiH4\n");
    uart2_puts("Flow 2   raw ");
    uart2_put_uint(adc_get_raw(ADC_IDX_FLOW2));
    uart2_puts(" / 4095 = ");
    uart2_put_uint(gas_measured_sccm(GAS_MFC2));
    uart2_puts(" sccm N2O\n");
}

/* Read a whole number 0..max from text. Returns 1 if valid, 0 if not. */
static int parse_uint(const char *s, uint32_t max, uint32_t *out)
{
    uint32_t value  = 0U;
    int      digits = 0;

    while (*s == ' ') {               /* skip spaces */
        s++;
    }
    while (*s >= '0' && *s <= '9') {
        value = (value * 10U) + (uint32_t)(*s - '0');
        if (value > max) {
            return 0;                 /* out of range */
        }
        s++;
        digits++;
    }
    if (digits == 0 || *s != '\0') {
        return 0;                     /* no number, or extra characters after it */
    }
    *out = value;
    return 1;
}

static int parse_percent(const char *s, uint32_t *out)
{
    return parse_uint(s, 100U, out);
}

static void cmd_heater(const char *arg)
{
    uint32_t pct;
    if (tc_is_auto()) {
        uart2_puts("Refused: PID is controlling the heater. Type 'temp 0' for MANUAL.\n");
        return;
    }
    if (!parse_percent(arg, &pct)) {
        uart2_puts("Usage: heater N   (N = 0-100)\n");
    } else if (heater_set_percent(pct)) {
        uart2_puts("Heater set to ");
        uart2_put_uint(pct);
        uart2_puts(" %\n");
    } else {
        uart2_puts("Refused: fault active. Heater locked off until 'clear'.\n");
    }
}

static void cmd_valve(const char *arg)
{
    uint32_t pct;
    if (!parse_percent(arg, &pct)) {
        uart2_puts("Usage: valve N   (N = 0-100)\n");
    } else if (valve_set_percent(pct)) {
        uart2_puts("Valve set to ");
        uart2_put_uint(pct);
        uart2_puts(" %\n");
    } else {
        uart2_puts("Refused: fault active. Valve stays fully open until 'clear'.\n");
    }
}

/* ---------------- Automatic recipe (Step 12) ---------------- */
static void cmd_run(const char *arg)
{
    uint32_t slot;
    recipe_t r;
    int rc;

    if (!parse_uint(arg, RECIPE_SLOTS, &slot) || slot == 0U) {
        uart2_puts("Usage: run N   (N = 1-4)\n");
        return;
    }
    if (process_is_running()) {
        uart2_puts("Refused: a recipe is already running. Type 'abort' first.\n");
        return;
    }
    if (safety_is_faulted()) {
        uart2_puts("Refused: fault active. Clear it first.\n");
        return;
    }
    /* Load and check the recipe HERE, in the console task: the EEPROM is only
     * ever used by this task, so it needs no mutex. The Process task gets a
     * verified copy through the queue. */
    storage_lock();
    rc = recipe_load(slot - 1U, &r);
    storage_unlock();
    if (rc != RECIPE_OK) {
        uart2_puts("Cannot run: recipe ");
        uart2_put_uint(slot);
        uart2_puts(" is ");
        uart2_puts(recipe_status_text(rc));
        uart2_puts("\n");
        return;
    }
    if (!process_request_run(&r)) {
        uart2_puts("Busy, try again.\n");
    }
}

static void cmd_abort(void)
{
    if (!process_is_running()) {
        uart2_puts("No recipe running.\n");
    } else if (!process_request_abort()) {
        uart2_puts("Busy, try again.\n");
    }
}

static void cmd_proc(void)
{
    proc_status_t st;
    process_get_status(&st);

    uart2_puts("Step       : ");
    uart2_puts(process_state_name(st.state));
    uart2_puts("\n");
    if (st.state == PS_IDLE) {
        uart2_puts("No recipe has been run. Type 'run N'.\n");
        return;
    }
    uart2_puts("Recipe     : ");      uart2_puts(st.recipe_name);
    uart2_puts("\nRun time   : ");    uart2_put_uint(st.run_s);
    uart2_puts(" s\nIn step    : ");  uart2_put_uint(st.state_s);
    uart2_puts(" s\n");
    if (st.state == PS_DEPOSIT) {
        uart2_puts("Remaining  : ");  uart2_put_uint(st.remaining_s);  uart2_puts(" s\n");
    }
    uart2_puts("Temperature: ");  print_tenths(sensors_temp_dc());          uart2_puts(" C\n");
    uart2_puts("Pressure   : ");  print_thousandths(sensors_pressure_mtorr());
    if (st.pressure_sp != 0U) {
        uart2_puts(" Torr (setpoint ");  print_thousandths(st.pressure_sp);  uart2_puts(")\n");
    } else {
        uart2_puts(" Torr (no pressure control)\n");
    }
    uart2_puts("Valve      : ");  print_permille(valve_get_permille());
    print_gas(GAS_MFC1);
    print_gas(GAS_MFC2);
    if (st.reason[0] != '\0') {
        uart2_puts("Aborted    : ");  uart2_puts(st.reason);  uart2_puts("\n");
    }
}

/* Manual commands that move the chamber are refused during a recipe */
static int manual_locked(void)
{
    if (process_is_running()) {
        uart2_puts("Refused: recipe running. Type 'abort' to take manual control.\n");
        return 1;
    }
    return 0;
}

static void cmd_status(void)
{
    uint8_t src = safety_fault_source();

    uart2_puts("Uptime     : ");
    uart2_put_uint(millis());
    uart2_puts(" ms\n");

    uart2_puts("E-stop     : ");
    uart2_puts(safety_estop_pressed() ? "PRESSED\n" : "released\n");
    uart2_puts("Door       : ");
    uart2_puts(safety_door_open() ? "OPEN\n" : "closed\n");

    uart2_puts("State      : ");
    if (safety_is_faulted()) {
        uart2_puts("FAULT (");
        if (src & FAULT_ESTOP) { uart2_puts(" e-stop"); }
        if (src & FAULT_DOOR)  { uart2_puts(" door"); }
        if (src & FAULT_OVERTEMP) { uart2_puts(" over-temp"); }
        if (src & FAULT_GASFLOW)  { uart2_puts(" gas-flow"); }
        uart2_puts(" )\n");
    } else {
        uart2_puts("OK\n");
    }
    uart2_puts("Trips      : ");
    uart2_put_uint(safety_trip_count());
    uart2_puts("\n");

    uart2_puts("Temperature: ");
    print_tenths(sensors_temp_dc());
    uart2_puts(" C\n");
    uart2_puts("Control    : ");
    if (tc_is_auto()) {
        uart2_puts("AUTO (PID), setpoint ");
        print_tenths(tc_setpoint_dc());
        uart2_puts(" C\n");
    } else {
        uart2_puts("MANUAL\n");
    }
    uart2_puts("Pressure   : ");
    print_thousandths(sensors_pressure_mtorr());
    uart2_puts(" Torr\n");

    print_gas(GAS_MFC1);
    print_gas(GAS_MFC2);

    uart2_puts("Heater     : ");
    print_permille(heater_get_permille());
    uart2_puts("Valve      : ");
    print_permille(valve_get_permille());

    print_led("Green  LED : ", LED_GREEN);
    print_led("Orange LED : ", LED_ORANGE);
    print_led("Red    LED : ", LED_RED);
    print_led("Blue   LED : ", LED_BLUE);
}

static void cmd_clear(void)
{
    if (!safety_is_faulted()) {
        uart2_puts("No fault to clear.\n");
    } else if (safety_clear()) {
        uart2_puts("Fault cleared. Chamber ready.\n");
    } else {
        uart2_puts("Cannot clear: e-stop, door, temperature or gas flow still unsafe.\n");
    }
}

/* Simulates a slow command (a busy-wait, NOT vTaskDelay), so the CPU is
 * really kept busy. Higher-priority tasks still interrupt it on time. */
static void cmd_busy(const char *arg)
{
    uint32_t ms;
    if (!parse_uint(arg, 5000U, &ms)) {
        uart2_puts("Usage: busy N   (N = 0-5000 ms)\n");
        return;
    }
    uart2_puts("Console busy for ");
    uart2_put_uint(ms);
    uart2_puts(" ms...\n");
    delay_ms(ms);
    uart2_puts("Done. Type 'tasks' to see that the Control period stayed 100 ms.\n");
}

static void cmd_version(void)
{
    uart2_puts(FW_NAME " v" FW_VERSION "\n");
}

static void cmd_reset(void)
{
    uart2_puts("Resetting...\n");
    delay_ms(10);                       /* let the message finish sending */
    SCB_AIRCR = SCB_AIRCR_RESET;        /* ask the CPU for a system reset */
    while (1) {
        /* wait for the reset to happen */
    }
}

static void run_command(const char *cmd)
{
    if (strcmp(cmd, "help") == 0) {
        cmd_help();
    } else if (strcmp(cmd, "status") == 0) {
        cmd_status();
    } else if (strcmp(cmd, "clear") == 0) {
        cmd_clear();
    } else if (strncmp(cmd, "temp", 4) == 0 && (cmd[4] == '\0' || cmd[4] == ' ')) {
        if (!manual_locked()) { cmd_temp(cmd + 4); }
    } else if (strcmp(cmd, "pid") == 0) {
        cmd_pid(cmd);                  /* just showing the PID is always allowed */
    } else if (strncmp(cmd, "pid ", 4) == 0) {
        if (!manual_locked()) { cmd_pid(cmd); }
    } else if (strncmp(cmd, "run", 3) == 0 && (cmd[3] == '\0' || cmd[3] == ' ')) {
        cmd_run(cmd + 3);
    } else if (strcmp(cmd, "abort") == 0) {
        cmd_abort();
    } else if (strcmp(cmd, "proc") == 0) {
        cmd_proc();
    } else if (strcmp(cmd, "can") == 0) {
        canlink_print();
    } else if (strncmp(cmd, "recipe", 6) == 0 && (cmd[6] == '\0' || cmd[6] == ' ')) {
        storage_lock();                /* EEPROM shared with the CANlink task */
        cmd_recipe(cmd);
        storage_unlock();
    } else if (strncmp(cmd, "log", 3) == 0 && (cmd[3] == '\0' || cmd[3] == ' ')) {
        cmd_log(cmd);
    } else if (strcmp(cmd, "sensors") == 0) {
        cmd_sensors();
    } else if (strcmp(cmd, "heater") == 0) {
        uart2_puts("Usage: heater N   (N = 0-100)\n");
    } else if (strcmp(cmd, "valve") == 0) {
        uart2_puts("Usage: valve N   (N = 0-100)\n");
    } else if (strncmp(cmd, "heater ", 7) == 0) {
        if (!manual_locked()) { cmd_heater(cmd + 7); }   /* the text after "heater " */
    } else if (strncmp(cmd, "valve ", 6) == 0) {
        if (!manual_locked()) { cmd_valve(cmd + 6); }
    } else if (strncmp(cmd, "gas1", 4) == 0) {
        if (!manual_locked()) { cmd_gas(GAS_MFC1, cmd + 4); }   /* parse_uint skips the space */
    } else if (strncmp(cmd, "gas2", 4) == 0) {
        if (!manual_locked()) { cmd_gas(GAS_MFC2, cmd + 4); }
    } else if (strcmp(cmd, "tasks") == 0) {
        rtos_print_tasks();
    } else if (strncmp(cmd, "busy", 4) == 0 && (cmd[4] == '\0' || cmd[4] == ' ')) {
        cmd_busy(cmd + 4);
    } else if (strcmp(cmd, "version") == 0) {
        cmd_version();
    } else if (strcmp(cmd, "led on") == 0) {
        led_on(LED_BLUE);
        uart2_puts("Blue LED on\n");
    } else if (strcmp(cmd, "led off") == 0) {
        led_off(LED_BLUE);
        uart2_puts("Blue LED off\n");
    } else if (strcmp(cmd, "reset") == 0) {
        cmd_reset();
    } else {
        uart2_puts("Unknown command: ");
        uart2_puts(cmd);
        uart2_puts("  (type 'help')\n");
    }
}

/* ---------------- Line editing ---------------- */

void console_init(void)
{
    uart2_puts("Type 'help' for commands.\n");
    prompt();
}

void console_poll(void)
{
    int ch;

    /* Handle every byte that has arrived since the last call */
    while ((ch = uart2_getc()) >= 0) {
        char c = (char)ch;

        if (c == '\r' || c == '\n') {
            if (c == '\n' && last_was_cr) {      /* "\r\n" counts as one Enter */
                last_was_cr = 0;
                continue;
            }
            last_was_cr = (c == '\r');

            uart2_puts("\n");
            line[line_len] = '\0';               /* end the string */
            if (line_len > 0U) {
                run_command(line);
            }
            line_len = 0;
            prompt();
        } else if (c == '\b' || c == 0x7F) {     /* Backspace or Delete */
            last_was_cr = 0;
            if (line_len > 0U) {
                line_len--;
                uart2_puts("\b \b");             /* erase the character on screen */
            }
        } else if (c >= ' ' && c <= '~') {       /* printable character */
            last_was_cr = 0;
            if (line_len < (LINE_MAX - 1U)) {
                line[line_len++] = c;
                uart2_putc(c);                   /* echo it so the user sees it */
            }
        }
        /* any other control character is ignored */
    }
}
