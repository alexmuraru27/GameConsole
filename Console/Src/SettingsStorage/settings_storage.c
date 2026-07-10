#include "SettingsStorage/settings_storage.h"
#include "SettingsStorage/settings_layout.h"
#include "Devices/external_eeprom.h"
#include "Logger/logger.h"
#include <string.h>
#include <stddef.h>

/*
 * Public settings-storage API: the console blob, the keyed game saves, the
 * management/introspection calls, and the current-game binding. It owns the one
 * piece of mutable module state — the in-RAM system header (game count + write
 * sequence) and the init flag — and drives the directory / entity mechanism in
 * settings_directory.c. The on-EEPROM layout lives in settings_layout.h.
 */

static SystemHeader s_header;
static bool s_initialized = false;
static bool s_game_bound = false;
static char s_bound_name[SETTINGS_GAME_NAME_MAX];

/* ------------------------------------------------------------------ helpers */

static SettingsStorageStatus writeHeader(void)
{
    s_header.crc16 = settingsStructCrc(&s_header, sizeof(s_header));
    return settingsEepromWrite(ADDR_SYS_HEADER, &s_header, sizeof(s_header));
}

/* Normalize a game name into a storage key and reject an empty result. Every
 * game-keyed entry point shares this prologue. */
static SettingsStorageStatus normalizeKeyChecked(const char *const game_name, char out[SETTINGS_GAME_NAME_MAX])
{
    settingsNormalizeName(game_name, out);
    return (out[0] == '\0') ? SETTINGS_STORAGE_STATUS_INVALID_ARG : SETTINGS_STORAGE_STATUS_OK;
}

/* Free slot `index`, decrement the (clamped) game count, and persist the header.
 * The shared commit tail of game-delete and evict-oldest. */
static SettingsStorageStatus freeSlotAndCommit(const uint16_t index)
{
    const SettingsStorageStatus st = settingsDirFreeSlot(index);
    if (st != SETTINGS_STORAGE_STATUS_OK)
    {
        return st;
    }
    if (s_header.game_count > 0U)
    {
        s_header.game_count--;
    }
    return writeHeader();
}

/* ------------------------------------------------------------------ lifecycle */

SettingsStorageStatus settingsStorageCleanupCorrupted(uint16_t *const cleared_count_out)
{
    if (cleared_count_out != NULL)
    {
        *cleared_count_out = 0U;
    }
    if (!s_initialized)
    {
        return SETTINGS_STORAGE_STATUS_NOT_INITIALIZED;
    }

    uint16_t cleared = 0U;
    uint16_t active = 0U;

    for (uint16_t i = 0U; i < SETTINGS_GAME_SLOTS; i++)
    {
        GameDirectoryEntry entry;
        SettingsStorageStatus st = settingsDirReadEntry(i, &entry);
        if (st != SETTINGS_STORAGE_STATUS_OK)
        {
            return st;
        }

        /* Only ACTIVE+valid entries are occupied; everything else is free. */
        if (!settingsDirEntryOccupied(&entry))
        {
            continue;
        }

        bool corrupted = false;
        if (entry.write_seq != 0U) /* 0 == reserved, no data written yet */
        {
            GameDataEntity data;
            st = settingsEepromRead(settingsSlotAddr(i), &data, sizeof(data));
            if (st != SETTINGS_STORAGE_STATUS_OK)
            {
                return st;
            }
            if (settingsStructCrc(&data, sizeof(data)) != data.crc16 || data.data_size > SETTINGS_GAME_MAX_DATA)
            {
                corrupted = true;
            }
        }

        if (corrupted)
        {
            LOGGER_LOG_WARN(LOGGER_SETTINGS, "clearing corrupted save '%s' (slot %u)", entry.name, i);
            st = settingsDirFreeSlot(i);
            if (st != SETTINGS_STORAGE_STATUS_OK)
            {
                return st;
            }
            cleared++;
        }
        else
        {
            active++;
        }
    }

    s_header.game_count = active;
    const SettingsStorageStatus st = writeHeader();
    if (st != SETTINGS_STORAGE_STATUS_OK)
    {
        return st;
    }

    if (cleared_count_out != NULL)
    {
        *cleared_count_out = cleared;
    }
    if (cleared > 0U)
    {
        LOGGER_LOG_INFO(LOGGER_SETTINGS, "cleanup freed %u corrupted save(s)", cleared);
    }
    return SETTINGS_STORAGE_STATUS_OK;
}

