#include "diskio_integration.h"
#include "sdio.h"
#include "sysclock.h"
#include "logger.h"

static uint32_t s_sd_rca = 0;
static uint8_t s_sd_type = 0;

uint8_t getSdType(void)
{
    return s_sd_type;
}

uint32_t getSdSectorCount(void)
{
    uint32_t csd[4];
    if (sdioSendCommand(CMD9, s_sd_rca << 16U, SD_RESP_LONG) != SD_OK)
    {
        return 0;
    }
    csd[0] = SDIO->RESP1;
    csd[1] = SDIO->RESP2;
    csd[2] = SDIO->RESP3;
    csd[3] = SDIO->RESP4;
    if (((csd[0] >> 30U) & 0x3U) == 1U)
    {
        uint32_t c_size = ((csd[1] & 0x3FU) << 16U) | ((csd[2] >> 16U) & 0xFFFFU);
        return (c_size + 1U) * 1024U;
    }
    return 0x100000U;
}

uint8_t sdInit(void)
{
    sdioInit();

    /* CMD0: GO_IDLE_STATE — matches HAL SD_PowerON */
    if (sdioSendCommand(CMD0, 0U, SD_RESP_NONE) != SD_OK)
    {
        return SD_ERROR;
    }
    delay(2U);

    /* CMD8: SEND_IF_COND — matches HAL */
    if (sdioSendCommand(CMD8, 0x1AA, SD_RESP_SHORT) == SD_OK)
    {
        if ((SDIO->RESP1 & 0xFF) == 0xAA)
        {
            s_sd_type = SD_CARD_SDHC;
        }
        else
        {
            return SD_ERROR;
        }
    }
    else
    {
        s_sd_type = SD_CARD_SDSC;
        /* HAL pattern: resend CMD0 on CMD8 failure (V1.X cards) */
        sdioSendCommand(CMD0, 0U, SD_RESP_NONE);
        delay(2U);
    }

    /* CMD55: APP_CMD before ACMD41 loop — matches HAL line 2802 */
    sdioSendCommand(CMD55, 0U, SD_RESP_SHORT);

    if (sdioSendRobustAcmd41() != SD_OK)
    {
        return SD_ERROR;
    }
    if (sdioSendCommand(CMD2, 0U, SD_RESP_LONG) != SD_OK)
    {
        return SD_ERROR;
    }
    if (sdioSendCommand(CMD3, 0U, SD_RESP_SHORT) != SD_OK)
    {
        return SD_ERROR;
    }

    s_sd_rca = (SDIO->RESP1 >> 16U) & 0xFFFF;
    if (sdioSendCommand(CMD7, s_sd_rca << 16U, SD_RESP_SHORT) != SD_OK)
    {
        return SD_ERROR;
    }
    if (sdioSendCommand(CMD16, 512U, SD_RESP_SHORT) != SD_OK)
    {
        return SD_ERROR;
    }

    sdSwitchTo4bitMode(s_sd_rca << 16U);

    // Raise clock from 400 kHz init speed to 12 MHz transfer speed
    sdioRaiseClock();

    /* Wait for card to be fully ready after init (HAL pattern) */
    sdWaitCardReady();

    return SD_OK;
}

/*
 * HAL pattern: DCTRL=0 → ConfigData (DTIMER,DLEN,DCTRL armed) → CMD → unified loop
 * This is identical for both read and write — only DTDIR differs.
 */

