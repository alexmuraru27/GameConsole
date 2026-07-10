#include "SettingsStorage/settings_layout.h"
#include "Devices/external_eeprom.h"
#include "Crc/crc.h"
#include <assert.h>
#include <string.h>
#include <stddef.h>

/*
 * The directory / entity mechanism for the EEPROM settings store: raw byte I/O,
 * CRC, layout address math, name normalization, the game directory (entry
 * read/write/occupancy/scan/free/count), and the shared 2 KB entity codec.
 * It holds no state of its own; settings_storage.c owns the system header and
 * the init flag and is the sole client. See settings_layout.h for the map.
 */

static_assert(DIRECTORY_ENTRY_SIZE == sizeof(GameDirectoryEntry),
              "DIRECTORY_ENTRY_SIZE must match the packed struct size");
static_assert(sizeof(GameDataEntity) == GAME_SLOT_SIZE,
              "GameDataEntity must be exactly one 2 KB slot");
static_assert(sizeof(ConsoleSettingsEntity) == CONSOLE_ENTITY_SIZE,
              "ConsoleSettingsEntity must be exactly 2 KB");
static_assert(sizeof(SystemHeader) <= SYS_HEADER_REGION_SIZE,
              "SystemHeader overflows its region");
static_assert(SETTINGS_GAME_SLOTS * GAME_SLOT_SIZE + ADDR_GAMES <= EEPROM_TOTAL_SIZE,
              "game slots overflow the EEPROM");
static_assert(ADDR_CONSOLE_SETTINGS == ADDR_DIRECTORY + SETTINGS_GAME_SLOTS * DIRECTORY_ENTRY_SIZE,
              "console address must follow the directory exactly");
static_assert(ADDR_GAMES == ADDR_CONSOLE_SETTINGS + CONSOLE_ENTITY_SIZE,
              "game data must follow the console entity exactly");

/* ------------------------------------------------------------------ addressing / I/O */

static uint16_t dirEntryAddr(const uint16_t index)
{
    return (uint16_t)(ADDR_DIRECTORY + index * sizeof(GameDirectoryEntry));
}

uint16_t settingsSlotAddr(const uint16_t index)
{
    return (uint16_t)(ADDR_GAMES + index * GAME_SLOT_SIZE);
}

uint16_t settingsStructCrc(const void *const obj, const size_t size)
{
    return crc16_calculate((const uint8_t *)obj, (uint32_t)(size - sizeof(uint16_t)));
}

SettingsStorageStatus settingsEepromRead(const uint16_t addr, void *const buf, const uint16_t len)
{
    return (externalEepromRead(addr, (uint8_t *)buf, len) == 0U)
               ? SETTINGS_STORAGE_STATUS_OK
               : SETTINGS_STORAGE_STATUS_EEPROM_ERROR;
}

SettingsStorageStatus settingsEepromWrite(const uint16_t addr, const void *const buf, const uint16_t len)
{
    return (externalEepromWrite(addr, (const uint8_t *)buf, len) == 0U)
               ? SETTINGS_STORAGE_STATUS_OK
               : SETTINGS_STORAGE_STATUS_EEPROM_ERROR;
}

void settingsNormalizeName(const char *const name, char out[SETTINGS_GAME_NAME_MAX])
{
    memset(out, 0, SETTINGS_GAME_NAME_MAX);
    if (name == NULL)
    {
        return;
    }

    size_t len = strlen(name);
    const char *const dot = strrchr(name, '.');
    if (dot != NULL)
    {
        len = (size_t)(dot - name);
    }
    if (len > SETTINGS_GAME_NAME_MAX - 1U)
    {
        len = SETTINGS_GAME_NAME_MAX - 1U;
    }

    for (size_t i = 0U; i < len; i++)
    {
        char c = name[i];
        if (c >= 'A' && c <= 'Z')
        {
            c = (char)(c - 'A' + 'a');
        }
        out[i] = c;
    }
}

/* ------------------------------------------------------------------ directory */

SettingsStorageStatus settingsDirReadEntry(const uint16_t index, GameDirectoryEntry *const entry)
{
    return settingsEepromRead(dirEntryAddr(index), entry, sizeof(*entry));
}

SettingsStorageStatus settingsDirWriteEntry(const uint16_t index, GameDirectoryEntry *const entry)
{
    entry->crc16 = settingsStructCrc(entry, sizeof(*entry));
    return settingsEepromWrite(dirEntryAddr(index), entry, sizeof(*entry));
}

/* A slot is occupied only by a valid, ACTIVE directory entry. A FREE or
 * crc-broken entry is treated as empty (the broken one is reclaimed on write
 * and freed by cleanup). */
bool settingsDirEntryOccupied(const GameDirectoryEntry *const entry)
{
    return (entry->state == SLOT_STATE_ACTIVE) && (settingsStructCrc(entry, sizeof(*entry)) == entry->crc16);
}