SettingsStorageStatus settingsStorageInit(void)
{
    s_initialized = false;
    s_game_bound = false;
    s_bound_name[0] = '\0';

    SettingsStorageStatus st = settingsEepromRead(ADDR_SYS_HEADER, &s_header, sizeof(s_header));
    if (st != SETTINGS_STORAGE_STATUS_OK)
    {
        LOGGER_LOG_ERROR(LOGGER_SETTINGS, "header read failed");
        return st;
    }

    const bool valid = (s_header.magic_version == SETTINGS_MAGIC_VERSION) &&
                       (settingsStructCrc(&s_header, sizeof(s_header)) == s_header.crc16);
    if (!valid)
    {
        LOGGER_LOG_WARN(LOGGER_SETTINGS, "no valid header — formatting storage");
        /* Wipe the directory so no stale entry survives the reformat. */
        if (externalEepromClearRange(ADDR_DIRECTORY, SETTINGS_GAME_SLOTS * DIRECTORY_ENTRY_SIZE) != 0U)
        {
            return SETTINGS_STORAGE_STATUS_EEPROM_ERROR;
        }
        s_header.magic_version = SETTINGS_MAGIC_VERSION;
        s_header.game_count = 0U;
        s_header.write_seq = 1U; /* 0 is reserved as "no data yet" */
        st = writeHeader();
        if (st != SETTINGS_STORAGE_STATUS_OK)
        {
            return st;
        }
    }

    s_initialized = true;

    uint16_t cleared = 0U;
    st = settingsStorageCleanupCorrupted(&cleared);
    LOGGER_LOG_INFO(LOGGER_SETTINGS, "init: %u/%u games, %u cleared, seq %lu",
                    s_header.game_count, (unsigned)SETTINGS_GAME_SLOTS, cleared,
                    (unsigned long)s_header.write_seq);
    return st;
}

SettingsStorageStatus settingsStorageClear(void)
{
    LOGGER_LOG_WARN(LOGGER_SETTINGS, "clearing all settings storage");
    if (externalEepromClear() != 0U)
    {
        return SETTINGS_STORAGE_STATUS_EEPROM_ERROR;
    }
    return settingsStorageInit();
}

/* ------------------------------------------------------------------ console */

SettingsStorageStatus settingsStorageConsoleWrite(const uint16_t struct_version, const uint8_t *const data, const uint16_t size)
{
    if (!s_initialized)
    {
        return SETTINGS_STORAGE_STATUS_NOT_INITIALIZED;
    }

    const SettingsStorageStatus st =
        settingsEntityWrite(ADDR_CONSOLE_SETTINGS, struct_version, data, size, SETTINGS_CONSOLE_MAX_DATA);
    if (st == SETTINGS_STORAGE_STATUS_OK)
    {
        LOGGER_LOG_DEBUG(LOGGER_SETTINGS, "console write %u B v%u", size, struct_version);
    }
    return st;
}

SettingsStorageStatus settingsStorageConsoleRead(const uint16_t expected_struct_version, uint8_t *const buffer, uint16_t *const size)
{
    if (!s_initialized)
    {
        return SETTINGS_STORAGE_STATUS_NOT_INITIALIZED;
    }
    if (buffer == NULL || size == NULL)
    {
        return SETTINGS_STORAGE_STATUS_INVALID_ARG;
    }

    return settingsEntityRead(ADDR_CONSOLE_SETTINGS, expected_struct_version, buffer, size, SETTINGS_CONSOLE_MAX_DATA);
}

SettingsStorageStatus settingsStorageConsoleDelete(void)
{
    if (!s_initialized)
    {
        return SETTINGS_STORAGE_STATUS_NOT_INITIALIZED;
    }
    if (externalEepromClearRange(ADDR_CONSOLE_SETTINGS, sizeof(ConsoleSettingsEntity)) != 0U)
    {
        return SETTINGS_STORAGE_STATUS_EEPROM_ERROR;
    }
    LOGGER_LOG_DEBUG(LOGGER_SETTINGS, "console settings deleted");
    return SETTINGS_STORAGE_STATUS_OK;
}

