#include "stm32f4xx.h"
#include <string.h>
#include <stdint.h>
#include "sysclock.h"
#include "usart.h"
#include "sdio.h"

// SD Card Commands
#define CMD0 0   // GO_IDLE_STATE
#define CMD2 2   // ALL_SEND_CID
#define CMD3 3   // SEND_RELATIVE_ADDR
#define CMD6 6   // SWITCH_FUNC
#define CMD7 7   // SELECT_CARD
#define CMD8 8   // SEND_IF_COND
#define CMD9 9   // SEND_CSD
#define CMD12 12 // STOP_TRANSMISSION
#define CMD16 16 // SET_BLOCKLEN
#define CMD17 17 // READ_SINGLE_BLOCK
#define CMD18 18 // READ_MULTIPLE_BLOCK
#define CMD41 41 // SD_SEND_OP_COND (ACMD)
#define CMD55 55 // APP_CMD
#define ACMD6 6  // SET_BUS_WIDTH (ACMD)

// SD Card Response Types
#define SD_RESP_NONE 0
#define SD_RESP_SHORT 1
#define SD_RESP_LONG 2

// SD Card Types
#define SD_CARD_SDSC 0 // Standard Capacity
#define SD_CARD_SDHC 1 // High Capacity

// FAT32 Constants
#define SECTOR_SIZE 512
#define FAT32_SIGNATURE 0xAA55

// Error Codes
#define SD_OK 0
#define SD_ERROR 1
#define SD_TIMEOUT 2
#define SD_UNSUPPORTED 3

// SDIO Bus Width
#define SDIO_BUS_1BIT 0
#define SDIO_BUS_4BIT 1

// Constants
#define MAX_FILES_IN_DIR 50 // Reduced from 100 to save memory
#define LFN_ATTR 0x0F
#define ATTR_DIRECTORY 0x10
#define ATTR_VOLUME_ID 0x08

// Static global variables
static uint32_t s_sd_rca = 0;               // Relative Card Address
static uint8_t s_sd_type = 0;               // Card Type
static uint8_t s_bus_width = SDIO_BUS_1BIT; // Current bus width

// FAT32 Structure
typedef struct Fat32Info
{
    uint32_t fat_begin_lba;
    uint32_t cluster_begin_lba;
    uint32_t sectors_per_cluster;
    uint32_t root_dir_first_cluster;
    uint32_t bytes_per_sector;
} Fat32Info;

static Fat32Info s_fat32;

// FAT32 Directory Entry
// must be packed due to casting directly over the sector
typedef struct Fat32DirEntry
{
    char name[11];
    uint8_t attr;
    uint8_t reserved;
    uint8_t create_time_tenth;
    uint16_t create_time;
    uint16_t create_date;
    uint16_t last_access_date;
    uint16_t first_cluster_high;
    uint16_t write_time;
    uint16_t write_date;
    uint16_t first_cluster_low;
    uint32_t file_size;
} __attribute__((packed)) Fat32DirEntry;

// Function prototypes
static uint8_t sdioSendCommand(uint8_t cmd, uint32_t arg, uint8_t resp_type);
static uint8_t sdInit(void);
static uint8_t sdReadSingleBlock(uint32_t block_addr, uint8_t *buffer);
static uint8_t fat32Init(void);
static uint32_t fat32GetFatEntry(uint32_t cluster);
static uint8_t fat32ReadFile(const char *filename, uint8_t *buffer, uint32_t max_size, uint32_t *bytes_read);
static uint8_t sdSwitchTo4bitMode(void);

// New file system interface prototypes
static uint8_t buildClusterChain(uint32_t first_cluster, uint32_t **cluster_chain, uint32_t *cluster_count);
static void convertFatNameToString(char *fat_name, char *output_name);
static uint8_t sdSendRobustAcmd41(void);

// Initialize SDIO peripheral with conservative settings for Black Board
static void sdioInitHW(void)
{
    debugString("Initializing SDIO peripheral for Black Board...\r\n");

    // Complete SDIO reset sequence
    SDIO->POWER = 0x00; // Power off
    delay(50);          // Wait for power stabilization

    // Power on sequence with proper timing
    SDIO->POWER = SDIO_POWER_PWRCTRL_1; // Power cycle on
    delay(10);
    SDIO->POWER = SDIO_POWER_PWRCTRL_1 | SDIO_POWER_PWRCTRL_0; // Power on + enable outputs
    delay(50);                                                 // Critical delay for Black Board power stabilization

    // Clear all flags
    SDIO->ICR = 0x5FF;

    // Configure clock control register for conservative 400kHz initialization
    // Using ClockDiv = 4 for better reliability on Black Board (48MHz / (4 + 2) = 8MHz)
    // This is slower than spec but more reliable
    SDIO->CLKCR = 4 | SDIO_CLKCR_CLKEN;
    delay(10);

    // Configure timeouts
    SDIO->DTIMER = 0xFFFFFFFF; // Maximum data timeout

    debugString("SDIO peripheral initialized with conservative settings\r\n");
}

