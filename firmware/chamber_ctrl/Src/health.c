/*
 * health.c
 * Watchdog, task supervision, HardFault capture and reset-cause reporting.
 * See health.h for the overview.
 */
#include <string.h>
#include "health.h"
#include "stm32f407_regs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "uart.h"
#include "gpio.h"
#include "actuators.h"
#include "app_config.h"

/* ---------------- Settings ---------------- */
#define WDG_TIMEOUT_MS      1000U   /* reset if not refreshed for 1 s                */
#define SUPERVISE_MS        250U    /* the supervisor checks the tasks this often     */
#define MAX_STRIKES         2U      /* missing check-ins in this many checks in a row */

/* ---------------- Backup register layout ---------------- */
#define BKP_MAGIC           0xB007U          /* "boot": marks our own data          */
#define BKP_REASON          0U               /* (magic << 16) | reason              */
#define BKP_DETAIL          1U               /* PC, missing-task mask or line no.   */
#define BKP_LR              2U
#define BKP_CFSR            3U
#define BKP_HFSR            4U
#define BKP_TAG             5U               /* 4 characters: task or file name     */
#define BKP_BOOTS           6U
#define BKP_BFAR            7U

static const char *const client_name[HC_COUNT] = {
    "Safety", "Control", "Process", "CANlink", "Logger"
};

/* Copy of the previous run's record, taken at boot, for the 'diag' command */
static uint32_t prev[8];
static uint32_t boot_count = 0U;
static uint32_t csr_at_boot = 0U;

/* Check-in flags: one byte each, so a task's write can never disturb another's */
static volatile uint8_t  alive[HC_COUNT];
static volatile uint32_t last_seen[HC_COUNT];
static volatile int      hang_request = 0;
static int               wdg_running  = 0;

/* ---------------- Small output helpers (no mutex: may run in a fault) ---------------- */
static void raw_hex(uint32_t v)
{
    char buf[11];
    int  i;
    buf[0] = '0';
    buf[1] = 'x';
    for (i = 0; i < 8; i++) {
        uint32_t n = (v >> (28 - 4 * i)) & 0xFU;
        buf[2 + i] = (char)((n < 10U) ? ('0' + n) : ('A' + n - 10U));
    }
    buf[10] = '\0';
    uart2_panic_puts(buf);
}

static void raw_uint(uint32_t v)
{
    char buf[11];
    int  i = 10;
    buf[i] = '\0';
    do {
        buf[--i] = (char)('0' + v % 10U);
        v /= 10U;
    } while (v > 0U && i > 0);
    uart2_panic_puts(&buf[i]);
}

static void put_tag(uint32_t tag)                 /* up to 4 packed characters */
{
    char s[5];
    int  i;
    for (i = 0; i < 4; i++) {
        s[i] = (char)((tag >> (8 * i)) & 0xFFU);
        if (s[i] < ' ' || s[i] > '~') { s[i] = '\0'; break; }
    }
    s[4] = '\0';
    uart2_panic_puts(s);
}

static uint32_t pack_tag(const char *t)
{
    uint32_t v = 0U;
    int      i;
    if (t == NULL) { return 0U; }
    for (i = 0; i < 4 && t[i] != '\0'; i++) {
        v |= (uint32_t)(uint8_t)t[i] << (8 * i);
    }
    return v;
}

/* Plain-language meaning of the most important fault bits */
static const char *fault_text(uint32_t cfsr)
{
    if (cfsr & (1UL << 16)) { return "undefined instruction"; }
    if (cfsr & (1UL << 17)) { return "invalid state (jump to non-Thumb address)"; }
    if (cfsr & (1UL << 18)) { return "invalid return address"; }
    if (cfsr & (1UL << 25)) { return "divide by zero"; }
    if (cfsr & (1UL << 24)) { return "unaligned access"; }
    if (cfsr & (1UL << 9))  { return "bus error on data access"; }
    if (cfsr & (1UL << 8))  { return "bus error on instruction fetch"; }
    if (cfsr & (1UL << 1))  { return "memory protection: data access"; }
    if (cfsr & (1UL << 0))  { return "memory protection: instruction fetch"; }
    return "see CFSR/HFSR";
}

