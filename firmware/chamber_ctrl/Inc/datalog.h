/*
 * datalog.h
 * Process data logger on the SPI flash: one 16-byte record per second
 * (time, state, temperature, pressure, heater, gas flows), in the first
 * 64 KB of the flash = 4096 records, about 68 minutes.
 */
#ifndef DATALOG_H
#define DATALOG_H

#include <stdint.h>

typedef struct {
    uint8_t  tag;                /* LOG_TAG = valid record, 0xFF = empty slot */
    uint8_t  faults;             /* fault bits at that moment                 */
    uint16_t temp_dc;            /* 0.1 C                                     */
    uint16_t pressure_mtorr;
    uint16_t heater_permille;
    uint16_t gas1_sccm;
    uint16_t gas2_sccm;
    uint32_t uptime_s;
} log_record_t;

void     datalog_init(void);            /* find where to continue writing      */
void     datalog_poll(void);            /* every SENSOR_PERIOD_MS (100 ms)      */

int      datalog_ok(void);              /* 1 if the flash was found             */
uint32_t datalog_count(void);           /* records stored                       */
uint32_t datalog_capacity(void);
int      datalog_get(uint32_t index, log_record_t *rec);  /* 0 = oldest; 1 if valid */
void     datalog_erase_all(void);       /* starts a background erase            */
int      datalog_busy_erasing(void);
uint32_t datalog_flash_id(void);

#endif /* DATALOG_H */
