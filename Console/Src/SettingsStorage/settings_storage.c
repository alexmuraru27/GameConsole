#include "settings_storage.h"
#include "external_eeprom.h"
#include "assert.h"
#include "crc.h"
#include "string.h"
#include "stddef.h"

#define SETTINGS_STORAGE_MAGIC_VERSION 0x1234U

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
#define SETTINGS_STORAGE_EEPROM_ADDR_START_CONSOLE_SETTINGS 0x0400
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
    SETTINGS_DIRECTORY_ATTRIBUTE_FREE = 0U,
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

static SystemHeader s_system_header;
static bool s_initialized = false;

static SettingsStorageStatus findDirectoryEntry(const uint8_t *const id_name, SettingsDirectoryEntry *const entry, uint16_t *const entry_index)
{
    SettingsDirectoryEntry temp_entry;

    for (uint16_t i = 0U; i < SETTINGS_STORAGE_DATA_BLOCKS; i++)
    {
        const uint16_t addr = SETTINGS_STORAGE_EEPROM_ADDR_START_GAME_DIRECTORY + (i * sizeof(SettingsDirectoryEntry));

        if (externalEepromRead(addr, (uint8_t *)&temp_entry, sizeof(SettingsDirectoryEntry)) != 0U)
        {
            return SETTINGS_STORAGE_STATUS_EEPROM_ERROR;
        }

        const uint16_t calculated_crc = crc16_calculate((uint8_t *)&temp_entry, sizeof(SettingsDirectoryEntry) - sizeof(uint16_t));
        if (calculated_crc != temp_entry.crc16)
        {
            // skip corrupted entries -> there will be a cleanup function
            continue;
        }

        if (temp_entry.attributes == SETTINGS_DIRECTORY_ATTRIBUTE_ACTIVE &&
            memcmp(temp_entry.directory_id, id_name, SETTINGS_STORAGE_ID_MAX_SIZE) == 0U)
        {
            if (entry != NULL)
            {
                *entry = temp_entry;
            }
            if (entry_index != NULL)
            {
                *entry_index = i;
            }
            return SETTINGS_STORAGE_STATUS_OK;
        }
    }

    return SETTINGS_STORAGE_STATUS_NOT_FOUND;
}

static SettingsStorageStatus findFreeDirectorySlot(uint16_t *const slot_index)
{
    SettingsDirectoryEntry temp_entry;
    for (uint16_t i = 0U; i < SETTINGS_STORAGE_DATA_BLOCKS; i++)
    {
        const uint16_t addr = SETTINGS_STORAGE_EEPROM_ADDR_START_GAME_DIRECTORY + (i * sizeof(SettingsDirectoryEntry));

        if (externalEepromRead(addr, (uint8_t *)&temp_entry, sizeof(SettingsDirectoryEntry)) != 0U)
        {
            return SETTINGS_STORAGE_STATUS_EEPROM_ERROR;
        }

        if (temp_entry.attributes == SETTINGS_DIRECTORY_ATTRIBUTE_FREE)
        {
            *slot_index = i;
            return SETTINGS_STORAGE_STATUS_OK;
        }
    }

    return SETTINGS_STORAGE_STATUS_STORAGE_FULL;
}

