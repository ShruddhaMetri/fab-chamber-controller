/*
 * i2c.h
 * I2C1 master driver, 100 kHz.  PB6 = SCL (clock), PB7 = SDA (data).
 * Every function returns I2C_OK or an error code, and gives up after a
 * timeout instead of hanging forever if a device does not answer.
 */
#ifndef I2C_H
#define I2C_H

#include <stdint.h>

#define I2C_OK          0
#define I2C_ERR_TIMEOUT (-1)   /* a step did not finish in time      */
#define I2C_ERR_NACK    (-2)   /* the device did not acknowledge     */

void i2c1_init(void);

/* Send 'len' bytes to the device at 7-bit address 'addr' */
int  i2c1_write(uint8_t addr, const uint8_t *data, uint32_t len);

/* Send 'wlen' bytes (e.g. a memory address), then read 'rlen' bytes back */
int  i2c1_write_read(uint8_t addr, const uint8_t *wdata, uint32_t wlen,
                     uint8_t *rdata, uint32_t rlen);

#endif /* I2C_H */
