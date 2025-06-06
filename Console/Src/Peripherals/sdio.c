#include "stm32f4xx.h"
#include <string.h>
#include <stdint.h>
#include "sysclock.h"
#include "sdio.h"

#define CMD41 41 // SD_SEND_OP_COND (ACMD)

void sdioInit(void)
{
    // Complete SDIO reset sequence
    SDIO->POWER = 0x00; // Power off
    delay(50U);         // Wait for power stabilization

    // Power on sequence with proper timing
    SDIO->POWER = SDIO_POWER_PWRCTRL_1; // Power cycle on
    delay(10U);
    SDIO->POWER = SDIO_POWER_PWRCTRL_1 | SDIO_POWER_PWRCTRL_0; // Power on + enable outputs
    delay(50U);                                                // Critical delay for Black Board power stabilization

    // Clear all flags
    SDIO->ICR = 0x5FF;

    // Configure clock control register for conservative initialization
    SDIO->CLKCR = 4U | SDIO_CLKCR_CLKEN;
    delay(10U);

    // Configure timeouts
    SDIO->DTIMER = 0xFFFFFFFF; // Maximum data timeout
}

uint8_t sdSwitchTo4bitMode(const uint32_t rca)
{
    uint8_t ret;

    // Send CMD55 + ACMD6 to set 4-bit mode
    ret = sdioSendCommand(CMD55, rca, SD_RESP_SHORT);
    if (ret != SD_OK)
    {
        return ret;
    }

    // ACMD6: SET_BUS_WIDTH (argument: 2 = 4-bit mode)
    ret = sdioSendCommand(ACMD6, 2, SD_RESP_SHORT);
    if (ret != SD_OK)
    {
        return ret;
    }

    // Update SDIO controller for 4-bit mode
    SDIO->CLKCR |= SDIO_CLKCR_WIDBUS_0; // Set 4-bit mode
    return SD_OK;
}

uint8_t sdioSendCommand(uint8_t cmd, uint32_t arg, uint8_t resp_type)
{
    uint32_t timeout = 2000000; // Increased timeout for Black Board
    uint32_t cmd_reg = 0;
    uint32_t status;

    // Wait for command path to be ready with timeout
    timeout = 1000000U;
    while ((SDIO->STA & SDIO_STA_CMDACT) && timeout--)
    {
        __NOP();
    }
    if (timeout == 0)
    {
        return SD_TIMEOUT;
    }

    // Clear all flags before sending command
    SDIO->ICR = 0x5FF;

    // Set argument
    SDIO->ARG = arg;

    // Configure command register
    cmd_reg = cmd | SDIO_CMD_CPSMEN;

    if (resp_type == SD_RESP_SHORT)
    {
        cmd_reg |= SDIO_CMD_WAITRESP_0;
    }
    else if (resp_type == SD_RESP_LONG)
    {
        cmd_reg |= SDIO_CMD_WAITRESP_1 | SDIO_CMD_WAITRESP_0;
    }

    // Send command
    SDIO->CMD = cmd_reg;

    // Reset timeout for response
    timeout = 2000000U;

    // Wait for response or timeout
    if (resp_type != SD_RESP_NONE)
    {
        do
        {
            status = SDIO->STA;
            timeout--;
        } while (!(status & (SDIO_STA_CMDREND | SDIO_STA_CCRCFAIL | SDIO_STA_CTIMEOUT)) && timeout > 0);

        if (timeout == 0 || (status & SDIO_STA_CTIMEOUT))
        {
            return SD_TIMEOUT;
        }

        // Handle CRC failures - ignore for specific commands
        if (status & SDIO_STA_CCRCFAIL)
        {
            if (cmd != CMD2 && cmd != CMD3 && cmd != CMD41)
            {
                return SD_ERROR;
            }
            // Clear CRC fail flag for commands that don't use CRC
            SDIO->ICR = SDIO_STA_CCRCFAIL;
        }
    }
    else
    {
        delay(2); // Longer delay for commands without response
    }

    return SD_OK;
}

// Robust ACMD41 implementation specifically for Black Board issues
uint8_t sdioSendRobustAcmd41(void)
{
    uint32_t response;
    uint8_t ret;
    uint32_t timeout_ms = 3000; // 3 second timeout for Black Board
    uint32_t start_time = getSysTime();
    uint32_t attempt = 0;

    while ((getSysTime() - start_time) < timeout_ms)
    {
        attempt++;

        // Send CMD55: Application command
        ret = sdioSendCommand(CMD55, 0, SD_RESP_SHORT);
        if (ret != SD_OK)
        {
            delay(50); // Longer delay between retries
            continue;
        }

        // Check CMD55 response - should have APP_CMD bit set
        response = SDIO->RESP1;
        if (!(response & (1 << 5U)))
        {
            delay(50);
            continue;
        }

        // Send ACMD41: SD_SEND_OP_COND with proper voltage range and HCS bit
        // Use full voltage range (0x00FF8000) + HCS bit (0x40000000) + busy bit check
        uint32_t acmd41_arg = 0x40FF8000; // HCS + voltage range 2.7-3.6V

        ret = sdioSendCommand(CMD41, acmd41_arg, SD_RESP_SHORT);
        if (ret != SD_OK)
        {
            delay(50);
            continue;
        }

        // Clear any CRC error flags (normal for ACMD41)
        SDIO->ICR = SDIO_STA_CCRCFAIL;

        // Get response
        response = SDIO->RESP1;

        // Check if card is ready (bit 31 set)
        if (response & 0x80000000)
        {

            return SD_OK;
        }

        // Wait before next attempt - important for Black Board
        delay(25); // 25ms delay between attempts
    }
    return SD_TIMEOUT;
}