uint8_t sdReadSingleBlock(uint32_t block_addr, uint8_t *buffer)
{
    uint32_t status;

    SDIO->ICR = 0x5FF;
    SDIO->DCTRL = 0U;

    uint32_t addr = block_addr;
    if (s_sd_type != SD_CARD_SDHC)
    {
        addr *= 512U;
    }

    /* ConfigData — arm DPSM before command */
    SDIO->DTIMER = 0xFFFFFFFF;
    SDIO->DLEN = 512U;
    SDIO->DCTRL = SDIO_DCTRL_DTEN | SDIO_DCTRL_DTDIR | (9U << SDIO_DCTRL_DBLOCKSIZE_Pos);

    /* CMD17 */
    if (sdioSendCommand(CMD17, addr, SD_RESP_SHORT) != SD_OK)
    {
        SDIO->ICR = 0x5FF;
        return SD_ERROR;
    }

    /* Unified polling loop — matches HAL_SD_ReadBlocks */
    uint8_t *bp = buffer;
    uint32_t remaining = 512U;
    uint32_t deadline = getSysTime() + 3000U;
    uint32_t word;

    while (!(SDIO->STA & (SDIO_STA_RXOVERR | SDIO_STA_DCRCFAIL |
                          SDIO_STA_DTIMEOUT | SDIO_STA_DATAEND)))
    {
        if ((SDIO->STA & SDIO_STA_RXFIFOHF) && (remaining > 0U))
        {
            for (uint32_t j = 0; j < 8U && remaining > 0U; j++)
            {
                word = SDIO->FIFO;
                *bp++ = (uint8_t)(word);        remaining--;
                *bp++ = (uint8_t)(word >> 8U);  remaining--;
                *bp++ = (uint8_t)(word >> 16U); remaining--;
                *bp++ = (uint8_t)(word >> 24U); remaining--;
            }
        }

        if (getSysTime() > deadline)
        {
            SDIO->ICR = 0x5FF;
            return SD_TIMEOUT;
        }
    }

    status = SDIO->STA;
    SDIO->ICR = 0x5FF;

    if (status & SDIO_STA_DTIMEOUT)
    {
        return SD_TIMEOUT;
    }
    if (status & SDIO_STA_DCRCFAIL)
    {
        return SD_ERROR;
    }
    if (status & SDIO_STA_RXOVERR)
    {
        return SD_ERROR;
    }
    return SD_OK;
}

uint8_t sdReadMultipleBlocks(uint32_t block_addr, uint8_t *buffer, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++)
    {
        if (sdReadSingleBlock(block_addr + i, buffer + (i * 512U)) != SD_OK)
        {
            return SD_ERROR;
        }
    }
    return SD_OK;
}

uint8_t sdWriteSingleBlock(uint32_t block_addr, const uint8_t *buffer)
{
    uint32_t status;

    SDIO->ICR = 0x5FF;
    SDIO->DCTRL = 0U;

    uint32_t addr = block_addr;
    if (s_sd_type != SD_CARD_SDHC)
    {
        addr *= 512U;
    }

    /* ConfigData — arm DPSM BEFORE command (HAL pattern) */
    SDIO->DTIMER = 0xFFFFFFFF;
    SDIO->DLEN = 512U;
    SDIO->DCTRL = SDIO_DCTRL_DTEN | (9U << SDIO_DCTRL_DBLOCKSIZE_Pos);

    /* CMD24 */
    if (sdioSendCommand(CMD24, addr, SD_RESP_SHORT) != SD_OK)
    {
        SDIO->ICR = 0x5FF;
        return SD_ERROR;
    }

    /* Unified polling loop — matches HAL_SD_WriteBlocks exactly */
    const uint8_t *bp = buffer;
    uint32_t remaining = 512U;
    uint32_t deadline = getSysTime() + 3000U;
    uint32_t word;

    while (!(SDIO->STA & (SDIO_STA_TXUNDERR | SDIO_STA_DCRCFAIL |
                          SDIO_STA_DTIMEOUT | SDIO_STA_DATAEND)))
    {
        if ((SDIO->STA & SDIO_STA_TXFIFOHE) && (remaining > 0U))
        {
            for (uint32_t j = 0; j < 8U && remaining > 0U; j++)
            {
                word  = (uint32_t)(*bp++); remaining--;
                word |= (uint32_t)(*bp++) << 8U;  remaining--;
                word |= (uint32_t)(*bp++) << 16U; remaining--;
                word |= (uint32_t)(*bp++) << 24U; remaining--;
                SDIO->FIFO = word;
            }
        }

        if (getSysTime() > deadline)
        {
            SDIO->ICR = 0x5FF;
            return SD_TIMEOUT;
        }
    }

    status = SDIO->STA;
    SDIO->ICR = 0x5FF;

    if (status & SDIO_STA_DTIMEOUT)
    {
        return SD_TIMEOUT;
    }
    if (status & SDIO_STA_DCRCFAIL)
    {
        return SD_ERROR;
    }
    if (status & SDIO_STA_TXUNDERR)
    {
        return SD_ERROR;
    }
    return SD_OK;
}

