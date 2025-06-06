#ifndef __SDIO_H
#define __SDIO_H
#include <stdint.h>

// SD Card Commands
#define CMD0 0    // GO_IDLE_STATE
#define CMD2 2    // ALL_SEND_CID
#define CMD3 3    // SEND_RELATIVE_ADDR
#define CMD6 6    // SWITCH_FUNC
#define CMD7 7    // SELECT_CARD
#define CMD8 8    // SEND_IF_COND
#define CMD9 9    // SEND_CSD
#define CMD12 12  // STOP_TRANSMISSION
#define CMD16 16  // SET_BLOCKLEN
#define CMD17 17  // READ_SINGLE_BLOCK
#define CMD18 18  // READ_MULTIPLE_BLOCK
#define CMD24 24  // WRITE_BLOCK
#define CMD25 25  // WRITE_MULTIPLE_BLOCK
#define CMD41 41  // SD_SEND_OP_COND (ACMD)
#define CMD55 55  // APP_CMD
#define ACMD6 6   // SET_BUS_WIDTH (ACMD)
#define ACMD23 23 // SET_WR_BLK_ERASE_COUNT (ACMD)

// SD Card Response Types
#define SD_RESP_NONE 0U
#define SD_RESP_SHORT 1U
#define SD_RESP_LONG 2U

// Error Codes
#define SD_OK 0U
#define SD_ERROR 1U
#define SD_TIMEOUT 2U
#define SD_UNSUPPORTED 3U

void sdioInit(void);
uint8_t sdioSendCommand(uint8_t cmd, uint32_t arg, uint8_t resp_type);
uint8_t sdioSendRobustAcmd41(void);
uint8_t sdSwitchTo4bitMode(uint32_t rca);
#endif /* __SDIO_H */