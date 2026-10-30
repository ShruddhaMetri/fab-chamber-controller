/*
 * rtos_app.c
 * Step 11: the old main loop split into FreeRTOS tasks.
 *
 * Why: in the Step 10 main loop every job had to finish quickly, because
 * one slow job (a long console command, a flash erase) delayed all the
 * others - including the PID and the safety checks. With an RTOS each job
 * is a task with a priority: when a more important task becomes ready, the
 * kernel stops the less important one at once and runs the important one.
 *
 * Periodic tasks use xTaskDelayUntil(): "wake me exactly 100 ms after my
 * LAST wake-up time", so the period does not drift even if the work inside
 * takes a variable time. (vTaskDelay would wait 100 ms from NOW, adding the
 * work time to every period.)
 */
#include "rtos_app.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "app_config.h"
#include "uart.h"
#include "gpio.h"
#include "systick.h"
#include "console.h"
#include "safety.h"
#include "sensors.h"
#include "gas.h"
#include "temp_ctrl.h"
#include "datalog.h"
#include "actuators.h"
#include "process.h"

/* ---------------- Priorities (higher number = more urgent) ---------------- */
#define PRIO_SAFETY       4U
#define PRIO_CONTROL      3U
#define PRIO_PROCESS      2U
#define PRIO_LOGGER       2U
#define PRIO_CONSOLE      1U
#define PRIO_HEARTBEAT    1U

/* ---------------- Stack sizes, in 32-bit words ----------------
 * Chosen generously, then checked with the 'tasks' command, which shows the
 * smallest amount each stack has ever had free. */
#define STACK_SAFETY      256U
#define STACK_CONTROL     384U
#define STACK_PROCESS     512U
#define STACK_LOGGER      320U
#define STACK_CONSOLE     768U
#define STACK_HEARTBEAT   128U

#define HEARTBEAT_TICK_MS 250U

/* ---------------- Shared resources ---------------- */
static SemaphoreHandle_t g_flash_mutex = NULL;

/* Control-loop timing, measured by the Control task itself */
static volatile uint32_t g_ctrl_cycles   = 0U;
static volatile uint32_t g_ctrl_min_ms   = 0xFFFFFFFFUL;
static volatile uint32_t g_ctrl_max_ms   = 0U;
static volatile uint32_t g_ctrl_overruns = 0U;   /* periods more than 10 % late */

void flash_lock(void)
{
    (void)xSemaphoreTake(g_flash_mutex, portMAX_DELAY);
}

void flash_unlock(void)
{
    (void)xSemaphoreGive(g_flash_mutex);
}

/* ---------------- Task bodies ---------------- */
static void safety_task(void *arg)
{
    TickType_t wake = xTaskGetTickCount();
    (void)arg;

    for (;;) {
        xTaskDelayUntil(&wake, pdMS_TO_TICKS(SAFETY_SAMPLE_MS));
        safety_poll();               /* debounce inputs, print new trips */
    }
}

static void control_task(void *arg)
{
    TickType_t wake = xTaskGetTickCount();
    uint32_t   last = 0U;
    (void)arg;

    for (;;) {
        xTaskDelayUntil(&wake, pdMS_TO_TICKS(SENSOR_PERIOD_MS));

        /* Measure the real time between two runs (the "jitter") */
        uint32_t now = millis();
        if (g_ctrl_cycles > 0U) {
            uint32_t dt = now - last;
            if (dt < g_ctrl_min_ms) { g_ctrl_min_ms = dt; }
            if (dt > g_ctrl_max_ms) { g_ctrl_max_ms = dt; }
            if (dt > (SENSOR_PERIOD_MS + (SENSOR_PERIOD_MS / 10U))) { g_ctrl_overruns++; }
        }
        last = now;
        g_ctrl_cycles++;

        gas_poll();       /* uses the scan that sensors_poll() started 100 ms ago */
        sensors_poll();   /* reads that scan, checks over-temperature, starts the next */
        tc_poll();        /* PID: new heater power from the new temperature */
    }
}

static void logger_task(void *arg)
{
    TickType_t wake = xTaskGetTickCount();
    (void)arg;

    for (;;) {
        xTaskDelayUntil(&wake, pdMS_TO_TICKS(SENSOR_PERIOD_MS));
        flash_lock();
        datalog_poll();   /* one record per second, or the next step of an erase */
        flash_unlock();
    }
}

static void console_task(void *arg)
{
    (void)arg;

    console_init();                       /* prints the first prompt */
    for (;;) {
        /* Sleep until the UART interrupt says a byte has arrived. While
         * asleep this task uses no CPU time at all. */
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        console_poll();                   /* handle everything received */
    }
}

