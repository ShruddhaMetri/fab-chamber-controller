/*
 * canlink.c
 * CAN task: reports the chamber's state to the fab host and accepts
 * remote commands. See canlink.h for the message table.
 *
 * Like the Process task, it waits on a queue (the CAN receive queue) with a
 * timeout, so one call gives "react to a frame at once" and "wake every 100 ms".
 */
#include "canlink.h"
#include "can.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app_config.h"
#include "uart.h"
#include "safety.h"
#include "sensors.h"
#include "actuators.h"
#include "gas.h"
#include "process.h"
#include "recipe.h"
#include "rtos_app.h"
#include "stm32f407_regs.h"

#define PERIOD_MS         100U
#define STATUS_EVERY      5U        /* x 100 ms = status every 500 ms     */
#define HOST_TIMEOUT_MS   3000U     /* no heartbeat for 3 s = link down   */

static uint8_t  seq          = 0U;
static uint8_t  alive        = 0U;
static uint32_t host_seen_ms = 0U;
static int      host_up      = 0;
static int      host_ever    = 0;
static uint8_t  last_cmd     = 0U;
static uint8_t  last_result  = 0U;

static uint32_t now_ms(void) { return (uint32_t)xTaskGetTickCount(); }

static void put16(uint8_t *d, uint32_t v)          /* little-endian */
{
    d[0] = (uint8_t)(v & 0xFFU);
    d[1] = (uint8_t)((v >> 8) & 0xFFU);
}

static void send_event(uint8_t code)
{
    proc_status_t st;
    can_frame_t f = { CANID_EVENT, 4U, {0} };

    process_get_status(&st);
    f.data[0] = code;
    f.data[1] = safety_fault_source();
    f.data[2] = (uint8_t)st.state;
    f.data[3] = seq++;
    (void)can1_send(&f);
}

static void send_status(void)
{
    proc_status_t st;
    can_frame_t s = { CANID_STATUS,  8U, {0} };
    can_frame_t p = { CANID_PROCESS, 8U, {0} };

    process_get_status(&st);

    s.data[0] = safety_fault_source();
    s.data[1] = (uint8_t)st.state;
    put16(&s.data[2], sensors_temp_dc());
    put16(&s.data[4], sensors_pressure_mtorr());
    s.data[6] = (uint8_t)(heater_get_permille() / 10U);
    s.data[7] = (uint8_t)(valve_get_permille() / 10U);

    put16(&p.data[0], gas_measured_sccm(GAS_MFC1));
    put16(&p.data[2], gas_measured_sccm(GAS_MFC2));
    put16(&p.data[4], st.remaining_s);
    p.data[6] = alive++;
    p.data[7] = (uint8_t)host_up;

    (void)can1_send(&s);
    (void)can1_send(&p);
}

static void reply(uint8_t cmd, uint8_t result, uint8_t arg)
{
    can_frame_t r = { CANID_REPLY, 3U, { cmd, result, arg } };
    last_cmd    = cmd;
    last_result = result;
    (void)can1_send(&r);
}

/* A command from the host. Exactly the same checks as the console's 'run'
 * and 'abort': a remote command gets no extra rights. */
static void handle_command(const can_frame_t *f)
{
    uint8_t  cmd = (f->len >= 1U) ? f->data[0] : 0U;
    uint8_t  arg = (f->len >= 2U) ? f->data[1] : 0U;
    recipe_t r;
    int      rc;

    switch (cmd) {
    case CMD_RUN:
        if (process_is_running() || safety_is_faulted()) {
            reply(cmd, RES_REFUSED, arg);
            break;
        }
        if (arg < 1U || arg > RECIPE_SLOTS) {
            reply(cmd, RES_BAD_RECIPE, arg);
            break;
        }
        storage_lock();                         /* the console uses the EEPROM too */
        rc = recipe_load((uint32_t)arg - 1U, &r);
        storage_unlock();
        if (rc != RECIPE_OK) {
            reply(cmd, RES_BAD_RECIPE, arg);
        } else if (process_request_run(&r)) {
            uart2_puts("\n[CAN] Host started recipe ");
            uart2_put_uint(arg);
            uart2_puts("\n> ");
            reply(cmd, RES_OK, arg);
        } else {
            reply(cmd, RES_REFUSED, arg);
        }
        break;

    case CMD_ABORT:
        if (process_is_running() && process_request_abort()) {
            uart2_puts("\n[CAN] Host requested abort\n> ");
            reply(cmd, RES_OK, 0U);
        } else {
            reply(cmd, RES_REFUSED, 0U);
        }
        break;

    case CMD_PING:
        reply(cmd, RES_OK, arg);                /* echo: "I am here" */
        break;

    default:
        reply(cmd, RES_UNKNOWN, arg);
        break;
    }
}

