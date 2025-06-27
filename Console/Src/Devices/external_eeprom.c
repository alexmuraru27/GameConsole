#include "external_eeprom.h"
#include "i2c.h"
#include "sysclock.h"

static uint32_t s_device_address = 0U;

void externalEepromInit(const uint32_t device_address)
{
    s_device_address = device_address;
}

uint8_t externalEepromWrite(const uint16_t mem_addr, uint8_t *const data, const uint16_t length)
{
    I2C_Status_t status;
    status = i2cStart();
    if (status != I2C_OK)
    {
        return status;
    }

    status = i2cSendAddress(s_device_address, I2C_WRITE);
    if (status != I2C_OK)
    {
        i2cStop();
        return status;
    }

    // send eeprom high memory address
    status = i2cWrite((mem_addr >> 8U) & 0xFF);
    if (status != I2C_OK)
    {
        i2cStop();
        return status;
    }

    // send eeprom low memory address
    status = i2cWrite(mem_addr & 0xFF);
    if (status != I2C_OK)
    {
        i2cStop();
        return status;
    }

    // send data
    for (uint16_t i = 0U; i < length; i++)
    {
        status = i2cWrite(data[i]);
        if (status != I2C_OK)
        {
            i2cStop();
            return status;
        }
    }

    i2cStop();

    // delay as per message catalog Write Cycle Time (tWR): 5ms maximum
    delay(5U);
    return I2C_OK;
}

uint8_t externalEepromRead(const uint16_t mem_addr, uint8_t *const data, const uint16_t length)
{
    I2C_Status_t status;
    status = i2cStart();
    if (status != I2C_OK)
    {
        return status;
    }
    // 1st address with write
    status = i2cSendAddress(s_device_address, I2C_WRITE);
    if (status != I2C_OK)
    {
        i2cStop();
        return status;
    }
    // send eeprom high memory address
    status = i2cWrite((mem_addr >> 8) & 0xFF);
    if (status != I2C_OK)
    {
        i2cStop();
        return status;
    }
    // send eeprom low memory address
    status = i2cWrite(mem_addr & 0xFF);
    if (status != I2C_OK)
    {
        i2cStop();
        return status;
    }

    // 2nd start address
    status = i2cStart();
    if (status != I2C_OK)
    {
        return status;
    }

    // send address read
    status = i2cSendAddress(s_device_address, I2C_READ);
    if (status != I2C_OK)
    {
        i2cStop();
        return status;
    }

    // read the actual data
    for (uint16_t i = 0U; i < length; i++)
    {
        if (i == length - 1)
        {
            // last read has no ack
            status = i2cRead(&data[i], false);
            i2cStop();
            if (status != I2C_OK)
            {
                return status;
            }
        }
        else
        {
            // read with ack
            status = i2cRead(&data[i], true);
            if (status != I2C_OK)
            {
                i2cStop();
                return status;
            }
        }
    }
    return I2C_OK;
}
