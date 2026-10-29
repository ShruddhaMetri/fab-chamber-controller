/*
 * sensors.h
 * Converts raw ADC counts into engineering units and watches temperature.
 *   Temperature transmitter : 0-3.3 V  = 0-800.0 C
 *   Pressure transmitter    : 0-3.3 V  = 0-10.000 Torr
 */
#ifndef SENSORS_H
#define SENSORS_H

#include <stdint.h>

void     sensors_init(void);
void     sensors_poll(void);              /* call every SENSOR_PERIOD_MS */

uint32_t sensors_temp_dc(void);           /* temperature, tenths of a degree C */
uint32_t sensors_pressure_mtorr(void);    /* pressure, milli-Torr              */
int      sensors_overtemp_active(void);   /* 1 while not yet cooled below reset limit */

#endif /* SENSORS_H */
