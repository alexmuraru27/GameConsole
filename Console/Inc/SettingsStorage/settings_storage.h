#ifndef __SETTINGS_STORAGE_H
#define __SETTINGS_STORAGE_H
#include "stdbool.h"
#include "stdint.h"
#include "game_console_api.h"

typedef enum
{
    SETTINGS_STORAGE_OK = 0,
    SETTINGS_STORAGE_NOT_FOUND,
    SETTINGS_STORAGE_STORAGE_FULL,
    SETTINGS_STORAGE_INVALID_DATA,
    SETTINGS_STORAGE_EEPROM_ERROR
} SettingsStorageStatus;

void settingsStorageInit(void);
SettingsStorageStatus settingsStorageClear(void);
SettingsStorageStatus settingsStorageCreate(const uint8_t *game_name, uint32_t game_magic_number, uint16_t data_size);
SettingsStorageStatus settingsStorageWrite(const uint8_t *game_name, uint32_t game_magic_number, const uint8_t *data, uint16_t size);
SettingsStorageStatus settingsStorageRead(const uint8_t *game_name, uint32_t game_magic_number, uint8_t *data, uint16_t *size);
SettingsStorageStatus settingsStorageDelete(const uint8_t *game_name, uint32_t game_magic_number);

#endif /* __SETTINGS_STORAGE_H */
