#include "SettingsStorage/settings_storage.h"
#include "Devices/external_eeprom.h"
#include "Crc/crc.h"
#include "Logger/logger.h"
#include <assert.h>
#include <string.h>
#include <stddef.h>

/*
 * On-EEPROM layout (AT24C512, 64 KB, uint16 addressing) — see docu/memory.md.
 * Packed flat with no arbitrary padding; every entity is exactly 2 KB.
 *
 *   0x0000   256 B    SystemHeader  (magic/version, game count, write seq, CRC)
 *   0x0100  2117 B    Game directory (29 × GameDirectoryEntry, 73 B each)
 *   0x0945  2048 B    Console settings entity
 *   0x1145    58 KB   Game data (29 slots × 2048 B each)
 *   0xF945   1.7 KB   (unused tail — not enough for another full slot)
 *
 * Directory entry i <-> game data slot i (same index, different regions).
 * Every persisted struct ends in a crc16 (CCITT) over all preceding bytes.
 */

#define SETTINGS_MAGIC_VERSION 0x5333U /* "S3" — 2 KB entities, 29 slots */

/* Number of game save slots and the usable-data payload per entity.
 * Both entities are exactly 2048 B (= 6 B header/CRC + 2042 B data). */
#define SETTINGS_GAME_SLOTS     29U
#define SETTINGS_CONSOLE_MAX_DATA 2042U

/* These are verified by static_assert below. */
#define EEPROM_TOTAL_SIZE     65536U
#define DIRECTORY_ENTRY_SIZE  73U      /* == sizeof(GameDirectoryEntry) */

#define ADDR_SYS_HEADER       0x0000U
#define SYS_HEADER_REGION_SIZE 0x0100U /* 256 B */

#define ADDR_DIRECTORY        0x0100U
/* 29 × 73 = 2117 B (0x0845); ADDR_CONSOLE_SETTINGS = 0x0100 + 2117 = 0x0945 */

#define ADDR_CONSOLE_SETTINGS (ADDR_DIRECTORY + SETTINGS_GAME_SLOTS * DIRECTORY_ENTRY_SIZE)
#define CONSOLE_ENTITY_SIZE   2048U

#define ADDR_GAMES            (ADDR_CONSOLE_SETTINGS + CONSOLE_ENTITY_SIZE)
#define GAME_SLOT_SIZE        2048U

typedef enum
{
    SLOT_STATE_FREE = 0U,
    SLOT_STATE_ACTIVE = 1U,
} SlotState;

typedef struct
{
    uint16_t magic_version; /* SETTINGS_MAGIC_VERSION */
    uint16_t game_count;    /* active game entries (advisory; directory is source of truth) */
    uint32_t write_seq;     /* next sequence number to hand out (monotonic) */
    uint16_t crc16;
} __attribute__((packed)) SystemHeader;

typedef struct
{
    char name[SETTINGS_GAME_NAME_MAX]; /* game key, NUL-padded */
    uint8_t state;                     /* SlotState */
    uint8_t reserved;
    uint32_t write_seq; /* last-write order; 0 == created but no data yet */
    uint16_t crc16;
} __attribute__((packed)) GameDirectoryEntry;

typedef struct
{
    uint16_t version;
    uint16_t data_size;
    uint8_t data[SETTINGS_GAME_MAX_DATA];
    uint16_t crc16;
} __attribute__((packed)) GameDataEntity;

typedef struct
{
    uint16_t version;
    uint16_t data_size;
    uint8_t data[SETTINGS_CONSOLE_MAX_DATA];
    uint16_t crc16;
} __attribute__((packed)) ConsoleSettingsEntity;

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

static SystemHeader s_header;
static bool s_initialized = false;
static bool s_game_bound = false;
static char s_bound_name[SETTINGS_GAME_NAME_MAX];

/* ------------------------------------------------------------------ helpers */

static uint16_t dirEntryAddr(const uint16_t index)
{
    return (uint16_t)(ADDR_DIRECTORY + index * sizeof(GameDirectoryEntry));
}

static uint16_t slotAddr(const uint16_t index)
{
    return (uint16_t)(ADDR_GAMES + index * GAME_SLOT_SIZE);
}