/* ------------------------------------------------------------------ games */

SettingsStorageStatus settingsStorageGameEnsure(const char *const game_name)
{
    if (!s_initialized)
    {
        return SETTINGS_STORAGE_STATUS_NOT_INITIALIZED;
    }

    char key[SETTINGS_GAME_NAME_MAX];
    SettingsStorageStatus st = normalizeKeyChecked(game_name, key);
    if (st != SETTINGS_STORAGE_STATUS_OK)
    {
        return st;
    }

    if (settingsDirFindEntry(key, NULL, NULL) == SETTINGS_STORAGE_STATUS_OK)
    {
        return SETTINGS_STORAGE_STATUS_OK; /* already present */
    }

    uint16_t index;
    st = settingsDirFindFreeSlot(&index);
    if (st != SETTINGS_STORAGE_STATUS_OK)
    {
        LOGGER_LOG_WARN(LOGGER_SETTINGS, "storage full — no slot for '%s'", key);
        return st;
    }

    GameDirectoryEntry entry;
    memset(&entry, 0, sizeof(entry));
    memcpy(entry.name, key, SETTINGS_GAME_NAME_MAX);
    entry.state = SLOT_STATE_ACTIVE;
    entry.write_seq = 0U; /* reserved; no data yet */
    st = settingsDirWriteEntry(index, &entry);
    if (st != SETTINGS_STORAGE_STATUS_OK)
    {
        return st;
    }
    if (externalEepromClearRange(settingsSlotAddr(index), GAME_SLOT_SIZE) != 0U)
    {
        return SETTINGS_STORAGE_STATUS_EEPROM_ERROR;
    }

    s_header.game_count++;
    st = writeHeader();
    if (st == SETTINGS_STORAGE_STATUS_OK)
    {
        LOGGER_LOG_INFO(LOGGER_SETTINGS, "created entry '%s' (slot %u)", key, index);
    }
    return st;
}

SettingsStorageStatus settingsStorageGameWrite(const char *const game_name, const uint16_t struct_version, const uint8_t *const data, const uint16_t size)
{
    if (!s_initialized)
    {
        return SETTINGS_STORAGE_STATUS_NOT_INITIALIZED;
    }
    if (data == NULL || size > SETTINGS_GAME_MAX_DATA)
    {
        return SETTINGS_STORAGE_STATUS_INVALID_ARG;
    }

    char key[SETTINGS_GAME_NAME_MAX];
    SettingsStorageStatus st = normalizeKeyChecked(game_name, key);
    if (st != SETTINGS_STORAGE_STATUS_OK)
    {
        return st;
    }

    uint16_t index;
    bool created = false;
    st = settingsDirFindEntry(key, NULL, &index);
    if (st == SETTINGS_STORAGE_STATUS_NOT_FOUND)
    {
        st = settingsDirFindFreeSlot(&index);
        if (st != SETTINGS_STORAGE_STATUS_OK)
        {
            LOGGER_LOG_WARN(LOGGER_SETTINGS, "storage full — cannot save '%s'", key);
            return st;
        }
        created = true;
    }
    else if (st != SETTINGS_STORAGE_STATUS_OK)
    {
        return st;
    }

    const uint32_t seq = s_header.write_seq;

    st = settingsEntityWrite(settingsSlotAddr(index), struct_version, data, size, SETTINGS_GAME_MAX_DATA);
    if (st != SETTINGS_STORAGE_STATUS_OK)
    {
        return st;
    }

    GameDirectoryEntry entry;
    memset(&entry, 0, sizeof(entry));
    memcpy(entry.name, key, SETTINGS_GAME_NAME_MAX);
    entry.state = SLOT_STATE_ACTIVE;
    entry.write_seq = seq;
    st = settingsDirWriteEntry(index, &entry);
    if (st != SETTINGS_STORAGE_STATUS_OK)
    {
        return st;
    }

    s_header.write_seq = seq + 1U;
    if (created)
    {
        s_header.game_count++;
    }
    st = writeHeader();
    if (st == SETTINGS_STORAGE_STATUS_OK)
    {
        LOGGER_LOG_DEBUG(LOGGER_SETTINGS, "wrote '%s' slot %u (%u B, seq %lu)",
                         key, index, size, (unsigned long)seq);
    }
    return st;
}