SettingsStorageStatus sanityCleanup(void)
{
    if (!s_initialized)
    {
        return SETTINGS_STORAGE_STATUS_NOT_FOUND;
    }
    uint16_t active_count = 0U;

    for (uint16_t i = 0U; i < SETTINGS_STORAGE_DATA_BLOCKS; i++)
    {
        SettingsDirectoryEntry dir_entry;

        const uint16_t dir_addr = SETTINGS_STORAGE_EEPROM_ADDR_START_GAME_DIRECTORY + (i * sizeof(SettingsDirectoryEntry));

        if (externalEepromRead(dir_addr, (uint8_t *)&dir_entry, sizeof(SettingsDirectoryEntry)) != 0U)
        {
            return SETTINGS_STORAGE_STATUS_EEPROM_ERROR;
        }

        if (dir_entry.attributes == SETTINGS_DIRECTORY_ATTRIBUTE_FREE)
        {
            continue;
        }

        bool is_entry_corrupted = false;
        const uint16_t dir_calculated_crc = crc16_calculate((uint8_t *)&dir_entry, sizeof(SettingsDirectoryEntry) - sizeof(uint16_t));
        if (dir_calculated_crc != dir_entry.crc16)
        {
            is_entry_corrupted = true;
        }
        else
        {
            SettingsEntity data_entity;
            const uint16_t data_addr = SETTINGS_STORAGE_EEPROM_ADDR_START_GAME_DATA + (i * SETTINGS_STORAGE_BLOCK_SIZE);

            if (externalEepromRead(data_addr, (uint8_t *)&data_entity, sizeof(SettingsEntity)) != 0U)
            {
                return SETTINGS_STORAGE_STATUS_EEPROM_ERROR;
            }

            const uint16_t data_calculated_crc = crc16_calculate((uint8_t *)&data_entity, sizeof(SettingsEntity) - sizeof(uint16_t));
            if (data_calculated_crc != data_entity.crc16)
            {
                is_entry_corrupted = true;
            }
        }

        if (is_entry_corrupted)
        {
            dir_entry.attributes = SETTINGS_DIRECTORY_ATTRIBUTE_FREE;
            memset(dir_entry.directory_id, 0U, SETTINGS_STORAGE_ID_MAX_SIZE);
            dir_entry.crc16 = crc16_calculate((uint8_t *)&dir_entry, sizeof(SettingsDirectoryEntry) - sizeof(uint16_t));
            if (externalEepromWrite(dir_addr, (uint8_t *)&dir_entry, sizeof(SettingsDirectoryEntry)) != 0U)
            {
                return SETTINGS_STORAGE_STATUS_EEPROM_ERROR;
            }

            SettingsEntity empty_entity;
            memset(&empty_entity, 0U, sizeof(SettingsEntity));
            uint16_t data_addr = SETTINGS_STORAGE_EEPROM_ADDR_START_GAME_DATA + (i * SETTINGS_STORAGE_BLOCK_SIZE);
            if (externalEepromWrite(data_addr, (uint8_t *)&empty_entity, sizeof(SettingsEntity)) != 0U)
            {
                return SETTINGS_STORAGE_STATUS_EEPROM_ERROR;
            }
        }
        else
        {
            active_count++;
        }
    }

    s_system_header.version = SETTINGS_STORAGE_MAGIC_VERSION;
    s_system_header.number_settings = active_count;
    s_system_header.crc16 = crc16_calculate((uint8_t *)&s_system_header, sizeof(SystemHeader) - sizeof(uint16_t));

    if (externalEepromWrite(SETTINGS_STORAGE_EEPROM_ADDR_START_SYS_HEADER,
                            (uint8_t *)&s_system_header, sizeof(SystemHeader)) != 0U)
    {
        return SETTINGS_STORAGE_STATUS_EEPROM_ERROR;
    }

    return SETTINGS_STORAGE_STATUS_OK;
}

SettingsStorageStatus settingsStorageInit()
{
    if (externalEepromRead(SETTINGS_STORAGE_EEPROM_ADDR_START_SYS_HEADER, (uint8_t *)&s_system_header, sizeof(SystemHeader)) != 0U)
    {
        return SETTINGS_STORAGE_STATUS_EEPROM_ERROR;
    }

    const uint16_t calculated_crc = crc16_calculate((uint8_t *)&s_system_header, (sizeof(SystemHeader) - sizeof(uint16_t)));
    if ((calculated_crc != s_system_header.crc16) || (s_system_header.version != SETTINGS_STORAGE_MAGIC_VERSION))
    {
        s_system_header.version = SETTINGS_STORAGE_MAGIC_VERSION;
        s_system_header.number_settings = 0U;
        s_system_header.crc16 = crc16_calculate((uint8_t *)&s_system_header,
                                                sizeof(SystemHeader) - sizeof(uint16_t));

        if (externalEepromWrite(SETTINGS_STORAGE_EEPROM_ADDR_START_SYS_HEADER,
                                (uint8_t *)&s_system_header, sizeof(SystemHeader)) != 0U)
        {
            return SETTINGS_STORAGE_STATUS_EEPROM_ERROR;
        }
    }

    s_initialized = true;

    return sanityCleanup();
}

SettingsStorageStatus settingsStorageClear(void)
{
    if (externalEepromClear())
    {
        return SETTINGS_STORAGE_STATUS_EEPROM_ERROR;
    }
    return settingsStorageInit();
}

