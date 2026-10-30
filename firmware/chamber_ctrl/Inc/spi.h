/*
 * spi.h
 * SPI2 master driver.  PB13 = SCK (clock), PB14 = MISO (data in),
 * PB15 = MOSI (data out), PB12 = CS (chip select, active LOW, driven by us).
 */
#ifndef SPI_H
#define SPI_H

#include <stdint.h>

void    spi2_init(void);
void    spi2_select(void);            /* CS low: start of a transaction */
void    spi2_deselect(void);          /* CS high: end of a transaction  */
uint8_t spi2_transfer(uint8_t out);   /* send one byte, receive one byte at the same time */

#endif /* SPI_H */