void canlink_task(void *arg)
{
    can_frame_t  f;
    proc_status_t st0;
    uint8_t      last_faults = safety_fault_source();
    proc_state_t last_state;
    uint32_t     tick        = 0U;
    TickType_t   next        = xTaskGetTickCount() + pdMS_TO_TICKS(PERIOD_MS);
    (void)arg;

    process_get_status(&st0);
    last_state = st0.state;
    send_event(EV_BOOT);                        /* carries the starting faults and step */

    for (;;) {
        TickType_t now  = xTaskGetTickCount();
        TickType_t wait = ((int32_t)(next - now) > 0) ? (next - now) : 0U;

        if (can1_receive(&f, (uint32_t)wait)) {
            /* A frame arrived (the hardware filter only lets 0x200-0x21F through) */
            host_seen_ms = now_ms();
            host_ever    = 1;
            if (f.id == CANID_COMMAND) {
                handle_command(&f);
            }
            /* CANID_HEARTBEAT needs no answer: receiving it is the point */
            continue;
        }

        /* ---- every 100 ms ---- */
        next += pdMS_TO_TICKS(PERIOD_MS);
        {
            proc_status_t st;
            uint8_t faults = safety_fault_source();
            process_get_status(&st);

            if (faults != last_faults) {        /* something tripped or was cleared */
                last_faults = faults;
                send_event(EV_FAULT);
            }
            if (st.state != last_state) {       /* process moved to another step */
                last_state = st.state;
                send_event(EV_STEP);
            }
        }

        /* Host link supervision */
        {
            int up = host_ever && ((now_ms() - host_seen_ms) < HOST_TIMEOUT_MS);
            if (up != host_up) {
                host_up = up;
                uart2_puts(up ? "\n[CAN] Fab host link UP\n> "
                              : "\n[CAN] Fab host link LOST (no heartbeat for 3 s)\n> ");
            }
        }

        if (++tick >= STATUS_EVERY) {
            tick = 0U;
            send_status();
        }
    }
}

/* ---------------- 'can' console command ---------------- */
void canlink_print(void)
{
    uint32_t esr = can1_error_status();

    uart2_puts("Bit rate    : 500 kbit/s  (PB8 RX, PB9 TX)\n");
    uart2_puts("Sent        : ");  uart2_put_uint(can1_tx_count());
    uart2_puts(" frames  (dropped, all mailboxes busy: ");  uart2_put_uint(can1_tx_dropped());
    uart2_puts(")\nReceived    : ");  uart2_put_uint(can1_rx_count());
    uart2_puts(" frames  (lost, FIFO overrun: ");  uart2_put_uint(can1_rx_overruns());
    uart2_puts(")\nErrors      : TEC ");  uart2_put_uint((esr >> 16) & 0xFFU);
    uart2_puts("  REC ");  uart2_put_uint((esr >> 24) & 0xFFU);
    uart2_puts((esr & CAN_ESR_BOFF) ? "  BUS-OFF\n" :
               (esr & CAN_ESR_EPVF) ? "  error-passive\n" : "  (error-active, normal)\n");
    uart2_puts("Host link   : ");
    if (!host_ever) {
        uart2_puts("never seen\n");
    } else {
        uart2_puts(host_up ? "UP" : "LOST");
        uart2_puts(", last frame ");
        uart2_put_uint(now_ms() - host_seen_ms);
        uart2_puts(" ms ago\n");
    }
    uart2_puts("Last command: ");
    if (last_cmd == 0U) {
        uart2_puts("none\n");
    } else {
        uart2_puts(last_cmd == CMD_RUN ? "RUN" : last_cmd == CMD_ABORT ? "ABORT" :
                   last_cmd == CMD_PING ? "PING" : "unknown");
        uart2_puts(" -> ");
        uart2_puts(last_result == RES_OK ? "OK\n" : last_result == RES_REFUSED ? "refused\n" :
                   last_result == RES_BAD_RECIPE ? "bad recipe\n" : "unknown command\n");
    }
}
