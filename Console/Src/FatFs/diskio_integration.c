#include "diskio_integration.h"
#include "sdio.h"
#include "sysclock.h"
#include "logger.h"
#include <stdbool.h>

/* SDIO register/protocol magic, named. */
#define SDIO_ICR_CLEAR_ALL 0x5FFU        /* write-1-to-clear every static STA flag */
#define SDIO_DTIMER_MAX    0xFFFFFFFFU   /* maximum data timeout */
#define SD_FIFO_BURST      8U            /* words moved per FIFO half-full/empty service */
#define SD_READ_TIMEOUT_MS        3000U
#define SD_WRITE_TIMEOUT_MS       3000U
#define SD_WRITE_MULTI_TIMEOUT_MS 5000U

/* R1 card-state (bits 12:9 of the card status word). */
#define SD_CARD_STATE_SHIFT 9U
#define SD_CARD_STATE_MASK  0xFU
#define SD_STATE_TRANSFER   4U /* ready for a new transfer */
#define SD_STATE_DATA       5U /* sending data */
#define SD_STATE_RECEIVE    6U /* receiving data */
#define SD_READY_POLL_MAX   500U

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
    return SD_DEFAULT_SECTORS_SDSC;
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

    // Raise clock from 400 kHz init speed to 2 MHz transfer speed
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

    SDIO->ICR = SDIO_ICR_CLEAR_ALL;
    SDIO->DCTRL = 0U;

    uint32_t addr = block_addr;
    if (s_sd_type != SD_CARD_SDHC)
    {
        addr *= SD_BLOCK_SIZE;
    }

    /* ConfigData — arm DPSM before command */
    SDIO->DTIMER = SDIO_DTIMER_MAX;
    SDIO->DLEN = SD_BLOCK_SIZE;
    SDIO->DCTRL = SDIO_DCTRL_DTEN | SDIO_DCTRL_DTDIR | (SD_BLOCK_SIZE_LOG2 << SDIO_DCTRL_DBLOCKSIZE_Pos);

    /* CMD17 */
    if (sdioSendCommand(CMD17, addr, SD_RESP_SHORT) != SD_OK)
    {
        SDIO->ICR = SDIO_ICR_CLEAR_ALL;
        return SD_ERROR;
    }

    /* Unified polling loop — matches HAL_SD_ReadBlocks */
    uint8_t *bp = buffer;
    uint32_t remaining = SD_BLOCK_SIZE;
    uint32_t deadline = getSysTime() + SD_READ_TIMEOUT_MS;
    uint32_t word;

    while (!(SDIO->STA & (SDIO_STA_RXOVERR | SDIO_STA_DCRCFAIL |
                          SDIO_STA_DTIMEOUT | SDIO_STA_DATAEND)))
    {
        if ((SDIO->STA & SDIO_STA_RXFIFOHF) && (remaining > 0U))
        {
            for (uint32_t j = 0; j < SD_FIFO_BURST && remaining > 0U; j++)
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
            SDIO->ICR = SDIO_ICR_CLEAR_ALL;
            return SD_TIMEOUT;
        }
    }

    status = SDIO->STA;
    SDIO->ICR = SDIO_ICR_CLEAR_ALL;

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
        if (sdReadSingleBlock(block_addr + i, buffer + (i * SD_BLOCK_SIZE)) != SD_OK)
        {
            return SD_ERROR;
        }
    }
    return SD_OK;
}

uint8_t sdWriteSingleBlock(uint32_t block_addr, const uint8_t *buffer)
{
    uint32_t status;

    SDIO->ICR = SDIO_ICR_CLEAR_ALL;
    SDIO->DCTRL = 0U;

    uint32_t addr = block_addr;
    if (s_sd_type != SD_CARD_SDHC)
    {
        addr *= SD_BLOCK_SIZE;
    }

    /* ConfigData — arm DPSM BEFORE command (HAL pattern) */
    SDIO->DTIMER = SDIO_DTIMER_MAX;
    SDIO->DLEN = SD_BLOCK_SIZE;
    SDIO->DCTRL = SDIO_DCTRL_DTEN | (SD_BLOCK_SIZE_LOG2 << SDIO_DCTRL_DBLOCKSIZE_Pos);

    /* CMD24 */
    if (sdioSendCommand(CMD24, addr, SD_RESP_SHORT) != SD_OK)
    {
        SDIO->ICR = SDIO_ICR_CLEAR_ALL;
        return SD_ERROR;
    }

    /* Unified polling loop — matches HAL_SD_WriteBlocks exactly */
    const uint8_t *bp = buffer;
    uint32_t remaining = SD_BLOCK_SIZE;
    uint32_t deadline = getSysTime() + SD_WRITE_TIMEOUT_MS;
    uint32_t word;

    while (!(SDIO->STA & (SDIO_STA_TXUNDERR | SDIO_STA_DCRCFAIL |
                          SDIO_STA_DTIMEOUT | SDIO_STA_DATAEND)))
    {
        if ((SDIO->STA & SDIO_STA_TXFIFOHE) && (remaining > 0U))
        {
            for (uint32_t j = 0; j < SD_FIFO_BURST && remaining > 0U; j++)
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
            SDIO->ICR = SDIO_ICR_CLEAR_ALL;
            return SD_TIMEOUT;
        }
    }

    status = SDIO->STA;
    SDIO->ICR = SDIO_ICR_CLEAR_ALL;

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

    SDIO->ICR = SDIO_ICR_CLEAR_ALL;
    SDIO->DCTRL = 0U;

    uint32_t addr = block_addr;
    if (s_sd_type != SD_CARD_SDHC)
    {
        addr *= SD_BLOCK_SIZE;
    }

    SDIO->DTIMER = SDIO_DTIMER_MAX;
    SDIO->DLEN = count * SD_BLOCK_SIZE;
    SDIO->DCTRL = SDIO_DCTRL_DTEN | (SD_BLOCK_SIZE_LOG2 << SDIO_DCTRL_DBLOCKSIZE_Pos);

    if (sdioSendCommand(CMD25, addr, SD_RESP_SHORT) != SD_OK)
    {
        SDIO->ICR = SDIO_ICR_CLEAR_ALL;
        return SD_ERROR;
    }

    const uint8_t *bp = buffer;
    uint32_t remaining = count * SD_BLOCK_SIZE;
    uint32_t deadline = getSysTime() + SD_WRITE_MULTI_TIMEOUT_MS;
    uint32_t word;
    uint8_t timed_out = 0U;

    while (!(SDIO->STA & (SDIO_STA_TXUNDERR | SDIO_STA_DCRCFAIL |
                          SDIO_STA_DTIMEOUT | SDIO_STA_DATAEND)))
    {
        if ((SDIO->STA & SDIO_STA_TXFIFOHE) && (remaining > 0U))
        {
            for (uint32_t j = 0; j < SD_FIFO_BURST && remaining > 0U; j++)
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
    SDIO->ICR = SDIO_ICR_CLEAR_ALL;

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
    for (uint32_t poll = 0; poll < SD_READY_POLL_MAX; poll++)
    {
        if (sdioSendCommand(CMD13, s_sd_rca << 16U, SD_RESP_SHORT) == SD_OK)
        {
            state = (SDIO->RESP1 >> SD_CARD_STATE_SHIFT) & SD_CARD_STATE_MASK;
            if (state == SD_STATE_TRANSFER)
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
    if (state == SD_STATE_DATA || state == SD_STATE_RECEIVE)
    {
        sdioSendCommand(CMD12, 0U, SD_RESP_SHORT);
        delay(1);
    }
}