SettingsStorageStatus settingsStorageGameRead(const char *const game_name, const uint16_t expected_struct_version, uint8_t *const buffer, uint16_t *const size)
{
    if (!s_initialized)
    {
        return SETTINGS_STORAGE_STATUS_NOT_INITIALIZED;
    }
    if (buffer == NULL || size == NULL)
    {
        return SETTINGS_STORAGE_STATUS_INVALID_ARG;
    }

    char key[SETTINGS_GAME_NAME_MAX];
    SettingsStorageStatus st = normalizeKeyChecked(game_name, key);
    if (st != SETTINGS_STORAGE_STATUS_OK)
    {
        return st;
    }

    GameDirectoryEntry entry;
    uint16_t index;
    st = settingsDirFindEntry(key, &entry, &index);
    if (st != SETTINGS_STORAGE_STATUS_OK)
    {
        return st; /* NOT_FOUND */
    }
    if (entry.write_seq == 0U)
    {
        return SETTINGS_STORAGE_STATUS_NOT_FOUND; /* reserved but never written */
    }

    return settingsEntityRead(settingsSlotAddr(index), expected_struct_version, buffer, size, SETTINGS_GAME_MAX_DATA);
}

SettingsStorageStatus settingsStorageGameDelete(const char *const game_name)
{
    if (!s_initialized)
    {
        return SETTINGS_STORAGE_STATUS_NOT_INITIALIZED;
    }

    char key[SETTINGS_GAME_NAME_MAX];
    SettingsStorageStatus st = normalizeKeyChecked(game_name, key);
    if (st != SETTINGS_STORAGE_STATUS_OK)
    {
        return st;
    }

    uint16_t index;
    st = settingsDirFindEntry(key, NULL, &index);
    if (st != SETTINGS_STORAGE_STATUS_OK)
    {
        return st;
    }

    st = freeSlotAndCommit(index);
    if (st == SETTINGS_STORAGE_STATUS_OK)
    {
        LOGGER_LOG_INFO(LOGGER_SETTINGS, "deleted save '%s' (slot %u)", key, index);
    }
    return st;
}

bool settingsStorageGameExists(const char *const game_name)
{
    if (!s_initialized)
    {
        return false;
    }
    char key[SETTINGS_GAME_NAME_MAX];
    if (normalizeKeyChecked(game_name, key) != SETTINGS_STORAGE_STATUS_OK)
    {
        return false;
    }
    return settingsDirFindEntry(key, NULL, NULL) == SETTINGS_STORAGE_STATUS_OK;
}

/* ------------------------------------------------------------------ management */

uint16_t settingsStorageGameCount(void)
{
    return s_initialized ? settingsDirCountGames() : 0U;
}

uint16_t settingsStorageGameCapacity(void)
{
    return SETTINGS_GAME_SLOTS;
}

SettingsStorageStatus settingsStorageGameInfoByIndex(const uint16_t slot_index, SettingsGameInfo *const info_out)
{
    if (!s_initialized)
    {
        return SETTINGS_STORAGE_STATUS_NOT_INITIALIZED;
    }
    if (info_out == NULL || slot_index >= SETTINGS_GAME_SLOTS)
    {
        return SETTINGS_STORAGE_STATUS_INVALID_ARG;
    }

    GameDirectoryEntry entry;
    SettingsStorageStatus st = settingsDirReadEntry(slot_index, &entry);
    if (st != SETTINGS_STORAGE_STATUS_OK)
    {
        return st;
    }
    if (!settingsDirEntryOccupied(&entry))
    {
        return SETTINGS_STORAGE_STATUS_NOT_FOUND;
    }

    memset(info_out, 0, sizeof(*info_out));
    memcpy(info_out->name, entry.name, SETTINGS_GAME_NAME_MAX);
    info_out->name[SETTINGS_GAME_NAME_MAX - 1U] = '\0';
    info_out->write_seq = entry.write_seq;

    if (entry.write_seq != 0U)
    {
        GameDataEntity data_entity;
        st = settingsEepromRead(settingsSlotAddr(slot_index), &data_entity, sizeof(data_entity));
        if (st != SETTINGS_STORAGE_STATUS_OK)
        {
            return st;
        }
        if (settingsStructCrc(&data_entity, sizeof(data_entity)) == data_entity.crc16)
        {
            info_out->data_size = data_entity.data_size;
            info_out->data_version = data_entity.version;
        }
    }
    return SETTINGS_STORAGE_STATUS_OK;
}