SettingsStorageStatus settingsStorageGameWrite(const uint8_t *id_name, const uint16_t struct_version, const uint8_t *const data, uint16_t size)
{
    if (!s_initialized)
    {
        return SETTINGS_STORAGE_STATUS_NOT_FOUND;
    }

    if (size > MAX_DATA_SIZE)
    {
        return SETTINGS_STORAGE_STATUS_STORAGE_FULL;
    }

    uint16_t existing_slot;
    SettingsStorageStatus status = findDirectoryEntry(id_name, NULL, &existing_slot);

    uint16_t slot_index;
    if (status == SETTINGS_STORAGE_STATUS_OK)
    {
        slot_index = existing_slot;
    }
    else if (status == SETTINGS_STORAGE_STATUS_NOT_FOUND)
    {
        status = findFreeDirectorySlot(&slot_index);
        if (status != SETTINGS_STORAGE_STATUS_OK)
        {
            return status;
        }

        s_system_header.number_settings++;
        s_system_header.crc16 = crc16_calculate((uint8_t *)&s_system_header, sizeof(SystemHeader) - sizeof(uint16_t));

        if (externalEepromWrite(SETTINGS_STORAGE_EEPROM_ADDR_START_SYS_HEADER,
                                (uint8_t *)&s_system_header, sizeof(SystemHeader)) != 0U)
        {
            return SETTINGS_STORAGE_STATUS_EEPROM_ERROR;
        }
    }
    else
    {
        return status;
    }

    SettingsEntity entity;
    entity.version = struct_version;
    entity.data_size = size;
    memcpy(entity.data, data, size);
    entity.crc16 = crc16_calculate((uint8_t *)&entity, sizeof(SettingsEntity) - sizeof(uint16_t));

    const uint16_t data_addr = SETTINGS_STORAGE_EEPROM_ADDR_START_GAME_DATA + (slot_index * SETTINGS_STORAGE_BLOCK_SIZE);
    if (externalEepromWrite(data_addr, (uint8_t *)&entity, sizeof(SettingsEntity)) != 0U)
    {
        return SETTINGS_STORAGE_STATUS_EEPROM_ERROR;
    }

    SettingsDirectoryEntry dir_entry;
    memcpy(dir_entry.directory_id, id_name, SETTINGS_STORAGE_ID_MAX_SIZE);
    dir_entry.attributes = SETTINGS_DIRECTORY_ATTRIBUTE_ACTIVE;
    dir_entry.crc16 = crc16_calculate((uint8_t *)&dir_entry, sizeof(SettingsDirectoryEntry) - sizeof(uint16_t));

    const uint16_t dir_addr = SETTINGS_STORAGE_EEPROM_ADDR_START_GAME_DIRECTORY + (slot_index * sizeof(SettingsDirectoryEntry));
    if (externalEepromWrite(dir_addr, (uint8_t *)&dir_entry, sizeof(SettingsDirectoryEntry)) != 0U)
    {
        return SETTINGS_STORAGE_STATUS_EEPROM_ERROR;
    }

    return SETTINGS_STORAGE_STATUS_OK;
}

SettingsStorageStatus settingsStorageGameRead(const uint8_t *id_name, const uint16_t expected_struct_version, uint8_t *const data, uint16_t *size)
{
    if (!s_initialized)
    {
        return SETTINGS_STORAGE_STATUS_NOT_FOUND;
    }

    uint16_t slot_index;
    SettingsStorageStatus status = findDirectoryEntry(id_name, NULL, &slot_index);
    if (status != SETTINGS_STORAGE_STATUS_OK)
    {
        return status;
    }

    SettingsEntity entity;
    const uint16_t data_addr = SETTINGS_STORAGE_EEPROM_ADDR_START_GAME_DATA + (slot_index * SETTINGS_STORAGE_BLOCK_SIZE);
    if (externalEepromRead(data_addr, (uint8_t *)&entity, sizeof(SettingsEntity)) != 0U)
    {
        return SETTINGS_STORAGE_STATUS_EEPROM_ERROR;
    }

    const uint16_t calculated_crc = crc16_calculate((uint8_t *)&entity, sizeof(SettingsEntity) - sizeof(uint16_t));
    if (calculated_crc != entity.crc16)
    {
        return SETTINGS_STORAGE_STATUS_CHECKSUM_MISMATCH;
    }

    if (entity.version != expected_struct_version)
    {
        return SETTINGS_STORAGE_STATUS_STRUCT_VERSION_MISMATCH;
    }

    memcpy(data, entity.data, entity.data_size);
    *size = entity.data_size;

    return SETTINGS_STORAGE_STATUS_OK;
}

