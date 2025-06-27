#ifndef __I2C_H
#define __I2C_H
#include <stdint.h>
#include "stdbool.h"

typedef enum
{
    I2C_OK = 0,
    I2C_ERROR = 1,
    I2C_TIMEOUT = 2,
    I2C_BUSY = 3,
    I2C_NACK = 4
} I2C_Status_t;

#define I2C_WRITE 0U
#define I2C_READ 1U

void i2cInit(void);
I2C_Status_t i2cStart(void);
void i2cStop(void);
I2C_Status_t i2cWrite(uint8_t data);
I2C_Status_t i2cRead(uint8_t *data, bool is_expecting_ack);
I2C_Status_t i2cSendAddress(uint8_t address, uint8_t direction);

#endif /* __I2C_H */