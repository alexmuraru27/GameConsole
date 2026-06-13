#ifndef __SDIO_H
#define __SDIO_H
#include <stdint.h>

/*
 * SD Card Commands — Physical Layer Spec v3.01
 *
 * Initialization sequence: CMD0 → CMD8 → ACMD41 → CMD2 → CMD3 → CMD7 → CMD16 → ACMD6
 *
 * Card state machine: Idle(0) → Ready(1) → Ident(2) → Stby(3) → Trans(4) → Data(5/6) →
 *                      Recv(6) → Prog(7) → Trans(4)
 */

/* --- Init & Identification --- */
#define CMD0   0   // GO_IDLE_STATE: reset card to Idle state, enter SPI mode if CS low
#define CMD2   2   // ALL_SEND_CID: card sends 128-bit CID (unique serial number). State: Ready→Ident
#define CMD3   3   // SEND_RELATIVE_ADDR: card publishes 16-bit RCA. State: Ident→Stby
#define CMD8   8   // SEND_IF_COND: check voltage range + SD v2.0. Arg=0x1AA, card echoes pattern
#define CMD9   9   // SEND_CSD: card sends 128-bit CSD (capacity, speed, features)
#define CMD41 41   // SD_SEND_OP_COND: ACMD — init OCR, HCS, voltage window. State: Ready→Ready, then→Stby

/* --- Card Selection & Configuration --- */
#define CMD7   7   // SELECT/DESELECT_CARD: RCA<<16 selects one card. State: Stby→Trans (or Trans→Stby if RCA=0)
#define CMD16 16   // SET_BLOCKLEN: set data block size for read/write. 512 for SDHC/SDXC
#define CMD6   6   // SWITCH_FUNC: check/switch card modes (high-speed, bus width, voltages)
#define ACMD6  6   // SET_BUS_WIDTH: ACMD — arg 0=1-bit, 2=4-bit. Must be in Trans state

/* --- Status --- */
#define CMD13 13   // SEND_STATUS: returns 32-bit card status (R1). State bits at [12:9].
                   //   State 0=Idle, 1=Ready, 2=Ident, 3=Stby, 4=Trans, 5=Data, 6=Recv, 7=Prog

/* --- Data Transfer — Read --- */
#define CMD17 17   // READ_SINGLE_BLOCK: read one 512-byte block. State: Trans→Data→Trans
#define CMD18 18   // READ_MULTIPLE_BLOCK: read N blocks continuously. Stop with CMD12
#define CMD12 12   // STOP_TRANSMISSION: abort multi-block read/write. Returns card to Trans state

/* --- Data Transfer — Write --- */
#define CMD24 24   // WRITE_BLOCK: write one 512-byte block. State: Trans→Recv→Prog→Trans.
                   //   Card asserts busy (DAT0 low) while programming NAND (≤250ms SDSC, ≤500ms SDHC)
#define CMD25 25   // WRITE_MULTIPLE_BLOCK: write N blocks. Pre-erase with ACMD23, stop with CMD12
#define ACMD23 23  // SET_WR_BLK_ERASE_COUNT: ACMD — pre-erase N blocks before CMD25

/* --- Application Command Prefix --- */
#define CMD55 55   // APP_CMD: next command is ACMD (e.g. CMD55+rca → ACMD6 sets bus width)

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
void sdioRaiseClock(void);
uint8_t sdioSendCommand(uint8_t cmd, uint32_t arg, uint8_t resp_type);
uint8_t sdioSendRobustAcmd41(void);
uint8_t sdSwitchTo4bitMode(uint32_t rca);
#endif /* __SDIO_H */