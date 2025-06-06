#include "diskio_integration.h"
#include "sdio.h"
#include "sysclock.h"
#include "usart.h"

#define SECTOR_SIZE 512
#define FAT32_SIGNATURE 0xAA55

static uint32_t s_sd_rca = 0; // Relative Card Address
static uint8_t s_sd_type = 0; // Card Type

uint8_t getSdType()
{
    return s_sd_type;
}

uint8_t sdInit(void)
{
    uint32_t response[4];
    uint8_t ret;
    sdioInit();

    // Extended delay for card stabilization (critical for Black Board)
    delay(300U);

    // multiple CMD0s for better reliability
    for (int i = 0U; i < 3U; i++)
    {
        ret = sdioSendCommand(CMD0, 0U, SD_RESP_NONE);
        delay(50U);
    }

    // CMD8: voltage range and card version
    ret = sdioSendCommand(CMD8, 0x1AA, SD_RESP_SHORT);
    if (ret == SD_OK)
    {
        response[0] = SDIO->RESP1;
        if ((response[0] & 0xFF) == 0xAA)
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
    }

    // ACMD41
    ret = sdioSendRobustAcmd41();
    if (ret != SD_OK)
    {
        return ret;
    }

    // CMD2: CID
    ret = sdioSendCommand(CMD2, 0U, SD_RESP_LONG);
    if (ret != SD_OK)
    {
        return ret;
    }

    // CMD3: RCA
    ret = sdioSendCommand(CMD3, 0U, SD_RESP_SHORT);
    if (ret != SD_OK)
    {
        return ret;
    }

    s_sd_rca = (SDIO->RESP1 >> 16U) & 0xFFFF;

    // CMD7: Select card
    ret = sdioSendCommand(CMD7, s_sd_rca << 16U, SD_RESP_SHORT);
    if (ret != SD_OK)
    {
        return ret;
    }

    // Set block size to 512 bytes
    ret = sdioSendCommand(CMD16, 512U, SD_RESP_SHORT);
    if (ret != SD_OK)
    {
        return ret;
    }

    sdSwitchTo4bitMode(s_sd_rca << 16U);

    // Switch to higher speed clock after successful initialization
    // Divide by 4 = 12MHz (conservative for Black Board)
    SDIO->CLKCR = (SDIO->CLKCR & ~0xFF) | 2U;
    delay(10U);
    return SD_OK;
}

uint8_t sdReadSingleBlock(uint32_t block_addr, uint8_t *buffer)
{
    uint32_t timeout = 3000000U; // Increased timeout for Black Board
    uint8_t ret;
    uint32_t *data_ptr = (uint32_t *)buffer;
    uint32_t status;
    uint32_t words_read = 0;

    // Clear all flags
    SDIO->ICR = 0x5FF;

    // Configure data path
    SDIO->DTIMER = 0xFFFFFFFF;
    SDIO->DLEN = 512U;
    SDIO->DCTRL = SDIO_DCTRL_DTEN | SDIO_DCTRL_DTDIR | (9U << SDIO_DCTRL_DBLOCKSIZE_Pos); // Block size 2^9 = 512

    // Send CMD17
    ret = sdioSendCommand(CMD17, block_addr, SD_RESP_SHORT);
    if (ret != SD_OK)
    {
        return ret;
    }

    // Read data with improved FIFO handling
    while (words_read < 128U && timeout--)
    {
        // 128 words = 512 bytes
        status = SDIO->STA;

        if (status & (SDIO_STA_RXOVERR | SDIO_STA_DCRCFAIL | SDIO_STA_DTIMEOUT))
        {
            if (status & SDIO_STA_DTIMEOUT)
            {
                debugString("SDIO timeout\r\n");
            }
            if (status & SDIO_STA_DCRCFAIL)
            {
                debugString("SDIO CRC fail\r\n");
            }
            if (status & SDIO_STA_RXOVERR)
            {
                debugString("SDIO RX overrun\r\n");
            }
            return SD_ERROR;
        }

        // data available?
        if (status & SDIO_STA_RXFIFOHF)
        {
            // Read 8 words (32b) from FIFO
            for (int i = 0; i < 8 && words_read < 128; i++)
            {
                *data_ptr++ = SDIO->FIFO;
                words_read++;
            }
        }
        else if (status & SDIO_STA_RXDAVL)
        {
            // read available data
            *data_ptr++ = SDIO->FIFO;
            words_read++;
        }

        // check if transfer is complete
        if (status & SDIO_STA_DATAEND)
        {
            break;
        }
    }

    // any remaining data
    while (!(SDIO->STA & SDIO_STA_RXFIFOE) && words_read < 128U)
    {
        *data_ptr++ = SDIO->FIFO;
        words_read++;
    }

    // clear flags
    SDIO->ICR = 0x5FF;

    if (timeout == 0)
    {
        debugString("Read timeout\r\n");
        return SD_TIMEOUT;
    }

    return SD_OK;
}

// Read multiple blocks from SD card
uint8_t sdReadMultipleBlocks(uint32_t block_addr, uint8_t *buffer, uint32_t count)
{
    uint8_t ret = SD_OK;

    for (uint32_t i = 0U; i < count; i++)
    {
        ret = sdReadSingleBlock(block_addr + i, buffer + (i * 512U));
        if (ret != SD_OK)
        {
            return ret;
        }
    }

    return SD_OK;
}