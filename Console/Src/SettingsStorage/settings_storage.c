#include "settings_storage.h"
#include "external_eeprom.h"
#include "assert.h"

#define SETTINGS_STORAGE_TOTAL_SIZE_BYTES 32768U
#define SETTINGS_STORAGE_TOTAL_SIZE_KB 32U

// Address Range   | Size  | Purpose
// 0x0000-0x01FF   | 512B  | System header
// 0x0200-0x03FF   | 512B  | Settings directory
// 0x0400-0x07FF   | 1024B | Console settings
// 0x0800-0x7FFF   | 30KB  | Settings data region
#define MAX_DATA_SIZE 1014U

#define SETTINGS_STORAGE_EEPROM_ADDR_START_SYS_HEADER 0U
#define SETTINGS_STORAGE_EEPROM_ADDR_START_GAME_DIRECTORY 0x0200
#define SETTINGS_STORAGE_EEPROM_ADDR_START_CONSOLE_SETTINGS 0x03FF
#define SETTINGS_STORAGE_EEPROM_ADDR_START_GAME_DATA 0x0800

#define SETTINGS_STORAGE_DATA_BLOCKS 30U
#define SETTINGS_STORAGE_BLOCK_SIZE 1024U

#define SETTINGS_STORAGE_REGION_SIZE_SYS_HEADER 512U
#define SETTINGS_STORAGE_REGION_SIZE_SETTINGS_DIRECTORY 512U
#define SETTINGS_STORAGE_REGION_SIZE_CONSOLE_SETTINGS 1024U
#define SETTINGS_STORAGE_REGION_SIZE_SETTINGS_DATA (SETTINGS_STORAGE_BLOCK_SIZE * SETTINGS_STORAGE_DATA_BLOCKS)

static_assert(
    (SETTINGS_STORAGE_REGION_SIZE_SYS_HEADER +
     SETTINGS_STORAGE_REGION_SIZE_CONSOLE_SETTINGS +
     SETTINGS_STORAGE_REGION_SIZE_SETTINGS_DIRECTORY +
     SETTINGS_STORAGE_REGION_SIZE_SETTINGS_DATA) == SETTINGS_STORAGE_TOTAL_SIZE_BYTES,
    "EEPROM region sizes must sum to exactly 32KB");

typedef enum
{
    SETTINGS_DIRECTORY_ATTRIBUTE_DELETED = 0U,
    SETTINGS_DIRECTORY_ATTRIBUTE_ACTIVE = 1U,
} SettingsDirectoryAttribute;

typedef struct
{
    uint16_t version;         // structure version
    uint16_t number_settings; // saved data entitites
    uint16_t crc16;           // integrity check
} __attribute__((packed)) SystemHeader;

static_assert(sizeof(SystemHeader) <= SETTINGS_STORAGE_REGION_SIZE_SYS_HEADER, "SystemHeader size is not <= SETTINGS_STORAGE_REGION_SIZE_SYS_HEADER");

typedef struct
{
    uint8_t directory_id[SETTINGS_STORAGE_ID_MAX_SIZE];
    uint8_t attributes; // SettingsDirectoryAttribute ENUM (deleted-active)
    uint16_t crc16;     // directory entry validation
} __attribute__((packed)) SettingsDirectoryEntry;

static_assert(sizeof(SettingsDirectoryEntry) * SETTINGS_STORAGE_DATA_BLOCKS <= SETTINGS_STORAGE_REGION_SIZE_SETTINGS_DIRECTORY, "SettingsDirectoryEntry * SETTINGS_STORAGE_DATA_BLOCKS  size is not <= SETTINGS_STORAGE_REGION_SIZE_GAME_DIRECTORY");

typedef struct
{
    uint16_t version;            // data structure version
    uint16_t data_size;          // data size
    uint8_t data[MAX_DATA_SIZE]; // data
    uint16_t crc16;              // integrity check
} __attribute__((packed)) SettingsEntity;

static_assert(sizeof(SettingsEntity) * SETTINGS_STORAGE_DATA_BLOCKS <= SETTINGS_STORAGE_REGION_SIZE_SETTINGS_DATA, "SettingsEntity * SETTINGS_STORAGE_DATA_BLOCKS  size is not <= SETTINGS_STORAGE_REGION_SIZE_SETTINGS_DATA");
static_assert(sizeof(SettingsEntity) <= SETTINGS_STORAGE_REGION_SIZE_CONSOLE_SETTINGS, "SettingsEntity  size is not <= SETTINGS_STORAGE_REGION_SIZE_CONSOLE_SETTINGS");

SettingsStorageStatus settingsStorageInit(void)
{
    return SETTINGS_STORAGE_OK;
}

SettingsStorageStatus settingsStorageClear(void)
{
    if (externalEepromClear())
    {
        return SETTINGS_STORAGE_EEPROM_ERROR;
    }
    return SETTINGS_STORAGE_OK;
}

SettingsStorageStatus settingsStorageGameWrite(const uint8_t *id_name, const uint16_t struct_version, const uint8_t *const data, uint16_t size)
{
    return SETTINGS_STORAGE_OK;
}

SettingsStorageStatus settingsStorageGameRead(const uint8_t *id_name, const uint16_t expected_struct_version, uint8_t *const data, uint16_t *size)
{
    return SETTINGS_STORAGE_OK;
}

SettingsStorageStatus settingsStorageGameDelete(const uint8_t *const id_name)
{
    return SETTINGS_STORAGE_OK;
}

SettingsStorageStatus settingsStorageConsoleSettingsWrite(const uint16_t struct_version, const uint8_t *const data, const uint16_t size)
{
    return SETTINGS_STORAGE_OK;
}

SettingsStorageStatus settingsStorageConsoleSettingsRead(const uint16_t expected_struct_version, uint8_t *const data, uint16_t *const size)
{
    return SETTINGS_STORAGE_OK;
}

SettingsStorageStatus settingsStorageConsoleSettingsDelete()
{
    return SETTINGS_STORAGE_OK;
}
