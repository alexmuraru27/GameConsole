#ifndef __SETTINGS_LAYOUT_H
#define __SETTINGS_LAYOUT_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "settings_interface.h"          /* SettingsStorageStatus, SETTINGS_GAME_MAX_DATA */
#include "SettingsStorage/settings_storage.h" /* SETTINGS_GAME_NAME_MAX */

/*
 * Console-internal layout + directory/entity mechanism for the EEPROM settings
 * store. This header is shared only by the two implementation files of the
 * subsystem — settings_directory.c (the mechanism) and settings_storage.c (the
 * public API + header sequencing that is a client of the mechanism). Games and
 * the rest of the console see only settings_storage.h.
 *
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
#define SETTINGS_GAME_SLOTS       29U
#define SETTINGS_CONSOLE_MAX_DATA 2042U

/* These are verified by static_assert in settings_directory.c. */
#define EEPROM_TOTAL_SIZE    65536U
#define DIRECTORY_ENTRY_SIZE 73U /* == sizeof(GameDirectoryEntry) */

#define ADDR_SYS_HEADER        0x0000U
#define SYS_HEADER_REGION_SIZE 0x0100U /* 256 B */

#define ADDR_DIRECTORY 0x0100U
/* 29 × 73 = 2117 B (0x0845); ADDR_CONSOLE_SETTINGS = 0x0100 + 2117 = 0x0945 */

#define ADDR_CONSOLE_SETTINGS (ADDR_DIRECTORY + SETTINGS_GAME_SLOTS * DIRECTORY_ENTRY_SIZE)
#define CONSOLE_ENTITY_SIZE   2048U

#define ADDR_GAMES     (ADDR_CONSOLE_SETTINGS + CONSOLE_ENTITY_SIZE)
#define GAME_SLOT_SIZE 2048U

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

/* ------------------------------------------------------------------ *
 *  Directory / entity mechanism (settings_directory.c).
 *
 *  A self-contained layer over the raw EEPROM: address math, CRC, byte
 *  I/O, name normalization, directory-entry read/write/occupancy/scan,
 *  and the shared 2 KB entity encode/verify. It carries no module state
 *  of its own — the public API (settings_storage.c) owns the header and
 *  init flag and drives this layer.
 * ------------------------------------------------------------------ */

/* Raw EEPROM byte I/O, mapped to the store's status enum. */
SettingsStorageStatus settingsEepromRead(uint16_t addr, void *buf, uint16_t len);
SettingsStorageStatus settingsEepromWrite(uint16_t addr, const void *buf, uint16_t len);

/* crc16 (CCITT) over a struct's bytes excluding its trailing crc16 field. */
uint16_t settingsStructCrc(const void *obj, size_t size);

/* Base EEPROM address of game data slot `index`. */
uint16_t settingsSlotAddr(uint16_t index);

/* Reduce a .bin name to a storage key: drop the extension, lowercase, truncate.
 * Lets "GameXO.bin", "GAMEXO.BIN" and "gamexo" all map to the same save. */
void settingsNormalizeName(const char *name, char out[SETTINGS_GAME_NAME_MAX]);

/* Read / write directory entry `index` (write stamps its CRC). */
SettingsStorageStatus settingsDirReadEntry(uint16_t index, GameDirectoryEntry *entry);
SettingsStorageStatus settingsDirWriteEntry(uint16_t index, GameDirectoryEntry *entry);

/* A slot is occupied only by a valid, ACTIVE directory entry; a FREE or
 * crc-broken entry is treated as empty. */
bool settingsDirEntryOccupied(const GameDirectoryEntry *entry);

/* Locate an occupied entry by normalized key. Optionally returns the entry and
 * index. NOT_FOUND when no occupied entry matches. */
SettingsStorageStatus settingsDirFindEntry(const char *key, GameDirectoryEntry *entry_out, uint16_t *index_out);

/* First unoccupied slot index, or STORAGE_FULL when the directory is full. */
SettingsStorageStatus settingsDirFindFreeSlot(uint16_t *index_out);

/* Free a slot: mark its directory entry FREE and wipe the data slot. */
SettingsStorageStatus settingsDirFreeSlot(uint16_t index);

/* Count occupied slots by scanning the directory (authoritative). */
uint16_t settingsDirCountGames(void);

/* Encode / verify the shared 2 KB entity {version, data_size, data[], crc16} —
 * one path for both the console blob and every game slot. `max` is the entity's
 * data capacity (SETTINGS_CONSOLE_MAX_DATA / SETTINGS_GAME_MAX_DATA). */
SettingsStorageStatus settingsEntityWrite(uint16_t addr, uint16_t version,
                                          const uint8_t *data, uint16_t size, uint16_t max);
SettingsStorageStatus settingsEntityRead(uint16_t addr, uint16_t expected_version,
                                         uint8_t *buffer, uint16_t *size, uint16_t max);

#endif /* __SETTINGS_LAYOUT_H */