/* ---------------- Backup domain ---------------- */
static void backup_enable(void)
{
    RCC_APB1ENR |= RCC_APB1ENR_PWREN;    /* clock for the power controller   */
    PWR_CR      |= PWR_CR_DBP;           /* unlock writes to backup registers */
}

static void record(reset_reason_t why, uint32_t detail, uint32_t lr,
                   uint32_t cfsr, uint32_t hfsr, uint32_t tag, uint32_t bfar)
{
    RTC_BKP(BKP_DETAIL) = detail;
    RTC_BKP(BKP_LR)     = lr;
    RTC_BKP(BKP_CFSR)   = cfsr;
    RTC_BKP(BKP_HFSR)   = hfsr;
    RTC_BKP(BKP_TAG)    = tag;
    RTC_BKP(BKP_BFAR)   = bfar;
    RTC_BKP(BKP_REASON) = (BKP_MAGIC << 16) | (uint32_t)why;   /* written LAST: marks the record complete */
}

static void chip_reset(void)
{
    volatile uint32_t n;
    for (n = 0U; n < 20000U; n++) {      /* let the last characters leave the UART */
    }
    SCB_AIRCR = SCB_AIRCR_RESET;
    for (;;) {
    }
}

void health_reset(reset_reason_t why, uint32_t detail, const char *text)
{
    actuators_safe_state();              /* heater off, gases off, valve open - FIRST */
    led_on(LED_RED);
    record(why, detail, 0U, 0U, 0U, pack_tag(text), 0U);
    uart2_panic_puts("\n*** Restarting the controller...\n");
    chip_reset();
}

/* ---------------- Start-up report ---------------- */
static void print_reason(void)
{
    uint32_t r = prev[BKP_REASON];

    if ((r >> 16) != BKP_MAGIC) {
        uart2_panic_puts("power-on (no previous record)\n");
        return;
    }
    switch ((reset_reason_t)(r & 0xFFFFU)) {
    case RR_RUNNING:
        uart2_panic_puts("UNEXPECTED - no reason was recorded (watchdog, power dip or debugger)");
        if (csr_at_boot & RCC_CSR_IWDGRSTF) {
            uart2_panic_puts("; hardware flag: independent watchdog");
        }
        uart2_panic_puts("\n");
        break;
    case RR_OPERATOR:
        uart2_panic_puts("operator 'reset' command\n");
        break;
    case RR_SUPERVISOR: {
        uint32_t m = prev[BKP_DETAIL];
        uint32_t i;
        int first = 1;
        uart2_panic_puts("WATCHDOG - tasks stopped checking in: ");
        for (i = 0U; i < HC_COUNT; i++) {
            if (m & (1UL << i)) {
                uart2_panic_puts(client_name[i]);
                uart2_panic_puts(" ");
            }
        }
        for (i = 0U; i < HC_COUNT && first; i++) {      /* list is in priority order */
            if (m & (1UL << i)) {
                uart2_panic_puts("\n             likely cause: ");
                uart2_panic_puts(client_name[i]);
                uart2_panic_puts(" (highest priority missing; the others were starved by it)");
                first = 0;
            }
        }
        uart2_panic_puts("\n");
        break;
    }
    case RR_HARDFAULT:
        uart2_panic_puts("HARDFAULT - ");
        uart2_panic_puts(fault_text(prev[BKP_CFSR]));
        uart2_panic_puts("\n             in task ");
        put_tag(prev[BKP_TAG]);
        uart2_panic_puts(", PC=");    raw_hex(prev[BKP_DETAIL]);
        uart2_panic_puts(", LR=");    raw_hex(prev[BKP_LR]);
        uart2_panic_puts("\n             CFSR=");  raw_hex(prev[BKP_CFSR]);
        uart2_panic_puts(", HFSR=");  raw_hex(prev[BKP_HFSR]);
        uart2_panic_puts("\n             (find the PC in Debug/chamber_ctrl.list to see the function)\n");
        break;
    case RR_FATAL:
        uart2_panic_puts("FATAL SOFTWARE ERROR in ");
        put_tag(prev[BKP_TAG]);
        if (prev[BKP_DETAIL] != 0U) {
            uart2_panic_puts(" line ");
            raw_uint(prev[BKP_DETAIL]);
        }
        uart2_panic_puts("\n");
        break;
    default:
        uart2_panic_puts("unknown\n");
        break;
    }
}