static void heartbeat_task(void *arg)
{
    TickType_t wake  = xTaskGetTickCount();
    uint32_t   count = 0U;
    (void)arg;

    for (;;) {
        xTaskDelayUntil(&wake, pdMS_TO_TICKS(HEARTBEAT_TICK_MS));
        led_toggle(LED_ORANGE);                               /* every 250 ms  */
        if (++count >= (HEARTBEAT_PERIOD_MS / HEARTBEAT_TICK_MS)) {
            count = 0U;
            led_toggle(LED_GREEN);                            /* every 1000 ms */
        }
    }
}

/* ---------------- Start-up ---------------- */
static TaskHandle_t create(TaskFunction_t fn, const char *name,
                           uint32_t stack_words, uint32_t prio)
{
    TaskHandle_t h = NULL;
    BaseType_t ok = xTaskCreate(fn, name, (configSTACK_DEPTH_TYPE)stack_words,
                                NULL, (UBaseType_t)prio, &h);
    configASSERT(ok == pdPASS);           /* out of heap: stop safely */
    return h;
}

void rtos_app_start(void)
{
    TaskHandle_t console;

    g_flash_mutex = xSemaphoreCreateMutex();
    configASSERT(g_flash_mutex != NULL);
    process_init();                       /* the console -> process command queue */

    (void)create(safety_task,    "Safety",    STACK_SAFETY,    PRIO_SAFETY);
    (void)create(control_task,   "Control",   STACK_CONTROL,   PRIO_CONTROL);
    (void)create(process_task,   "Process",   STACK_PROCESS,   PRIO_PROCESS);
    (void)create(logger_task,    "Logger",    STACK_LOGGER,    PRIO_LOGGER);
    console = create(console_task, "Console", STACK_CONSOLE,   PRIO_CONSOLE);
    (void)create(heartbeat_task, "Heartbeat", STACK_HEARTBEAT, PRIO_HEARTBEAT);

    uart2_rtos_init(console);             /* UART interrupt will wake the console */

    vTaskStartScheduler();                /* from here on, the kernel decides what runs */

    /* Only reached if there was not enough heap for the idle task */
    for (;;) {
    }
}

/* ---------------- 'tasks' command ---------------- */
static const char *state_name(eTaskState s)
{
    switch (s) {
    case eRunning:   return "Running ";
    case eReady:     return "Ready   ";
    case eBlocked:   return "Blocked ";
    case eSuspended: return "Suspend ";
    default:         return "Other   ";
    }
}

static void pad(const char *s, uint32_t width)
{
    uint32_t n = 0U;
    uart2_puts(s);
    while (s[n] != '\0') { n++; }
    while (n++ < width) { uart2_putc(' '); }
}

void rtos_print_tasks(void)
{
    TaskStatus_t list[8];
    UBaseType_t  n, i;

    n = uxTaskGetSystemState(list, 8U, NULL);

    uart2_puts("Task       Prio  State     Stack free (lowest ever)\n");
    for (i = 0U; i < n; i++) {
        pad(list[i].pcTaskName, 11U);
        uart2_put_uint(list[i].uxCurrentPriority);
        uart2_puts("     ");
        uart2_puts(state_name(list[i].eCurrentState));
        uart2_puts("  ");
        uart2_put_uint((uint32_t)list[i].usStackHighWaterMark * 4U);
        uart2_puts(" bytes\n");
    }

    uart2_puts("Heap free  : ");
    uart2_put_uint((uint32_t)xPortGetFreeHeapSize());
    uart2_puts(" bytes (lowest ever ");
    uart2_put_uint((uint32_t)xPortGetMinimumEverFreeHeapSize());
    uart2_puts(")\n");

    uart2_puts("Control    : ");
    uart2_put_uint(g_ctrl_cycles);
    uart2_puts(" cycles, period min ");
    uart2_put_uint((g_ctrl_cycles > 1U) ? g_ctrl_min_ms : 0U);
    uart2_puts(" ms, max ");
    uart2_put_uint(g_ctrl_max_ms);
    uart2_puts(" ms, late ");
    uart2_put_uint(g_ctrl_overruns);
    uart2_puts("\n");
}

/* ---------------- Fatal software errors ----------------
 * A bug the kernel can detect (stack overflow, failed check, no memory)
 * must never leave the heater running. Put the chamber in the safe state,
 * say what happened, and stop. (Step 14 adds a watchdog that will then
 * restart the controller automatically.) */
static void fatal(const char *what, const char *detail)
{
    taskDISABLE_INTERRUPTS();             /* stop task switching; e-stop ISRs still run */
    actuators_safe_state();
    led_on(LED_RED);
    uart2_panic_puts("\n\n*** FATAL SOFTWARE ERROR: ");
    uart2_panic_puts(what);
    uart2_panic_puts(detail);
    uart2_panic_puts("\n*** Chamber in safe state. Controller halted.\n");
    for (;;) {
    }
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)task;
    fatal("stack overflow in task ", name);
}

void vApplicationMallocFailedHook(void)
{
    fatal("out of heap memory", "");
}

void rtos_assert_failed(const char *file, int line)
{
    (void)line;
    fatal("check failed in ", file);
}
