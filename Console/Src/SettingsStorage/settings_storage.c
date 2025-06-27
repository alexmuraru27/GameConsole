#include "settings_storage.h"
#include "external_eeprom.h"

void settingsStorageInit(void)
{
}

SettingsStorageStatus settingsStorageClear(void)
{
    return SETTINGS_STORAGE_OK;
}

SettingsStorageStatus settingsStorageCreate(const uint8_t *game_name, const uint32_t game_magic_number, uint16_t data_size)
{
    return SETTINGS_STORAGE_OK;
}

SettingsStorageStatus settingsStorageWrite(const uint8_t *game_name, const uint32_t game_magic_number, const uint8_t *data, uint16_t size)
{
    return SETTINGS_STORAGE_OK;
}

SettingsStorageStatus settingsStorageRead(const uint8_t *game_name, const uint32_t game_magic_number, uint8_t *data, uint16_t *size)
{
    return SETTINGS_STORAGE_OK;
}

SettingsStorageStatus settingsStorageDelete(const uint8_t *game_name, const uint32_t game_magic_number)
{
    return SETTINGS_STORAGE_OK;
}