void health_boot_report(void)
{
    uint32_t i;

    backup_enable();
    for (i = 0U; i < 8U; i++) {
        prev[i] = RTC_BKP(i);
    }
    csr_at_boot = RCC_CSR;
    RCC_CSR |= RCC_CSR_RMVF;                     /* clear the hardware flags for next time */

    boot_count = ((prev[BKP_REASON] >> 16) == BKP_MAGIC) ? (prev[BKP_BOOTS] + 1U) : 1U;
    RTC_BKP(BKP_BOOTS) = boot_count;

    uart2_panic_puts("Boot #");
    raw_uint(boot_count);
    uart2_panic_puts(". Last reset: ");
    print_reason();

    /* From now on, if we reset without recording a reason, the next boot
     * will know it was unexpected */
    record(RR_RUNNING, 0U, 0U, 0U, 0U, 0U, 0U);
}

/* ---------------- Watchdog ---------------- */
void health_watchdog_start(void)
{
    uint32_t n;

    DBGMCU_APB1_FZ |= DBGMCU_IWDG_STOP;   /* do not reset while paused in a debugger */

    IWDG_KR  = IWDG_KEY_START;           /* starts the LSI clock and the watchdog  */
    IWDG_KR  = IWDG_KEY_UNLOCK;          /* allow changing PR and RLR               */
    IWDG_PR  = 3U;                       /* 32 kHz / 32 = 1 kHz: one count per ms   */
    IWDG_RLR = WDG_TIMEOUT_MS - 1U;      /* 1000 counts = 1 s                       */
    for (n = 0U; (IWDG_SR != 0U) && (n < 100000U); n++) {
        /* wait until the new values are taken over by the watchdog's clock */
    }
    IWDG_KR  = IWDG_KEY_REFRESH;
    wdg_running = 1;
}

void health_checkin(health_client_t c)
{
    alive[c]     = 1U;
    last_seen[c] = (uint32_t)xTaskGetTickCount();
}

/* The supervisor: highest priority, so a stuck lower task cannot stop it.
 * It refreshes the watchdog only when every task has checked in. */
void health_task(void *arg)
{
    TickType_t wake    = xTaskGetTickCount();
    uint32_t   strikes = 0U;
    (void)arg;

    for (;;) {
        uint32_t missing = 0U;
        uint32_t i;

        xTaskDelayUntil(&wake, pdMS_TO_TICKS(SUPERVISE_MS));

        for (i = 0U; i < HC_COUNT; i++) {
            if (!alive[i]) {
                missing |= (1UL << i);
            }
            alive[i] = 0U;
        }

        if (missing == 0U) {
            strikes = 0U;
            IWDG_KR = IWDG_KEY_REFRESH;          /* "all is well" */
            continue;
        }
        if (++strikes < MAX_STRIKES) {
            IWDG_KR = IWDG_KEY_REFRESH;          /* allow one late check, not two */
            continue;
        }

        /* A task is stuck. Make the chamber safe NOW, record which tasks were
         * silent, and stop refreshing: the watchdog will reset the chip. */
        actuators_safe_state();
        led_on(LED_RED);
        record(RR_SUPERVISOR, missing, 0U, 0U, 0U, 0U, 0U);
        uart2_panic_puts("\n*** SUPERVISOR: tasks not responding:");
        for (i = 0U; i < HC_COUNT; i++) {
            if (missing & (1UL << i)) {
                uart2_panic_puts(" ");
                uart2_panic_puts(client_name[i]);
            }
        }
        uart2_panic_puts("\n*** Chamber in safe state. Watchdog will reset the controller.\n");
        for (;;) {
            vTaskDelay(pdMS_TO_TICKS(1000U));    /* no refresh any more */
        }
    }
}

