/*
 * console.c
 * Reads characters from the UART, builds a line, and runs the command
 * when Enter is pressed.
 */
#include <string.h>
#include "console.h"
#include "uart.h"
#include "gpio.h"
#include "systick.h"
#include "stm32f407_regs.h"
#include "app_config.h"
#include "safety.h"

#define LINE_MAX   32U

static char     line[LINE_MAX];
static uint32_t line_len = 0;
static int      last_was_cr = 0;

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
        uart2_puts(" )\n");
    } else {
        uart2_puts("OK\n");
    }
    uart2_puts("Trips      : ");
    uart2_put_uint(safety_trip_count());
    uart2_puts("\n");

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
        uart2_puts("Cannot clear: e-stop is pressed or door is open.\n");
    }
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
