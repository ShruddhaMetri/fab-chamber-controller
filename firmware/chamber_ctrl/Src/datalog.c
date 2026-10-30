/*
 * datalog.c
 * Append-only log in SPI flash.
 *
 * Start-up: read records from the beginning until the first empty one
 * (tag 0xFF): that is where writing continues, so the log survives resets.
 * If the area contains something that is neither a record nor empty,
 * the flash is "unformatted" and is erased first.
 *
 * Erasing takes up to 400 ms per 4 KB sector on a real chip. To avoid
 * blocking the main loop (and the safety checks in it), erasing runs in the
 * background: one sector is started, and the next one only when the chip
 * reports it is no longer busy.
 */
#include <string.h>
#include "datalog.h"
#include "flash.h"
#include "sensors.h"
#include "actuators.h"
#include "gas.h"
#include "safety.h"
#include "systick.h"
#include "uart.h"
#include "app_config.h"

#define LOG_TAG          0xA5U
#define LOG_BASE         0x000000UL
#define LOG_SECTORS      16U
#define LOG_SIZE         (LOG_SECTORS * FLASH_SECTOR_SIZE)          /* 64 KB */
#define LOG_REC_SIZE     16U
#define LOG_CAPACITY     (LOG_SIZE / LOG_REC_SIZE)                  /* 4096  */
#define MICRON_ID        0x20U       /* first byte of the JEDEC ID */

_Static_assert(sizeof(log_record_t) == LOG_REC_SIZE, "log record must be 16 bytes");

static int      flash_ok      = 0;
static uint32_t flash_id      = 0U;
static uint32_t write_index   = 0U;     /* next free record                    */
static uint32_t erase_sector  = 0U;     /* next sector to erase                */
static int      erasing       = 0;
static uint32_t tick          = 0U;     /* counts 100 ms polls                 */
static int      full_reported = 0;

static uint32_t rec_addr(uint32_t index)
{
    return LOG_BASE + (index * LOG_REC_SIZE);
}

void datalog_erase_all(void)
{
    erase_sector = 0U;
    erasing      = 1;
    write_index  = 0U;
    full_reported = 0;
}

void datalog_init(void)
{
    uint8_t  tag;
    uint32_t i;

    flash_init();
    flash_id = flash_read_id();
    flash_ok = ((flash_id >> 16) == MICRON_ID);
    if (!flash_ok) {
        uart2_puts("Data log: flash not found. Logging disabled.\n");
        return;
    }

    /* Find the first empty record: read only the 1-byte tag of each */
    for (i = 0U; i < LOG_CAPACITY; i++) {
        flash_read(rec_addr(i), &tag, 1U);
        if (tag == 0xFFU) {
            break;                      /* empty: continue writing here   */
        }
        if (tag != LOG_TAG) {
            uart2_puts("Data log: flash not formatted, erasing in background...\n");
            datalog_erase_all();
            return;
        }
    }
    write_index = i;
    uart2_puts("Data log: ");
    uart2_put_uint(write_index);
    uart2_puts(" records found.\n");
}

static void write_record(void)
{
    log_record_t r;

    r.tag             = LOG_TAG;
    r.faults          = safety_fault_source();
    r.temp_dc         = (uint16_t)sensors_temp_dc();
    r.pressure_mtorr  = (uint16_t)sensors_pressure_mtorr();
    r.heater_permille = (uint16_t)heater_get_permille();
    r.gas1_sccm       = (uint16_t)gas_measured_sccm(GAS_MFC1);
    r.gas2_sccm       = (uint16_t)gas_measured_sccm(GAS_MFC2);
    r.uptime_s        = millis() / 1000U;

    /* 16-byte records never cross a 256-byte page, so one program is enough */
    flash_program(rec_addr(write_index), (const uint8_t *)&r, LOG_REC_SIZE);
    write_index++;
}

void datalog_poll(void)
{
    if (!flash_ok || flash_is_busy()) {
        return;                         /* nothing to do, or chip still working */
    }

    if (erasing) {
        if (erase_sector < LOG_SECTORS) {
            flash_erase_sector(LOG_BASE + (erase_sector * FLASH_SECTOR_SIZE));
            erase_sector++;
        } else {
            erasing = 0;
            uart2_puts("\nData log: erase complete.\n> ");
        }
        return;
    }

    tick++;
    if ((tick * SENSOR_PERIOD_MS) < LOG_PERIOD_MS) {
        return;                         /* not yet time for the next record */
    }
    tick = 0U;

    if (write_index >= LOG_CAPACITY) {
        if (!full_reported) {
            uart2_puts("\nData log full. Type 'log erase' to start again.\n> ");
            full_reported = 1;
        }
        return;
    }
    write_record();
}

int      datalog_ok(void)            { return flash_ok; }
uint32_t datalog_count(void)         { return erasing ? 0U : write_index; }
uint32_t datalog_capacity(void)      { return LOG_CAPACITY; }
int      datalog_busy_erasing(void)  { return erasing; }
uint32_t datalog_flash_id(void)      { return flash_id; }

int datalog_get(uint32_t index, log_record_t *rec)
{
    if (!flash_ok || erasing || (index >= write_index)) {
        return 0;
    }
    flash_read(rec_addr(index), (uint8_t *)rec, LOG_REC_SIZE);
    return rec->tag == LOG_TAG;
}