SettingsStorageStatus settingsDirFindEntry(const char *const key, GameDirectoryEntry *const entry_out, uint16_t *const index_out)
{
    GameDirectoryEntry entry;
    for (uint16_t i = 0U; i < SETTINGS_GAME_SLOTS; i++)
    {
        const SettingsStorageStatus st = settingsDirReadEntry(i, &entry);
        if (st != SETTINGS_STORAGE_STATUS_OK)
        {
            return st;
        }
        if (settingsDirEntryOccupied(&entry) && strncmp(entry.name, key, SETTINGS_GAME_NAME_MAX) == 0)
        {
            if (entry_out != NULL)
            {
                *entry_out = entry;
            }
            if (index_out != NULL)
            {
                *index_out = i;
            }
            return SETTINGS_STORAGE_STATUS_OK;
        }
    }
    return SETTINGS_STORAGE_STATUS_NOT_FOUND;
}

SettingsStorageStatus settingsDirFindFreeSlot(uint16_t *const index_out)
{
    GameDirectoryEntry entry;
    for (uint16_t i = 0U; i < SETTINGS_GAME_SLOTS; i++)
    {
        const SettingsStorageStatus st = settingsDirReadEntry(i, &entry);
        if (st != SETTINGS_STORAGE_STATUS_OK)
        {
            return st;
        }
        if (!settingsDirEntryOccupied(&entry))
        {
            *index_out = i;
            return SETTINGS_STORAGE_STATUS_OK;
        }
    }
    return SETTINGS_STORAGE_STATUS_STORAGE_FULL;
}

SettingsStorageStatus settingsDirFreeSlot(const uint16_t index)
{
    GameDirectoryEntry entry;
    memset(&entry, 0, sizeof(entry));
    entry.state = SLOT_STATE_FREE;
    const SettingsStorageStatus st = settingsDirWriteEntry(index, &entry);
    if (st != SETTINGS_STORAGE_STATUS_OK)
    {
        return st;
    }
    if (externalEepromClearRange(settingsSlotAddr(index), GAME_SLOT_SIZE) != 0U)
    {
        return SETTINGS_STORAGE_STATUS_EEPROM_ERROR;
    }
    return SETTINGS_STORAGE_STATUS_OK;
}

uint16_t settingsDirCountGames(void)
{
    GameDirectoryEntry entry;
    uint16_t count = 0U;
    for (uint16_t i = 0U; i < SETTINGS_GAME_SLOTS; i++)
    {
        if (settingsDirReadEntry(i, &entry) == SETTINGS_STORAGE_STATUS_OK && settingsDirEntryOccupied(&entry))
        {
            count++;
        }
    }
    return count;
}

/* ------------------------------------------------------------------ entity codec */

/* The console blob (ConsoleSettingsEntity) and every game slot (GameDataEntity) are
 * the same 2 KB record — {version, data_size, data[], crc16} — so these two helpers
 * are the single encode / verify path for both, keeping the CRC + version invariant
 * in one place. The working buffer is a GameDataEntity, but its byte layout is
 * identical to ConsoleSettingsEntity (both are one slot; see the static_asserts), so
 * the bytes written/read are the same either way. `max` is the entity's data
 * capacity (SETTINGS_CONSOLE_MAX_DATA / SETTINGS_GAME_MAX_DATA — equal by layout). */
SettingsStorageStatus settingsEntityWrite(uint16_t addr, uint16_t version,
                                          const uint8_t *data, uint16_t size, uint16_t max)
{
    if (data == NULL || size > max)
    {
        return SETTINGS_STORAGE_STATUS_INVALID_ARG;
    }
    GameDataEntity entity;
    memset(&entity, 0, sizeof(entity));
    entity.version = version;
    entity.data_size = size;
    memcpy(entity.data, data, size);
    entity.crc16 = settingsStructCrc(&entity, sizeof(entity));
    return settingsEepromWrite(addr, &entity, sizeof(entity));
}

SettingsStorageStatus settingsEntityRead(uint16_t addr, uint16_t expected_version,
                                         uint8_t *buffer, uint16_t *size, uint16_t max)
{
    GameDataEntity entity;
    const SettingsStorageStatus st = settingsEepromRead(addr, &entity, sizeof(entity));
    if (st != SETTINGS_STORAGE_STATUS_OK)
    {
        return st;
    }
    if (settingsStructCrc(&entity, sizeof(entity)) != entity.crc16)
    {
        return SETTINGS_STORAGE_STATUS_CHECKSUM_MISMATCH;
    }
    if (entity.version != expected_version)
    {
        return SETTINGS_STORAGE_STATUS_VERSION_MISMATCH;
    }
    if (entity.data_size > max)
    {
        return SETTINGS_STORAGE_STATUS_CHECKSUM_MISMATCH;
    }
    if (*size < entity.data_size)
    {
        *size = entity.data_size;
        return SETTINGS_STORAGE_STATUS_BUFFER_TOO_SMALL;
    }
    memcpy(buffer, entity.data, entity.data_size);
    *size = entity.data_size;
    return SETTINGS_STORAGE_STATUS_OK;
}