SettingsStorageStatus settingsStorageGameDelete(const uint8_t *const id_name)
{
    if (!s_initialized)
    {
        return SETTINGS_STORAGE_STATUS_NOT_FOUND;
    }

    SettingsDirectoryEntry dir_entry;
    uint16_t slot_index;
    SettingsStorageStatus status = findDirectoryEntry(id_name, &dir_entry, &slot_index);
    if (status != SETTINGS_STORAGE_STATUS_OK)
    {
        return status;
    }

    dir_entry.attributes = SETTINGS_DIRECTORY_ATTRIBUTE_FREE;
    dir_entry.crc16 = crc16_calculate((uint8_t *)&dir_entry, sizeof(SettingsDirectoryEntry) - sizeof(uint16_t));

    const uint16_t dir_addr = SETTINGS_STORAGE_EEPROM_ADDR_START_GAME_DIRECTORY + (slot_index * sizeof(SettingsDirectoryEntry));
    if (externalEepromWrite(dir_addr, (uint8_t *)&dir_entry, sizeof(SettingsDirectoryEntry)) != 0U)
    {
        return SETTINGS_STORAGE_STATUS_EEPROM_ERROR;
    }

    if (s_system_header.number_settings > 0U)
    {
        s_system_header.number_settings--;
    }
    s_system_header.crc16 = crc16_calculate((uint8_t *)&s_system_header, sizeof(SystemHeader) - sizeof(uint16_t));

    if (externalEepromWrite(SETTINGS_STORAGE_EEPROM_ADDR_START_SYS_HEADER,
                            (uint8_t *)&s_system_header, sizeof(SystemHeader)) != 0U)
    {
        return SETTINGS_STORAGE_STATUS_EEPROM_ERROR;
    }

    return SETTINGS_STORAGE_STATUS_OK;
}

SettingsStorageStatus settingsStorageConsoleSettingsWrite(const uint16_t struct_version, const uint8_t *const data, const uint16_t size)
{
    if (!s_initialized)
    {
        return SETTINGS_STORAGE_STATUS_NOT_FOUND;
    }

    if (size > MAX_DATA_SIZE)
    {
        return SETTINGS_STORAGE_STATUS_STORAGE_FULL;
    }

    SettingsEntity entity;
    entity.version = struct_version;
    entity.data_size = size;
    memcpy(entity.data, data, size);
    entity.crc16 = crc16_calculate((uint8_t *)&entity, sizeof(SettingsEntity) - sizeof(uint16_t));

    if (externalEepromWrite(SETTINGS_STORAGE_EEPROM_ADDR_START_CONSOLE_SETTINGS,
                            (uint8_t *)&entity, sizeof(SettingsEntity)) != 0U)
    {
        return SETTINGS_STORAGE_STATUS_EEPROM_ERROR;
    }

    return SETTINGS_STORAGE_STATUS_OK;
}

SettingsStorageStatus settingsStorageConsoleSettingsRead(const uint16_t expected_struct_version, uint8_t *const data, uint16_t *const size)
{
    if (!s_initialized)
    {
        return SETTINGS_STORAGE_STATUS_NOT_FOUND;
    }
    SettingsEntity entity;
    if (externalEepromRead(SETTINGS_STORAGE_EEPROM_ADDR_START_CONSOLE_SETTINGS,
                           (uint8_t *)&entity, sizeof(SettingsEntity)) != 0U)
    {
        return SETTINGS_STORAGE_STATUS_EEPROM_ERROR;
    }

    uint16_t calculated_crc = crc16_calculate((uint8_t *)&entity, sizeof(SettingsEntity) - sizeof(uint16_t));
    if (calculated_crc != entity.crc16)
    {
        return SETTINGS_STORAGE_STATUS_CHECKSUM_MISMATCH;
    }

    if (entity.version != expected_struct_version)
    {
        return SETTINGS_STORAGE_STATUS_STRUCT_VERSION_MISMATCH;
    }

    memcpy(data, entity.data, entity.data_size);
    *size = entity.data_size;

    return SETTINGS_STORAGE_STATUS_OK;
}

SettingsStorageStatus settingsStorageConsoleSettingsDelete()
{
    if (!s_initialized)
    {
        return SETTINGS_STORAGE_STATUS_NOT_FOUND;
    }

    SettingsEntity entity;
    memset(&entity, 0U, sizeof(SettingsEntity));

    if (externalEepromWrite(SETTINGS_STORAGE_EEPROM_ADDR_START_CONSOLE_SETTINGS,
                            (uint8_t *)&entity, sizeof(SettingsEntity)) != 0U)
    {
        return SETTINGS_STORAGE_STATUS_EEPROM_ERROR;
    }

    return SETTINGS_STORAGE_STATUS_OK;
}