/* crc16 over a struct's bytes excluding its trailing crc16 field. */
static uint16_t structCrc(const void *const obj, const size_t size)
{
    return crc16_calculate((const uint8_t *)obj, (uint32_t)(size - sizeof(uint16_t)));
}

static SettingsStorageStatus eepromRead(const uint16_t addr, void *const buf, const uint16_t len)
{
    return (externalEepromRead(addr, (uint8_t *)buf, len) == 0U)
               ? SETTINGS_STORAGE_STATUS_OK
               : SETTINGS_STORAGE_STATUS_EEPROM_ERROR;
}

static SettingsStorageStatus eepromWrite(const uint16_t addr, const void *const buf, const uint16_t len)
{
    return (externalEepromWrite(addr, (const uint8_t *)buf, len) == 0U)
               ? SETTINGS_STORAGE_STATUS_OK
               : SETTINGS_STORAGE_STATUS_EEPROM_ERROR;
}

/* Reduce a .bin name to a storage key: drop the extension, lowercase, truncate.
 * Lets "GameXO.bin", "GAMEXO.BIN" and "gamexo" all map to the same save. */
static void normalizeName(const char *const name, char out[SETTINGS_GAME_NAME_MAX])
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

static SettingsStorageStatus writeHeader(void)
{
    s_header.crc16 = structCrc(&s_header, sizeof(s_header));
    return eepromWrite(ADDR_SYS_HEADER, &s_header, sizeof(s_header));
}

static SettingsStorageStatus readDirEntry(const uint16_t index, GameDirectoryEntry *const entry)
{
    return eepromRead(dirEntryAddr(index), entry, sizeof(*entry));
}

static SettingsStorageStatus writeDirEntry(const uint16_t index, GameDirectoryEntry *const entry)
{
    entry->crc16 = structCrc(entry, sizeof(*entry));
    return eepromWrite(dirEntryAddr(index), entry, sizeof(*entry));
}

/* A slot is occupied only by a valid, ACTIVE directory entry. A FREE or
 * crc-broken entry is treated as empty (the broken one is reclaimed on write
 * and freed by cleanup). */
static bool entryOccupied(const GameDirectoryEntry *const entry)
{
    return (entry->state == SLOT_STATE_ACTIVE) && (structCrc(entry, sizeof(*entry)) == entry->crc16);
}

