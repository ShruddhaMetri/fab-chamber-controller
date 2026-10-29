/*
 * app_config.h
 * Settings for the whole application, kept in one place.
 */
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#define FW_NAME                "Fab Chamber Controller"
#define FW_VERSION             "0.3"

#define CPU_CLOCK_HZ           16000000UL   /* internal HSI oscillator after reset */
#define UART_BAUD              115200UL

#define HEARTBEAT_PERIOD_MS    1000UL
#define ACTIVITY_PERIOD_MS     250UL

#endif /* APP_CONFIG_H */
