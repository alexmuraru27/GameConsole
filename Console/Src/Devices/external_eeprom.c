#include "external_eeprom.h"
#include "i2c.h"
#include "sysclock.h"
#include "stddef.h"

static uint32_t s_device_address = 0U;

void externalEepromInit(const uint32_t device_address)
{
    s_device_address = device_address;
}

static uint8_t externalEepromWritePage(const uint16_t mem_addr, uint8_t *const data, const uint16_t length)
{
    if (data == NULL || length == 0U || length > EXTERNAL_EEPROM_AT24C512_PAGE_SIZE)
    {
        return I2C_ERROR;
    }

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

    // send data (max 64 bytes per page)
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
    return I2C_OK;
}

uint8_t externalEepromWrite(const uint16_t mem_addr, uint8_t *const data, const uint16_t length)
{
    if (data == NULL || length == 0U || mem_addr > EXTERNAL_EEPROM_AT24C512_MAX_MEMORY_ADDR)
    {
        return I2C_ERROR;
    }

    uint16_t bytes_written = 0U;
    uint16_t current_addr = mem_addr;

    while (bytes_written < length)
    {
        // calculate bytes per page
        const uint16_t page_offset = current_addr % EXTERNAL_EEPROM_AT24C512_PAGE_SIZE;
        const uint16_t bytes_remaining_in_page = EXTERNAL_EEPROM_AT24C512_PAGE_SIZE - page_offset;
        const uint16_t bytes_remaining_total = length - bytes_written;
        const uint16_t bytes_to_write = (bytes_remaining_total < bytes_remaining_in_page) ? bytes_remaining_total : bytes_remaining_in_page;

        // write in this page
        uint8_t status = I2C_OK;
        if (bytes_to_write > 0U)
        {
            status = externalEepromWritePage(current_addr,
                                             &data[bytes_written],
                                             bytes_to_write);
        }

        if (status != I2C_OK)
        {
            return status;
        }

        bytes_written += bytes_to_write;
        current_addr += bytes_to_write;

        // delay as per message catalog Write Cycle Time (tWR): 5ms maximum
        delay(5U);
    }

    return I2C_OK;
}

uint8_t externalEepromRead(const uint16_t mem_addr, uint8_t *const data, const uint16_t length)
{
    if (mem_addr > EXTERNAL_EEPROM_AT24C512_MAX_MEMORY_ADDR)
    {
        return I2C_ERROR;
    }

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

uint8_t externalEepromClear()
{
    uint8_t data[EXTERNAL_EEPROM_AT24C512_PAGE_SIZE] = {0U};
    I2C_Status_t status;
    for (uint16_t i = 0U; i <= EXTERNAL_EEPROM_AT24C512_MAX_MEMORY_ADDR / EXTERNAL_EEPROM_AT24C512_PAGE_SIZE; i++)
    {
        status = externalEepromWrite(i * EXTERNAL_EEPROM_AT24C512_PAGE_SIZE, data, EXTERNAL_EEPROM_AT24C512_PAGE_SIZE);
        if (status != I2C_OK)
        {
            return status;
        }
    }

    return I2C_OK;
}