// Send SDIO command with improved error handling for Black Board
static uint8_t sdioSendCommand(uint8_t cmd, uint32_t arg, uint8_t resp_type)
{
    uint32_t timeout = 2000000; // Increased timeout for Black Board
    uint32_t cmd_reg = 0;
    uint32_t status;

    // Wait for command path to be ready with timeout
    timeout = 1000000;
    while ((SDIO->STA & SDIO_STA_CMDACT) && timeout--)
    {
        __NOP();
    }
    if (timeout == 0)
    {
        debugString("Command path busy timeout\r\n");
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
    timeout = 2000000;

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
            debugString("Command timeout - CMD");
            debugHex(cmd);
            debugString("\r\n");
            return SD_TIMEOUT;
        }

        // Handle CRC failures - ignore for specific commands
        if (status & SDIO_STA_CCRCFAIL)
        {
            if (cmd != CMD2 && cmd != CMD3 && cmd != CMD41)
            {
                debugString("Command CRC fail - CMD");
                debugHex(cmd);
                debugString("\r\n");
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
static uint8_t sdSendRobustAcmd41(void)
{
    uint32_t response;
    uint8_t ret;
    uint32_t timeout_ms = 3000; // 3 second timeout for Black Board
    uint32_t start_time = getSysTime();
    uint32_t attempt = 0;

    debugString("Starting robust ACMD41 sequence for Black Board...\r\n");

    while ((getSysTime() - start_time) < timeout_ms)
    {
        attempt++;

        // Send CMD55: Application command
        ret = sdioSendCommand(CMD55, 0, SD_RESP_SHORT);
        if (ret != SD_OK)
        {
            debugString("CMD55 failed, retrying...\r\n");
            delay(50); // Longer delay between retries
            continue;
        }

        // Check CMD55 response - should have APP_CMD bit set
        response = SDIO->RESP1;
        if (!(response & (1 << 5)))
        {
            debugString("CMD55 response invalid - APP_CMD not set\r\n");
            delay(50);
            continue;
        }

        // Send ACMD41: SD_SEND_OP_COND with proper voltage range and HCS bit
        // Use full voltage range (0x00FF8000) + HCS bit (0x40000000) + busy bit check
        uint32_t acmd41_arg = 0x40FF8000; // HCS + voltage range 2.7-3.6V

        ret = sdioSendCommand(CMD41, acmd41_arg, SD_RESP_SHORT);
        if (ret != SD_OK)
        {
            debugString("ACMD41 failed, retrying...\r\n");
            delay(50);
            continue;
        }

        // Clear any CRC error flags (normal for ACMD41)
        SDIO->ICR = SDIO_STA_CCRCFAIL;

        // Get response
        response = SDIO->RESP1;

        // Debug response every 50 attempts
        if (attempt % 50 == 0)
        {
            debugString("ACMD41 attempt ");
            debugHex(attempt);
            debugString(" response: 0x");
            debugHex(response);
            debugString(" - ");
            if (response & 0x80000000)
            {
                debugString("Ready!");
            }
            else
            {
                debugString("Busy...");
            }
            debugString("\r\n");
        }

        // Check if card is ready (bit 31 set)
        if (response & 0x80000000)
        {
            debugString("ACMD41 completed successfully after ");
            debugHex(attempt);
            debugString(" attempts\r\n");

            // Check card capacity
            if (response & 0x40000000)
            {
                debugString("SDHC/SDXC card detected\r\n");
                s_sd_type = SD_CARD_SDHC;
            }
            else
            {
                debugString("SDSC card detected\r\n");
                s_sd_type = SD_CARD_SDSC;
            }

            return SD_OK;
        }

        // Wait before next attempt - important for Black Board
        delay(25); // 25ms delay between attempts
    }

    debugString("ACMD41 timeout after ");
    debugHex(attempt);
    debugString(" attempts\r\n");
    return SD_TIMEOUT;
}

// Switch to 4-bit bus mode
static uint8_t sdSwitchTo4bitMode(void)
{
    uint8_t ret;

    debugString("Switching to 4-bit bus mode...\r\n");

    // Send CMD55 + ACMD6 to set 4-bit mode
    ret = sdioSendCommand(CMD55, s_sd_rca << 16, SD_RESP_SHORT);
    if (ret != SD_OK)
    {
        debugString("CMD55 for bus width failed\r\n");
        return ret;
    }

    // ACMD6: SET_BUS_WIDTH (argument: 2 = 4-bit mode)
    ret = sdioSendCommand(ACMD6, 2, SD_RESP_SHORT);
    if (ret != SD_OK)
    {
        debugString("ACMD6 (set bus width) failed\r\n");
        return ret;
    }

    // Update SDIO controller for 4-bit mode
    SDIO->CLKCR |= SDIO_CLKCR_WIDBUS_0; // Set 4-bit mode
    s_bus_width = SDIO_BUS_4BIT;

    debugString("4-bit bus mode enabled\r\n");
    return SD_OK;
}

// Initialize SD card with Black Board specific fixes
static uint8_t sdInit(void)
{
    uint32_t response[4];
    uint8_t ret;

    debugString("Starting SD card initialization for Black Board...\r\n");

    // Initialize hardware
    sdioInitHW();

    // Extended delay for card stabilization (critical for Black Board)
    delay(300);

    // Send multiple CMD0s for better reliability
    debugString("Sending CMD0 (GO_IDLE_STATE) sequence...\r\n");
    for (int i = 0; i < 3; i++)
    {
        ret = sdioSendCommand(CMD0, 0, SD_RESP_NONE);
        delay(50);
    }

    // CMD8: Check voltage range and card version
    debugString("Sending CMD8 (SEND_IF_COND)...\r\n");
    ret = sdioSendCommand(CMD8, 0x1AA, SD_RESP_SHORT);
    if (ret == SD_OK)
    {
        response[0] = SDIO->RESP1;
        debugString("CMD8 response: 0x");
        debugHex(response[0]);
        debugString("\r\n");

        if ((response[0] & 0xFF) == 0xAA)
        {
            debugString("SD v2.0 or later detected\r\n");
            s_sd_type = SD_CARD_SDHC;
        }
        else
        {
            debugString("Invalid CMD8 response\r\n");
            return SD_ERROR;
        }
    }
    else
    {
        debugString("CMD8 failed - SD v1.x card detected\r\n");
        s_sd_type = SD_CARD_SDSC;
    }

    // Robust ACMD41 sequence
    ret = sdSendRobustAcmd41();
    if (ret != SD_OK)
    {
        debugString("ACMD41 sequence failed\r\n");
        return ret;
    }

    // CMD2: Get CID
    debugString("Getting CID with CMD2...\r\n");
    ret = sdioSendCommand(CMD2, 0, SD_RESP_LONG);
    if (ret != SD_OK)
    {
        debugString("CMD2 failed\r\n");
        return ret;
    }

    // CMD3: Get RCA
    debugString("Getting RCA with CMD3...\r\n");
    ret = sdioSendCommand(CMD3, 0, SD_RESP_SHORT);
    if (ret != SD_OK)
    {
        debugString("CMD3 failed\r\n");
        return ret;
    }

    s_sd_rca = (SDIO->RESP1 >> 16) & 0xFFFF;
    debugString("RCA: 0x");
    debugHex(s_sd_rca);
    debugString("\r\n");

    // CMD7: Select card
    debugString("Selecting card with CMD7...\r\n");
    ret = sdioSendCommand(CMD7, s_sd_rca << 16, SD_RESP_SHORT);
    if (ret != SD_OK)
    {
        debugString("CMD7 failed\r\n");
        return ret;
    }

    // Set block size to 512 bytes
    debugString("Setting block size with CMD16...\r\n");
    ret = sdioSendCommand(CMD16, 512, SD_RESP_SHORT);
    if (ret != SD_OK)
    {
        debugString("CMD16 failed\r\n");
        return ret;
    }

    // Try to switch to 4-bit mode (fallback to 1-bit if it fails)
    if (sdSwitchTo4bitMode() != SD_OK)
    {
        debugString("4-bit mode failed, staying in 1-bit mode\r\n");
        s_bus_width = SDIO_BUS_1BIT;
    }

    // Switch to higher speed clock after successful initialization
    debugString("Switching to higher speed clock...\r\n");
    SDIO->CLKCR = (SDIO->CLKCR & ~0xFF) | 2; // Divide by 4 = 12MHz (conservative for Black Board)
    delay(10);

    debugString("SD card initialization completed successfully\r\n");
    debugString("Bus width: ");
    debugString(s_bus_width == SDIO_BUS_4BIT ? "4-bit" : "1-bit");
    debugString("\r\n");

    return SD_OK;
}

// Read single block from SD card with improved error handling
static uint8_t sdReadSingleBlock(uint32_t block_addr, uint8_t *buffer)
{
    uint32_t timeout = 3000000; // Increased timeout for Black Board
    uint8_t ret;
    uint32_t *data_ptr = (uint32_t *)buffer;
    uint32_t status;
    uint32_t words_read = 0;

    // Clear all flags
    SDIO->ICR = 0x5FF;

    // Configure data path
    SDIO->DTIMER = 0xFFFFFFFF;
    SDIO->DLEN = 512;
    SDIO->DCTRL = SDIO_DCTRL_DTEN | SDIO_DCTRL_DTDIR | (9 << 4); // Block size 2^9 = 512

    // Send CMD17
    ret = sdioSendCommand(CMD17, block_addr, SD_RESP_SHORT);
    if (ret != SD_OK)
    {
        debugString("CMD17 failed\r\n");
        return ret;
    }

    // Read data with improved FIFO handling
    while (words_read < 128 && timeout--)
    { // 128 words = 512 bytes
        status = SDIO->STA;

        // Check for errors
        if (status & (SDIO_STA_RXOVERR | SDIO_STA_DCRCFAIL | SDIO_STA_DTIMEOUT))
        {
            if (status & SDIO_STA_DTIMEOUT)
            {
                debugString("Data timeout\r\n");
            }
            if (status & SDIO_STA_DCRCFAIL)
            {
                debugString("Data CRC fail\r\n");
            }
            if (status & SDIO_STA_RXOVERR)
            {
                debugString("RX overrun\r\n");
            }
            return SD_ERROR;
        }

        // Check if data is available
        if (status & SDIO_STA_RXFIFOHF)
        {
            // Read 8 words (32 bytes) from FIFO
            for (int i = 0; i < 8 && words_read < 128; i++)
            {
                *data_ptr++ = SDIO->FIFO;
                words_read++;
            }
        }
        else if (status & SDIO_STA_RXDAVL)
        {
            // Read available data
            *data_ptr++ = SDIO->FIFO;
            words_read++;
        }

        // Check if transfer is complete
        if (status & SDIO_STA_DATAEND)
        {
            break;
        }
    }

    // Read any remaining data
    while (!(SDIO->STA & SDIO_STA_RXFIFOE) && words_read < 128)
    {
        *data_ptr++ = SDIO->FIFO;
        words_read++;
    }

    // Clear flags
    SDIO->ICR = 0x5FF;

    if (timeout == 0)
    {
        debugString("Read timeout\r\n");
        return SD_TIMEOUT;
    }

    return SD_OK;
}

// Initialize FAT32 filesystem
static uint8_t fat32Init(void)
{
    uint8_t buffer[512];
    uint8_t ret;

    debugString("Initializing FAT32 filesystem...\r\n");

    // Read MBR (sector 0)
    ret = sdReadSingleBlock(0, buffer);
    if (ret != SD_OK)
    {
        debugString("Failed to read MBR\r\n");
        return ret;
    }

    // Check MBR signature
    if (*(uint16_t *)(buffer + 510) != FAT32_SIGNATURE)
    {
        debugString("Invalid MBR signature\r\n");
        return SD_ERROR;
    }

    // Get first partition offset
    uint32_t partition_offset = *(uint32_t *)(buffer + 454); // First partition LBA
    debugString("First partition at sector: ");
    debugHex(partition_offset);
    debugString("\r\n");

    // Read Boot Sector
    ret = sdReadSingleBlock(partition_offset, buffer);
    if (ret != SD_OK)
    {
        debugString("Failed to read boot sector\r\n");
        return ret;
    }

    // Parse FAT32 Boot Sector
    s_fat32.bytes_per_sector = *(uint16_t *)(buffer + 11);
    s_fat32.sectors_per_cluster = buffer[13];
    uint16_t reserved_sectors = *(uint16_t *)(buffer + 14);
    uint8_t num_fats = buffer[16];
    uint32_t sectors_per_fat = *(uint32_t *)(buffer + 36);
    s_fat32.root_dir_first_cluster = *(uint32_t *)(buffer + 44);

    // Calculate important addresses
    s_fat32.fat_begin_lba = partition_offset + reserved_sectors;
    s_fat32.cluster_begin_lba = s_fat32.fat_begin_lba + (num_fats * sectors_per_fat);

    debugString("FAT32 filesystem initialized\r\n");
    debugString("Bytes per sector: ");
    debugHex(s_fat32.bytes_per_sector);
    debugString("\r\n");
    debugString("Sectors per cluster: ");
    debugHex(s_fat32.sectors_per_cluster);
    debugString("\r\n");
    debugString("Root directory cluster: ");
    debugHex(s_fat32.root_dir_first_cluster);
    debugString("\r\n");

    return SD_OK;
}

// Get FAT entry for a cluster
static uint32_t fat32GetFatEntry(uint32_t cluster)
{
    uint8_t buffer[512];
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = s_fat32.fat_begin_lba + (fat_offset / 512);
    uint32_t entry_offset = fat_offset % 512;

    if (sdReadSingleBlock(fat_sector, buffer) != SD_OK)
    {
        return 0x0FFFFFFF; // End of chain marker
    }

    return *(uint32_t *)(buffer + entry_offset) & 0x0FFFFFFF;
}

// Read file from FAT32 filesystem
static uint8_t fat32ReadFile(const char *filename, uint8_t *buffer, uint32_t max_size, uint32_t *bytes_read)
{
    uint8_t sector_buffer[512];
    Fat32DirEntry *entry;
    uint32_t current_cluster;
    uint32_t file_size;
    uint32_t bytes_to_read;
    uint32_t bytes_copied = 0;
    uint8_t ret;

    debugString("Searching for file: ");
    debugString(filename);
    debugString("\r\n");

    // Search in root directory
    current_cluster = s_fat32.root_dir_first_cluster;

    while (current_cluster < 0x0FFFFFF8)
    {
        // Read cluster
        for (uint32_t sector = 0; sector < s_fat32.sectors_per_cluster; sector++)
        {
            uint32_t sector_addr = s_fat32.cluster_begin_lba + ((current_cluster - 2) * s_fat32.sectors_per_cluster) + sector;

            ret = sdReadSingleBlock(sector_addr, sector_buffer);
            if (ret != SD_OK)
            {
                debugString("Failed to read directory sector\r\n");
                return ret;
            }

            // Check each directory entry
            for (int i = 0; i < 16; i++)
            { // 16 entries per sector
                entry = (Fat32DirEntry *)(sector_buffer + (i * 32));

                // Skip deleted or empty entries
                if (entry->name[0] == 0x00 || entry->name[0] == 0xE5)
                    continue;

                // Skip long filename entries and directories
                if (entry->attr & 0x0F || entry->attr & 0x10)
                    continue;

                // Compare filename (8.3 format)
                char entry_name[12];
                memcpy(entry_name, entry->name, 11);
                entry_name[11] = '\0';

                // Convert to null-terminated string
                for (int j = 10; j >= 0; j--)
                {
                    if (entry_name[j] == ' ')
                        entry_name[j] = '\0';
                    else
                        break;
                }

                // Simple filename comparison (you may want to improve this)
                if (strstr(entry_name, filename) != NULL)
                {
                    debugString("File found: ");
                    debugString(entry_name);
                    debugString("\r\n");

                    // Get file info
                    current_cluster = ((uint32_t)entry->first_cluster_high << 16) | entry->first_cluster_low;
                    file_size = entry->file_size;

                    debugString("File size: ");
                    debugHex(file_size);
                    debugString("\r\n");
                    debugString("Starting cluster: ");
                    debugHex(current_cluster);
                    debugString("\r\n");

                    // Read file data
                    bytes_to_read = (file_size < max_size) ? file_size : max_size;

                    while (current_cluster < 0x0FFFFFF8 && bytes_copied < bytes_to_read)
                    {
                        // Read cluster data
                        for (uint32_t s = 0; s < s_fat32.sectors_per_cluster && bytes_copied < bytes_to_read; s++)
                        {
                            uint32_t data_sector = s_fat32.cluster_begin_lba + ((current_cluster - 2) * s_fat32.sectors_per_cluster) + s;

                            ret = sdReadSingleBlock(data_sector, sector_buffer);
                            if (ret != SD_OK)
                            {
                                debugString("Failed to read file data\r\n");
                                return ret;
                            }

                            // Copy data to buffer
                            uint32_t copy_size = (bytes_to_read - bytes_copied < 512) ? (bytes_to_read - bytes_copied) : 512;
                            memcpy(buffer + bytes_copied, sector_buffer, copy_size);
                            bytes_copied += copy_size;
                        }

                        // Get next cluster
                        current_cluster = fat32GetFatEntry(current_cluster);
                    }

                    *bytes_read = bytes_copied;
                    debugString("File read successfully, bytes read: ");
                    debugHex(bytes_copied);
                    debugString("\r\n");
                    return SD_OK;
                }
            }
        }

        // Get next cluster in directory chain
        current_cluster = fat32GetFatEntry(current_cluster);
    }

    debugString("File not found\r\n");
    return SD_ERROR;
}

// Build cluster chain for a file
static uint8_t buildClusterChain(uint32_t first_cluster, uint32_t **cluster_chain, uint32_t *cluster_count)
{
    uint32_t current_cluster = first_cluster;
    uint32_t count = 0;
    uint32_t *chain = NULL;
    uint32_t max_clusters = 500; // Reduced from 1000 to save memory

    // First pass: count clusters
    while (current_cluster < 0x0FFFFFF8 && count < max_clusters)
    {
        count++;
        current_cluster = fat32GetFatEntry(current_cluster);
    }

    if (count == 0)
    {
        *cluster_chain = NULL;
        *cluster_count = 0;
        return SD_ERROR;
    }

    // Allocate memory for cluster chain (simple static allocation)
    static uint32_t s_static_cluster_chain[500]; // Reduced from 1000
    chain = s_static_cluster_chain;

    // Second pass: build chain
    current_cluster = first_cluster;
    for (uint32_t i = 0; i < count; i++)
    {
        chain[i] = current_cluster;
        current_cluster = fat32GetFatEntry(current_cluster);
    }

    *cluster_chain = chain;
    *cluster_count = count;
    return SD_OK;
}

// Convert FAT 8.3 name to readable format (simplified)
static void convertFatNameToString(char *fat_name, char *output_name)
{
    int i, j = 0;

    // Copy name part (first 8 characters)
    for (i = 0; i < 8 && fat_name[i] != ' '; i++)
    {
        output_name[j++] = fat_name[i];
    }

    // Add extension if present
    if (fat_name[8] != ' ')
    {
        output_name[j++] = '.';
        for (i = 8; i < 11 && fat_name[i] != ' '; i++)
        {
            output_name[j++] = fat_name[i];
        }
    }

    output_name[j] = '\0';
}

// List all files in root directory (8.3 names only)
uint8_t sdListDirectory(Directory *dir)
{
    uint8_t sector_buffer[512];
    Fat32DirEntry *entry;
    uint32_t current_cluster;
    uint8_t ret;

    // Static allocation for file list (reduced size)
    static FileInfo s_file_list[MAX_FILES_IN_DIR];

    dir->files = s_file_list;
    dir->file_count = 0;
    dir->max_files = MAX_FILES_IN_DIR;

    debugString("Listing root directory contents (8.3 names only)...\r\n");

    current_cluster = s_fat32.root_dir_first_cluster;

    while (current_cluster < 0x0FFFFFF8 && dir->file_count < MAX_FILES_IN_DIR)
    {
        // Read cluster
        for (uint32_t sector = 0; sector < s_fat32.sectors_per_cluster; sector++)
        {
            uint32_t sector_addr = s_fat32.cluster_begin_lba + ((current_cluster - 2) * s_fat32.sectors_per_cluster) + sector;

            ret = sdReadSingleBlock(sector_addr, sector_buffer);
            if (ret != SD_OK)
            {
                debugString("Failed to read directory sector\r\n");
                return ret;
            }

            // Process directory entries
            for (int i = 0; i < 16; i++)
            { // 16 entries per sector
                entry = (Fat32DirEntry *)(sector_buffer + (i * 32));

                // Skip empty entries
                if (entry->name[0] == 0x00)
                    break; // End of directory
                if (entry->name[0] == 0xE5)
                    continue; // Deleted entry

                // Skip Long Filename entries (we don't process them)
                if (entry->attr == LFN_ATTR)
                {
                    continue;
                }

                // Skip volume ID and directories
                if (entry->attr & (ATTR_VOLUME_ID | ATTR_DIRECTORY))
                {
                    continue;
                }

                // This is a regular file entry
                if (dir->file_count < MAX_FILES_IN_DIR)
                {
                    FileInfo *file = &s_file_list[dir->file_count];

                    // Convert 8.3 name to readable format
                    convertFatNameToString(entry->name, file->name);

                    // Fill in file information
                    file->file_size = entry->file_size;
                    file->first_cluster = ((uint32_t)entry->first_cluster_high << 16) | entry->first_cluster_low;
                    file->attributes = entry->attr;
                    file->create_date = entry->create_date;
                    file->create_time = entry->create_time;
                    file->modify_date = entry->write_date;
                    file->modify_time = entry->write_time;

                    dir->file_count++;
                }
            }
        }

        // Get next cluster in directory chain
        current_cluster = fat32GetFatEntry(current_cluster);
    }

    debugString("Directory listing completed. Found ");
    debugHex(dir->file_count);
    debugString(" files\r\n");

    return SD_OK;
}

// Open a file and prepare for reading (8.3 names only)
uint8_t sdOpenFile(const char *filename, FileHandle *handle)
{
    // First, find the file in directory
    Directory dir;
    uint8_t ret = sdListDirectory(&dir);
    if (ret != SD_OK)
    {
        return ret;
    }

    // Search for the file
    FileInfo *file_info = NULL;
    for (uint32_t i = 0; i < dir.file_count; i++)
    {
        if (strcmp(filename, dir.files[i].name) == 0)
        {
            file_info = &dir.files[i];
            break;
        }
    }

    if (file_info == NULL)
    {
        debugString("File not found: ");
        debugString(filename);
        debugString("\r\n");
        return SD_ERROR;
    }

    // Build cluster chain
    ret = buildClusterChain(file_info->first_cluster, &handle->cluster_chain, &handle->cluster_count);
    if (ret != SD_OK)
    {
        debugString("Failed to build cluster chain\r\n");
        return ret;
    }

    // Initialize handle
    handle->file_size = file_info->file_size;
    handle->current_pos = 0;
    handle->current_cluster_idx = 0;
    handle->current_cluster_pos = 0;
    handle->is_open = 1;

    debugString("File opened: ");
    debugString(file_info->name);
    debugString(" (size: ");
    debugHex(file_info->file_size);
    debugString(" bytes, clusters: ");
    debugHex(handle->cluster_count);
    debugString(")\r\n");

    return SD_OK;
}

// Read data from file at specific offset
uint8_t sdReadFileAtOffset(FileHandle *handle, uint32_t offset, uint8_t *buffer, uint32_t size, uint32_t *bytes_read)
{
    if (!handle->is_open)
    {
        debugString("File not open\r\n");
        return SD_ERROR;
    }

    if (offset >= handle->file_size)
    {
        *bytes_read = 0;
        return SD_OK; // EOF
    }

    // Calculate cluster and position within cluster
    uint32_t bytes_per_cluster = s_fat32.sectors_per_cluster * 512;
    uint32_t target_cluster_idx = offset / bytes_per_cluster;
    uint32_t pos_in_cluster = offset % bytes_per_cluster;

    if (target_cluster_idx >= handle->cluster_count)
    {
        *bytes_read = 0;
        return SD_OK; // EOF
    }

    uint32_t bytes_to_read = size;
    if (offset + size > handle->file_size)
    {
        bytes_to_read = handle->file_size - offset;
    }

    uint32_t total_bytes_read = 0;
    uint8_t sector_buffer[512];

    // Read data
    while (bytes_to_read > 0 && target_cluster_idx < handle->cluster_count)
    {
        uint32_t cluster = handle->cluster_chain[target_cluster_idx];
        uint32_t sector_in_cluster = pos_in_cluster / 512;
        uint32_t pos_in_sector = pos_in_cluster % 512;

        // Read sector
        uint32_t sector_addr = s_fat32.cluster_begin_lba + ((cluster - 2) * s_fat32.sectors_per_cluster) + sector_in_cluster;
        uint8_t ret = sdReadSingleBlock(sector_addr, sector_buffer);
        if (ret != SD_OK)
        {
            debugString("Failed to read data sector\r\n");
            return ret;
        }

        // Copy data from sector
        uint32_t bytes_in_sector = 512 - pos_in_sector;
        if (bytes_in_sector > bytes_to_read)
        {
            bytes_in_sector = bytes_to_read;
        }

        memcpy(buffer + total_bytes_read, sector_buffer + pos_in_sector, bytes_in_sector);
        total_bytes_read += bytes_in_sector;
        bytes_to_read -= bytes_in_sector;

        // Move to next position
        pos_in_cluster += bytes_in_sector;
        if (pos_in_cluster >= bytes_per_cluster)
        {
            target_cluster_idx++;
            pos_in_cluster = 0;
        }
        pos_in_sector = 0; // After first sector, always start from beginning
    }

    *bytes_read = total_bytes_read;
    return SD_OK;
}

// Close file handle
uint8_t sdCloseFile(FileHandle *handle)
{
    if (handle->is_open)
    {
        handle->is_open = 0;
        handle->cluster_chain = NULL;
        handle->cluster_count = 0;
        handle->file_size = 0;
        handle->current_pos = 0;
        handle->current_cluster_idx = 0;
        handle->current_cluster_pos = 0;
        debugString("File closed\r\n");
    }
    return SD_OK;
}

uint8_t sdCardInit(void)
{
    uint8_t ret;

    debugString("=== STM32F407VET6 Black Board SD Card Driver ===\r\n");

    ret = sdInit();
    if (ret != SD_OK)
    {
        debugString("SD card initialization failed with error: ");
        debugHex(ret);
        debugString("\r\n");
        return ret;
    }

    ret = fat32Init();
    if (ret != SD_OK)
    {
        debugString("FAT32 initialization failed with error: ");
        debugHex(ret);
        debugString("\r\n");
        return ret;
    }

    debugString("SD card and FAT32 initialization completed successfully\r\n");

    return ret;
}

uint8_t sdReadFile(const char *filename, uint8_t *buffer, uint32_t max_size, uint32_t *bytes_read)
{
    return fat32ReadFile(filename, buffer, max_size, bytes_read);
}

// Example usage function with simplified interface (8.3 names only)
void sdCardExample(void)
{
    uint8_t file_buffer[2048];
    uint32_t bytes_read;
    uint8_t ret;

    // Initialize SD card and filesystem
    ret = sdCardInit();
    if (ret != SD_OK)
    {
        debugString("SD card initialization failed!\r\n");
        return;
    }

    // 1. List all files in root directory
    Directory directory;
    ret = sdListDirectory(&directory);
    if (ret == SD_OK)
    {
        debugString("Files found:\r\n");
        for (uint32_t i = 0; i < directory.file_count; i++)
        {
            FileInfo *file = &directory.files[i];
            debugString("  ");
            debugHex(i + 1);
            debugString(". ");
            debugString(file->name);
            debugString(" - Size: ");
            debugHex(file->file_size);
            debugString(" bytes\r\n");
        }
    }
    else
    {
        debugString("Failed to list directory\r\n");
    }

    // 2. Open and read a file with offsets
    if (directory.file_count > 0)
    {
        FileInfo *first_file = &directory.files[1];

        debugString("\r\n--- File Reading with Offsets ---\r\n");
        debugString("Opening file: ");
        debugString(first_file->name);
        debugString("\r\n");

        FileHandle file_handle;
        ret = sdOpenFile(first_file->name, &file_handle);
        if (ret == SD_OK)
        {
            // Read first 100 bytes
            debugString("Reading first 100 bytes:\r\n");
            ret = sdReadFileAtOffset(&file_handle, 0, file_buffer, 100, &bytes_read);
            if (ret == SD_OK)
            {
                file_buffer[bytes_read] = '\0';
                debugString("Data: ");
                debugString((char *)file_buffer);
                debugString("\r\n");
            }

            // Read from middle of file
            if (first_file->file_size > 200)
            {
                debugString("Reading 50 bytes from offset 100:\r\n");
                ret = sdReadFileAtOffset(&file_handle, 100, file_buffer, 50, &bytes_read);
                if (ret == SD_OK)
                {
                    file_buffer[bytes_read] = '\0';
                    debugString("Data: ");
                    debugString((char *)file_buffer);
                    debugString("\r\n");
                }
            }

            // Read from near end of file
            if (first_file->file_size > 100)
            {
                uint32_t offset = first_file->file_size - 50;
                debugString("Reading last 50 bytes from offset ");
                debugHex(offset);
                debugString(":\r\n");
                ret = sdReadFileAtOffset(&file_handle, offset, file_buffer, 50, &bytes_read);
                if (ret == SD_OK)
                {
                    file_buffer[bytes_read] = '\0';
                    debugString("Data: ");
                    debugString((char *)file_buffer);
                    debugString("\r\n");
                }
            }

            sdCloseFile(&file_handle);
        }
        else
        {
            debugString("Failed to open file\r\n");
        }
    }

    // 3. Test original simple read function
    debugString("\r\n--- Legacy File Reading ---\r\n");
    debugString("Attempting to read TEST.TXT...\r\n");
    ret = sdReadFile("TEST.TXT", file_buffer, sizeof(file_buffer) - 1, &bytes_read);
    if (ret == SD_OK)
    {
        debugString("File read successful!\r\n");
        debugString("File content (");
        debugHex(bytes_read);
        debugString(" bytes):\r\n");
        file_buffer[bytes_read] = '\0';
        debugString((char *)file_buffer);
        debugString("\r\n");
    }
    else
    {
        debugString("TEST.TXT not found\r\n");
    }

    debugString("\r\nAdvanced example completed.\r\n");
}
