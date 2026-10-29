/*
 * app_config.h
 * Settings for the whole application, kept in one place.
 */
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#define FW_NAME                "Fab Chamber Controller"
#define FW_VERSION             "0.6"

#define CPU_CLOCK_HZ           16000000UL   /* internal HSI oscillator after reset */
#define UART_BAUD              115200UL

#define HEARTBEAT_PERIOD_MS    1000UL
#define ACTIVITY_PERIOD_MS     250UL

/* Safety inputs are sampled this often for debouncing */
#define SAFETY_SAMPLE_MS       5UL
/* An input must read the same value this many samples in a row (4 x 5 ms = 20 ms) */
#define DEBOUNCE_SAMPLES       4U

/* Sensors are converted and checked this often */
#define SENSOR_PERIOD_MS       100UL

/* Over-temperature interlock, in tenths of a degree C (5000 = 500.0 C) */
#define OVERTEMP_LIMIT_DC      5000U
/* Must cool below this before the fault may be cleared (hysteresis) */
#define OVERTEMP_RESET_DC      4800U
/* Readings in a row above the limit before tripping (3 x 100 ms) */
#define OVERTEMP_SAMPLES       3U

#endif /* APP_CONFIG_H */
