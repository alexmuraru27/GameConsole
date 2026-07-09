#include "i2c.h"
#include <stm32f407xx.h>
#include <stddef.h>
#include "gpio.h"
#include "sysclock.h"
#include "logger.h"

#define I2C_CCR_VALUE 0x23U                     // Calculated for 400kHz with 42MHz APB1
#define I2C_TRISE_VALUE 14U                     // Rise time for fast mode
#define I2C_APB1_FREQ_MHZ (PCLK1_HZ / 1000000U) // APB1 (PCLK1) frequency in MHz

#define I2C_TIMEOUT_COUNT 1000000U

void i2cInit(void)
{
    // Free the bus before configuring the peripheral: a slave left mid-byte by a
    // prior reset can hold SDA low and wedge every START. Recovery bit-bangs the
    // pins as GPIO (they're set up by gpioInit, which runs first), then restores AF.
    const uint8_t recovery_pulses = gpioI2cBusRecovery();
    if (recovery_pulses > 0U)
    {
        LOGGER_LOG_WARN(LOGGER_CORE, "I2C bus was stuck; freed with %u SCL pulse(s)",
                        (unsigned)recovery_pulses);
    }

    // trigger reset
    I2C1->CR1 |= I2C_CR1_SWRST;
    I2C1->CR1 &= ~I2C_CR1_SWRST;

    // set apb1 freq to 42Mhz
    I2C1->CR2 &= ~I2C_CR2_FREQ;
    I2C1->CR2 |= I2C_APB1_FREQ_MHZ;

    // fast mode 400khz
    I2C1->CCR &= ~I2C_CCR_CCR;
    I2C1->CCR |= I2C_CCR_VALUE;
    I2C1->CCR |= I2C_CCR_FS;

    // config rise time
    I2C1->TRISE = I2C_TRISE_VALUE;

    // enable i2c
    I2C1->CR1 |= I2C_CR1_PE;

    // enable ack for the 1st operation
    I2C1->CR1 |= I2C_CR1_ACK;
    LOGGER_LOG_DEBUG(LOGGER_CORE, "I2C1 init: fast mode 400kHz");
}

I2C_Status_t i2cStart(void)
{
    uint32_t timeout = I2C_TIMEOUT_COUNT;

    // Check if we're already the master (repeated START case)
    // MSL=1, we're already master, so skip BUSY check
    if (!(I2C1->SR2 & I2C_SR2_MSL))
    {
        // Normal START - wait for bus to be free
        while ((I2C1->SR2 & I2C_SR2_BUSY) && --timeout)
        {
        }
        if (!timeout)
        {
            return I2C_TIMEOUT;
        }
    }

    // ACK for new transfer (might be disabled from previous read)
    I2C1->CR1 |= I2C_CR1_ACK;

    I2C1->CR1 |= I2C_CR1_START;

    // Wait for START bit to be set
    timeout = I2C_TIMEOUT_COUNT;
    while (!(I2C1->SR1 & I2C_SR1_SB) && --timeout)
    {
    }
    if (!timeout)
    {
        return I2C_TIMEOUT;
    }
    return I2C_OK;
}

void i2cStop(void)
{
    I2C1->CR1 |= I2C_CR1_STOP;

    while (I2C1->CR1 & I2C_CR1_STOP)
    {
    }
}

I2C_Status_t i2cWrite(const uint8_t data)
{
    uint32_t timeout = I2C_TIMEOUT_COUNT;

    while (!(I2C1->SR1 & I2C_SR1_TXE) && --timeout)
    {
        // is acknoledgement failure
        if (I2C1->SR1 & I2C_SR1_AF)
        {
            I2C1->SR1 &= ~I2C_SR1_AF;
            return I2C_NACK;
        }
    }
    if (!timeout)
    {
        return I2C_TIMEOUT;
    }

    I2C1->DR = data;

    timeout = I2C_TIMEOUT_COUNT;
    // byte transfer finished
    while (!(I2C1->SR1 & I2C_SR1_BTF) && --timeout)
    {
        // check ack
        if (I2C1->SR1 & I2C_SR1_AF)
        {
            I2C1->SR1 &= ~I2C_SR1_AF;
            return I2C_NACK;
        }
    }

    if (!timeout)
    {
        return I2C_TIMEOUT;
    }

    return I2C_OK;
}

I2C_Status_t i2cRead(uint8_t *const data, bool is_expecting_ack)
{
    if (data == NULL)
    {
        return I2C_ERROR;
    }

    uint32_t timeout = I2C_TIMEOUT_COUNT;
    if (is_expecting_ack)
    {
        I2C1->CR1 |= I2C_CR1_ACK;
    }
    else
    {
        I2C1->CR1 &= ~I2C_CR1_ACK;
    }

    while (!(I2C1->SR1 & I2C_SR1_RXNE) && --timeout)
    {
    }
    if (!timeout)
    {
        return I2C_TIMEOUT;
    }

    *data = I2C1->DR;

    return I2C_OK;
}

I2C_Status_t i2cSendAddress(const uint8_t address, const uint8_t direction)
{
    uint32_t timeout = I2C_TIMEOUT_COUNT;

    // set address +direction
    I2C1->DR = (address << 1) | direction;

    // wait ack
    while (!(I2C1->SR1 & I2C_SR1_ADDR) && --timeout)
    {
        // if no ack
        if (I2C1->SR1 & I2C_SR1_AF)
        {
            // clear flag+err
            I2C1->SR1 &= ~I2C_SR1_AF;
            return I2C_NACK;
        }
    }
    if (!timeout)
    {
        return I2C_TIMEOUT;
    }

    // clear addr flag
    volatile uint32_t temp = I2C1->SR1;
    temp = I2C1->SR2;
    (void)temp;

    return I2C_OK;
}