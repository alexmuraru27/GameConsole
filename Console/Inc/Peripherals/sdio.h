#ifndef __SDIO_H
#define __SDIO_H
#include <stdint.h>

// File Information Structure (8.3 names only)
typedef struct FileInfo
{
    char name[13];          // 8.3 name (null terminated, e.g., "FILE.TXT")
    uint32_t file_size;     // File size in bytes
    uint32_t first_cluster; // Starting cluster
    uint8_t attributes;     // File attributes
    uint16_t create_date;   // Creation date
    uint16_t create_time;   // Creation time
    uint16_t modify_date;   // Last modification date
    uint16_t modify_time;   // Last modification time
} FileInfo;

// Directory Listing Structure
typedef struct Directory
{
    FileInfo *files;     // Array of file information
    uint32_t file_count; // Number of files
    uint32_t max_files;  // Maximum files that can be stored
} Directory;

// File Handle Structure
typedef struct FileHandle
{
    uint32_t *cluster_chain;      // Array of clusters for this file
    uint32_t cluster_count;       // Number of clusters in chain
    uint32_t file_size;           // Total file size
    uint32_t current_pos;         // Current read position
    uint32_t current_cluster_idx; // Current cluster index
    uint32_t current_cluster_pos; // Position within current cluster
    uint8_t is_open;              // File open flag
} FileHandle;

// I'D LIKE TO THANK TO CLAUDE AND CHAT GPT FOR MAKING THIS HELL OF SD CARD READING WORK
// insight: if you are using STM32F407VET6 Chinese Black Board you might encounter tons of issues with the sdio

uint8_t sdCardInit(void);
uint8_t sdReadFile(const char *filename, uint8_t *buffer, uint32_t max_size, uint32_t *bytes_read);
uint8_t sdListDirectory(Directory *dir);
uint8_t sdOpenFile(const char *filename, FileHandle *handle);
uint8_t sdReadFileAtOffset(FileHandle *handle, uint32_t offset, uint8_t *buffer, uint32_t size, uint32_t *bytes_read);
uint8_t sdCloseFile(FileHandle *handle);
void sdCardExample(void);
#endif /* __SDIO_H */