uint8_t sdWriteMultipleBlocks(uint32_t block_addr, const uint8_t *buffer, uint32_t count)
{
    uint32_t status;

    SDIO->ICR = 0x5FF;
    SDIO->DCTRL = 0U;

    uint32_t addr = block_addr;
    if (s_sd_type != SD_CARD_SDHC)
    {
        addr *= 512U;
    }

    SDIO->DTIMER = 0xFFFFFFFF;
    SDIO->DLEN = count * 512U;
    SDIO->DCTRL = SDIO_DCTRL_DTEN | (9U << SDIO_DCTRL_DBLOCKSIZE_Pos);

    if (sdioSendCommand(CMD25, addr, SD_RESP_SHORT) != SD_OK)
    {
        SDIO->ICR = 0x5FF;
        return SD_ERROR;
    }

    const uint8_t *bp = buffer;
    uint32_t remaining = count * 512U;
    uint32_t deadline = getSysTime() + 5000U;
    uint32_t word;
    uint8_t timed_out = 0U;

    while (!(SDIO->STA & (SDIO_STA_TXUNDERR | SDIO_STA_DCRCFAIL |
                          SDIO_STA_DTIMEOUT | SDIO_STA_DATAEND)))
    {
        if ((SDIO->STA & SDIO_STA_TXFIFOHE) && (remaining > 0U))
        {
            for (uint32_t j = 0; j < 8U && remaining > 0U; j++)
            {
                word  = (uint32_t)(*bp++); remaining--;
                word |= (uint32_t)(*bp++) << 8U;  remaining--;
                word |= (uint32_t)(*bp++) << 16U; remaining--;
                word |= (uint32_t)(*bp++) << 24U; remaining--;
                SDIO->FIFO = word;
            }
        }

        if (getSysTime() > deadline)
        {
            timed_out = 1U;
            break;
        }
    }

    status = SDIO->STA;
    SDIO->ICR = 0x5FF;

    /* CMD25 is open-ended: ALWAYS terminate with CMD12, even on error/timeout.
     * Skipping it on the error path leaves the card stuck in the receive state
     * (CMD13 state=6) so every later read/write fails until re-init. */
    sdioSendCommand(CMD12, 0U, SD_RESP_SHORT);

    if (timed_out || (status & SDIO_STA_DTIMEOUT))
    {
        return SD_TIMEOUT;
    }
    if (status & SDIO_STA_DCRCFAIL)
    {
        return SD_ERROR;
    }
    if (status & SDIO_STA_TXUNDERR)
    {
        return SD_ERROR;
    }
    return SD_OK;
}

void sdWaitCardReady(void)
{
    uint32_t state = 0;
    for (uint32_t poll = 0; poll < 500; poll++)
    {
        if (sdioSendCommand(CMD13, s_sd_rca << 16U, SD_RESP_SHORT) == SD_OK)
        {
            state = (SDIO->RESP1 >> 9U) & 0xFU;
            if (state == 4U)
            {
                return;
            }
        }
        delay(1);
    }
    LOGGER_LOG_WARN(LOGGER_SDIO, "sdWaitCardReady timeout, state=%lu", state);
    /* Wedged mid-transfer (5=data, 6=rcv) — e.g. an aborted multi-block write.
     * Issue CMD12 to abort it and return the card to the transfer state, so it
     * recovers in place instead of staying locked until a board reboot. */
    if (state == 5U || state == 6U)
    {
        sdioSendCommand(CMD12, 0U, SD_RESP_SHORT);
        delay(1);
    }
}