/* Locate an occupied entry by normalized key. Optionally returns the entry and index. */
static SettingsStorageStatus findEntry(const char *const key, GameDirectoryEntry *const entry_out, uint16_t *const index_out)
{
    GameDirectoryEntry entry;
    for (uint16_t i = 0U; i < SETTINGS_GAME_SLOTS; i++)
    {
        const SettingsStorageStatus st = readDirEntry(i, &entry);
        if (st != SETTINGS_STORAGE_STATUS_OK)
        {
            return st;
        }
        if (entryOccupied(&entry) && strncmp(entry.name, key, SETTINGS_GAME_NAME_MAX) == 0)
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

static SettingsStorageStatus findFreeSlot(uint16_t *const index_out)
{
    GameDirectoryEntry entry;
    for (uint16_t i = 0U; i < SETTINGS_GAME_SLOTS; i++)
    {
        const SettingsStorageStatus st = readDirEntry(i, &entry);
        if (st != SETTINGS_STORAGE_STATUS_OK)
        {
            return st;
        }
        if (!entryOccupied(&entry))
        {
            *index_out = i;
            return SETTINGS_STORAGE_STATUS_OK;
        }
    }
    return SETTINGS_STORAGE_STATUS_STORAGE_FULL;
}

/* Free a slot: mark its directory entry FREE and wipe the data slot. */
static SettingsStorageStatus freeSlot(const uint16_t index)
{
    GameDirectoryEntry entry;
    memset(&entry, 0, sizeof(entry));
    entry.state = SLOT_STATE_FREE;
    const SettingsStorageStatus st = writeDirEntry(index, &entry);
    if (st != SETTINGS_STORAGE_STATUS_OK)
    {
        return st;
    }
    if (externalEepromClearRange(slotAddr(index), GAME_SLOT_SIZE) != 0U)
    {
        return SETTINGS_STORAGE_STATUS_EEPROM_ERROR;
    }
    return SETTINGS_STORAGE_STATUS_OK;
}

/* Count occupied slots by scanning the directory (authoritative). */
static uint16_t countGames(void)
{
    GameDirectoryEntry entry;
    uint16_t count = 0U;
    for (uint16_t i = 0U; i < SETTINGS_GAME_SLOTS; i++)
    {
        if (readDirEntry(i, &entry) == SETTINGS_STORAGE_STATUS_OK && entryOccupied(&entry))
        {
            count++;
        }
    }
    return count;
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
        SettingsStorageStatus st = readDirEntry(i, &entry);
        if (st != SETTINGS_STORAGE_STATUS_OK)
        {
            return st;
        }

        /* Only ACTIVE+valid entries are occupied; everything else is free. */
        if (!entryOccupied(&entry))
        {
            continue;
        }

        bool corrupted = false;
        if (entry.write_seq != 0U) /* 0 == reserved, no data written yet */
        {
            GameDataEntity data;
            st = eepromRead(slotAddr(i), &data, sizeof(data));
            if (st != SETTINGS_STORAGE_STATUS_OK)
            {
                return st;
            }
            if (structCrc(&data, sizeof(data)) != data.crc16 || data.data_size > SETTINGS_GAME_MAX_DATA)
            {
                corrupted = true;
            }
        }

        if (corrupted)
        {
            LOGGER_LOG_WARN(LOGGER_SETTINGS, "clearing corrupted save '%s' (slot %u)", entry.name, i);
            st = freeSlot(i);
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

    SettingsStorageStatus st = eepromRead(ADDR_SYS_HEADER, &s_header, sizeof(s_header));
    if (st != SETTINGS_STORAGE_STATUS_OK)
    {
        LOGGER_LOG_ERROR(LOGGER_SETTINGS, "header read failed");
        return st;
    }

    const bool valid = (s_header.magic_version == SETTINGS_MAGIC_VERSION) &&
                       (structCrc(&s_header, sizeof(s_header)) == s_header.crc16);
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

/* The console blob (ConsoleSettingsEntity) and every game slot (GameDataEntity) are
 * the same 2 KB record — {version, data_size, data[], crc16} — so these two helpers
 * are the single encode / verify path for both, keeping the CRC + version invariant
 * in one place. The working buffer is a GameDataEntity, but its byte layout is
 * identical to ConsoleSettingsEntity (both are one slot; see the static_asserts), so
 * the bytes written/read are the same either way. `max` is the entity's data
 * capacity (SETTINGS_CONSOLE_MAX_DATA / SETTINGS_GAME_MAX_DATA — equal by layout). */
static SettingsStorageStatus entityWrite(uint16_t addr, uint16_t version,
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
    entity.crc16 = structCrc(&entity, sizeof(entity));
    return eepromWrite(addr, &entity, sizeof(entity));
}

static SettingsStorageStatus entityRead(uint16_t addr, uint16_t expected_version,
                                        uint8_t *buffer, uint16_t *size, uint16_t max)
{
    GameDataEntity entity;
    const SettingsStorageStatus st = eepromRead(addr, &entity, sizeof(entity));
    if (st != SETTINGS_STORAGE_STATUS_OK)
    {
        return st;
    }
    if (structCrc(&entity, sizeof(entity)) != entity.crc16)
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

SettingsStorageStatus settingsStorageConsoleWrite(const uint16_t struct_version, const uint8_t *const data, const uint16_t size)
{
    if (!s_initialized)
    {
        return SETTINGS_STORAGE_STATUS_NOT_INITIALIZED;
    }

    const SettingsStorageStatus st =
        entityWrite(ADDR_CONSOLE_SETTINGS, struct_version, data, size, SETTINGS_CONSOLE_MAX_DATA);
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

    return entityRead(ADDR_CONSOLE_SETTINGS, expected_struct_version, buffer, size, SETTINGS_CONSOLE_MAX_DATA);
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
    normalizeName(game_name, key);
    if (key[0] == '\0')
    {
        return SETTINGS_STORAGE_STATUS_INVALID_ARG;
    }

    if (findEntry(key, NULL, NULL) == SETTINGS_STORAGE_STATUS_OK)
    {
        return SETTINGS_STORAGE_STATUS_OK; /* already present */
    }

    uint16_t index;
    SettingsStorageStatus st = findFreeSlot(&index);
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
    st = writeDirEntry(index, &entry);
    if (st != SETTINGS_STORAGE_STATUS_OK)
    {
        return st;
    }
    if (externalEepromClearRange(slotAddr(index), GAME_SLOT_SIZE) != 0U)
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
    normalizeName(game_name, key);
    if (key[0] == '\0')
    {
        return SETTINGS_STORAGE_STATUS_INVALID_ARG;
    }

    uint16_t index;
    bool created = false;
    SettingsStorageStatus st = findEntry(key, NULL, &index);
    if (st == SETTINGS_STORAGE_STATUS_NOT_FOUND)
    {
        st = findFreeSlot(&index);
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

    st = entityWrite(slotAddr(index), struct_version, data, size, SETTINGS_GAME_MAX_DATA);
    if (st != SETTINGS_STORAGE_STATUS_OK)
    {
        return st;
    }

    GameDirectoryEntry entry;
    memset(&entry, 0, sizeof(entry));
    memcpy(entry.name, key, SETTINGS_GAME_NAME_MAX);
    entry.state = SLOT_STATE_ACTIVE;
    entry.write_seq = seq;
    st = writeDirEntry(index, &entry);
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
    normalizeName(game_name, key);
    if (key[0] == '\0')
    {
        return SETTINGS_STORAGE_STATUS_INVALID_ARG;
    }

    GameDirectoryEntry entry;
    uint16_t index;
    SettingsStorageStatus st = findEntry(key, &entry, &index);
    if (st != SETTINGS_STORAGE_STATUS_OK)
    {
        return st; /* NOT_FOUND */
    }
    if (entry.write_seq == 0U)
    {
        return SETTINGS_STORAGE_STATUS_NOT_FOUND; /* reserved but never written */
    }

    return entityRead(slotAddr(index), expected_struct_version, buffer, size, SETTINGS_GAME_MAX_DATA);
}

SettingsStorageStatus settingsStorageGameDelete(const char *const game_name)
{
    if (!s_initialized)
    {
        return SETTINGS_STORAGE_STATUS_NOT_INITIALIZED;
    }

    char key[SETTINGS_GAME_NAME_MAX];
    normalizeName(game_name, key);
    if (key[0] == '\0')
    {
        return SETTINGS_STORAGE_STATUS_INVALID_ARG;
    }

    uint16_t index;
    SettingsStorageStatus st = findEntry(key, NULL, &index);
    if (st != SETTINGS_STORAGE_STATUS_OK)
    {
        return st;
    }

    st = freeSlot(index);
    if (st != SETTINGS_STORAGE_STATUS_OK)
    {
        return st;
    }

    if (s_header.game_count > 0U)
    {
        s_header.game_count--;
    }
    st = writeHeader();
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
    normalizeName(game_name, key);
    if (key[0] == '\0')
    {
        return false;
    }
    return findEntry(key, NULL, NULL) == SETTINGS_STORAGE_STATUS_OK;
}

/* ------------------------------------------------------------------ management */

uint16_t settingsStorageGameCount(void)
{
    return s_initialized ? countGames() : 0U;
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
    SettingsStorageStatus st = readDirEntry(slot_index, &entry);
    if (st != SETTINGS_STORAGE_STATUS_OK)
    {
        return st;
    }
    if (!entryOccupied(&entry))
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
        st = eepromRead(slotAddr(slot_index), &data_entity, sizeof(data_entity));
        if (st != SETTINGS_STORAGE_STATUS_OK)
        {
            return st;
        }
        if (structCrc(&data_entity, sizeof(data_entity)) == data_entity.crc16)
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
        const SettingsStorageStatus st = readDirEntry(i, &entry);
        if (st != SETTINGS_STORAGE_STATUS_OK)
        {
            return st;
        }
        if (!entryOccupied(&entry))
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
    (void)readDirEntry(oldest_index, &entry);
    memcpy(evicted_name, entry.name, SETTINGS_GAME_NAME_MAX);
    evicted_name[SETTINGS_GAME_NAME_MAX - 1U] = '\0';

    SettingsStorageStatus st = freeSlot(oldest_index);
    if (st != SETTINGS_STORAGE_STATUS_OK)
    {
        return st;
    }
    if (s_header.game_count > 0U)
    {
        s_header.game_count--;
    }
    st = writeHeader();
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

    normalizeName(game_name, s_bound_name);
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