SettingsStorageStatus settingsStorageEvictOldest(SettingsGameInfo *const evicted_out)
{
    if (!s_initialized)
    {
        return SETTINGS_STORAGE_STATUS_NOT_INITIALIZED;
    }

    GameDirectoryEntry entry;
    bool found = false;
    uint16_t oldest_index = 0U;
    uint32_t oldest_seq = 0U;

    for (uint16_t i = 0U; i < SETTINGS_GAME_SLOTS; i++)
    {
        const SettingsStorageStatus st = settingsDirReadEntry(i, &entry);
        if (st != SETTINGS_STORAGE_STATUS_OK)
        {
            return st;
        }
        if (!settingsDirEntryOccupied(&entry))
        {
            continue;
        }
        if (!found || entry.write_seq < oldest_seq)
        {
            found = true;
            oldest_index = i;
            oldest_seq = entry.write_seq;
        }
    }

    if (!found)
    {
        return SETTINGS_STORAGE_STATUS_NOT_FOUND;
    }

    if (evicted_out != NULL)
    {
        (void)settingsStorageGameInfoByIndex(oldest_index, evicted_out);
    }

    char evicted_name[SETTINGS_GAME_NAME_MAX];
    (void)settingsDirReadEntry(oldest_index, &entry);
    memcpy(evicted_name, entry.name, SETTINGS_GAME_NAME_MAX);
    evicted_name[SETTINGS_GAME_NAME_MAX - 1U] = '\0';

    const SettingsStorageStatus st = freeSlotAndCommit(oldest_index);
    if (st == SETTINGS_STORAGE_STATUS_OK)
    {
        LOGGER_LOG_INFO(LOGGER_SETTINGS, "evicted oldest save '%s' (seq %lu)",
                        evicted_name, (unsigned long)oldest_seq);
    }
    return st;
}

/* ------------------------------------------------------------------ current game */

SettingsStorageStatus settingsStorageBindGame(const char *const game_name)
{
    if (!s_initialized)
    {
        return SETTINGS_STORAGE_STATUS_NOT_INITIALIZED;
    }

    settingsNormalizeName(game_name, s_bound_name);
    if (s_bound_name[0] == '\0')
    {
        s_game_bound = false;
        return SETTINGS_STORAGE_STATUS_INVALID_ARG;
    }
    s_game_bound = true;

    /* Create the entry up front so the game has a slot reserved. STORAGE_FULL is
     * surfaced (and logged) but the game still runs — its writes just fail. */
    const SettingsStorageStatus st = settingsStorageGameEnsure(s_bound_name);
    LOGGER_LOG_INFO(LOGGER_SETTINGS, "bound game '%s' (%d)", s_bound_name, (int)st);
    return st;
}

void settingsStorageUnbindGame(void)
{
    s_game_bound = false;
    s_bound_name[0] = '\0';
}

SettingsStorageStatus settingsStorageCurrentGameWrite(const uint16_t struct_version, const uint8_t *const data, const uint16_t size)
{
    if (!s_game_bound)
    {
        return SETTINGS_STORAGE_STATUS_NOT_FOUND;
    }
    return settingsStorageGameWrite(s_bound_name, struct_version, data, size);
}

SettingsStorageStatus settingsStorageCurrentGameRead(const uint16_t expected_struct_version, uint8_t *const buffer, uint16_t *const size)
{
    if (!s_game_bound)
    {
        return SETTINGS_STORAGE_STATUS_NOT_FOUND;
    }
    return settingsStorageGameRead(s_bound_name, expected_struct_version, buffer, size);
}

SettingsStorageStatus settingsStorageCurrentGameDelete(void)
{
    if (!s_game_bound)
    {
        return SETTINGS_STORAGE_STATUS_NOT_FOUND;
    }
    return settingsStorageGameDelete(s_bound_name);
}