/* ---------------- Low power: sleep when there is nothing to do ---------------- */
void vApplicationIdleHook(void)
{
    /* Wait For Interrupt: the CPU core stops until the next interrupt
     * (at the latest the 1 ms tick). Peripherals keep running. */
    __asm volatile ("wfi");
}

/* ---------------- HardFault ---------------- */
/* On a fault the CPU has already pushed r0-r3, r12, lr, pc, xpsr onto the
 * stack that was in use. Bit 2 of the special LR value (EXC_RETURN) says
 * which stack: 0 = main stack (an interrupt was running), 1 = process
 * stack (a task was running). The naked function hands that address to C. */
void hardfault_c(uint32_t *frame, uint32_t exc_return);

__attribute__((naked)) void HardFault_Handler(void)
{
    __asm volatile (
        "tst   lr, #4      \n"
        "ite   eq          \n"
        "mrseq r0, msp     \n"
        "mrsne r0, psp     \n"
        "mov   r1, lr      \n"
        "b     hardfault_c \n"
    );
}

void hardfault_c(uint32_t *frame, uint32_t exc_return)
{
    uint32_t    pc   = frame[6];
    uint32_t    lr   = frame[5];
    uint32_t    cfsr = SCB_CFSR;
    uint32_t    hfsr = SCB_HFSR;
    const char *who  = (exc_return & 4U) ? pcTaskGetName(NULL) : "ISR";

    actuators_safe_state();                  /* safety first, then diagnosis */
    led_on(LED_RED);
    record(RR_HARDFAULT, pc, lr, cfsr, hfsr, pack_tag(who), SCB_BFAR);

    uart2_panic_puts("\n\n*** HARDFAULT: ");
    uart2_panic_puts(fault_text(cfsr));
    uart2_panic_puts(" in ");
    uart2_panic_puts(who);
    uart2_panic_puts("\n*** PC=");  raw_hex(pc);
    uart2_panic_puts("  LR=");      raw_hex(lr);
    uart2_panic_puts("  CFSR=");    raw_hex(cfsr);
    uart2_panic_puts("  HFSR=");    raw_hex(hfsr);
    uart2_panic_puts("\n*** Chamber in safe state. Restarting...\n");
    chip_reset();
}

/* ---------------- Console support ---------------- */
void health_print(void)
{
    uint32_t i, now = (uint32_t)xTaskGetTickCount();

    uart2_puts("Boot count  : ");  uart2_put_uint(boot_count);  uart2_puts("\n");
    uart2_puts("Last reset  : ");  print_reason();
    uart2_puts("Watchdog    : ");
    uart2_puts(wdg_running ? "running, 1000 ms timeout, refreshed every 250 ms\n" : "not started\n");
    uart2_puts("Check-ins   :\n");
    for (i = 0U; i < HC_COUNT; i++) {
        uart2_puts("  ");
        uart2_puts(client_name[i]);
        uart2_puts("\t: ");
        uart2_put_uint(now - last_seen[i]);
        uart2_puts(" ms ago\n");
    }
    uart2_puts("Idle        : CPU sleeps (WFI) when no task is ready\n");
}

void health_test_hang(void)  { hang_request = 1; }
int  health_hang_requested(void) { return hang_request; }

void health_test_fault(void)
{
    /* An instruction that is permanently undefined on ARM: the CPU raises a
     * UsageFault, which (not enabled separately) escalates to a HardFault. */
    __asm volatile ("udf #0");
}
