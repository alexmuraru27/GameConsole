#ifndef __SETTINGS_STORAGE_H
#define __SETTINGS_STORAGE_H
#include "stdbool.h"
#include "stdint.h"

#define SETTINGS_STORAGE_ID_MAX_SIZE 12U
typedef enum
{
    SETTINGS_STORAGE_OK = 0U,
    SETTINGS_STORAGE_NOT_FOUND = 1U,
    SETTINGS_STORAGE_MAGIC_NUMBER_MISMATCH = 2U,
    SETTINGS_STORAGE_CHECKSUM_MISMATCH = 3U,
    SETTINGS_STORAGE_STRUCT_VERSION_MISMATCH = 4U,
    SETTINGS_STORAGE_STORAGE_FULL = 5U,
    SETTINGS_STORAGE_EEPROM_ERROR = 6U,
} SettingsStorageStatus;

SettingsStorageStatus settingsStorageInit(void);
SettingsStorageStatus settingsStorageClear(void);

// Storage for the games
SettingsStorageStatus settingsStorageGameWrite(const uint8_t *id_name, uint16_t struct_version, const uint8_t *data, uint16_t size);
SettingsStorageStatus settingsStorageGameRead(const uint8_t *id_name, uint16_t expected_struct_version, uint8_t *data, uint16_t *size);
SettingsStorageStatus settingsStorageGameDelete(const uint8_t *id_name);

// Storage for the console
SettingsStorageStatus settingsStorageConsoleSettingsWrite(uint16_t struct_version, const uint8_t *data, uint16_t size);
SettingsStorageStatus settingsStorageConsoleSettingsRead(uint16_t expected_struct_version, uint8_t *data, uint16_t *size);
SettingsStorageStatus settingsStorageConsoleSettingsDelete();

#endif /* __SETTINGS_STORAGE_H */
