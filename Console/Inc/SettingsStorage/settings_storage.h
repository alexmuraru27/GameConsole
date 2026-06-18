#ifndef __SETTINGS_STORAGE_H
#define __SETTINGS_STORAGE_H

#include "stdbool.h"
#include "stdint.h"
#include "ff.h"                  /* FF_LFN_BUF — the loader's filename buffer size */
#include "settings_interface.h" /* SettingsStorageStatus, SETTINGS_GAME_MAX_DATA */

/*
 * Longest game key kept in a directory entry, sized to match the loader's
 * filename buffer (FatFs FILINFO.fname is FF_LFN_BUF + 1) so a game's .bin name
 * is never truncated when it becomes the save key. Console-internal: games never
 * see or pass a key — the loader binds the running game by name.
 */
#define SETTINGS_GAME_NAME_MAX (FF_LFN_BUF + 1U)

/* Snapshot of one stored game save, for listing / management UIs. */
typedef struct
{
    char name[SETTINGS_GAME_NAME_MAX]; /* game key (its .bin base name) */
    uint16_t data_size;                /* bytes currently stored */
    uint16_t data_version;             /* struct version of the stored data */
    uint32_t write_seq;                /* monotonic last-write order (smaller == older) */
} SettingsGameInfo;

/*
 * Settings storage — a small CRC-protected key/value store on the AT24C512
 * (64 KB) EEPROM, packed flat with no arbitrary padding (see docu/memory.md
 * for the on-EEPROM map).
 *
 *   - The console keeps one 2 KB settings blob (settingsStorageConsole*).
 *   - Each game gets a 2 KB slot keyed by its .bin name (settingsStorageGame*).
 *     A directory entry is created on demand (settingsStorageGameEnsure, or the
 *     first write). When all 29 slots are taken, writes return STORAGE_FULL —
 *     the store never evicts on its own; callers manage space with the listing
 *     / delete / evict-oldest helpers.
 *   - Corrupted entries are freed automatically on init and can be re-swept with
 *     settingsStorageCleanupCorrupted().
 *
 * Loaded games never call this directly: the game loader binds the running
 * game (settingsStorageBindGame) and the ConsoleAPI routes the game's
 * read/write/clear to the settingsStorageCurrentGame* functions.
 *
 * Every function returns SETTINGS_STORAGE_STATUS_OK (0) on success.
 */

/* ----- lifecycle ----- */
SettingsStorageStatus settingsStorageInit(void);
SettingsStorageStatus settingsStorageClear(void);
SettingsStorageStatus settingsStorageCleanupCorrupted(uint16_t *cleared_count_out);

/* ----- console settings (single blob) ----- */
SettingsStorageStatus settingsStorageConsoleWrite(uint16_t struct_version, const uint8_t *data, uint16_t size);
SettingsStorageStatus settingsStorageConsoleRead(uint16_t expected_struct_version, uint8_t *buffer, uint16_t *size);
SettingsStorageStatus settingsStorageConsoleDelete(void);

/* ----- game saves, keyed by .bin name (extension stripped, case-insensitive) ----- */
SettingsStorageStatus settingsStorageGameEnsure(const char *game_name);
SettingsStorageStatus settingsStorageGameWrite(const char *game_name, uint16_t struct_version, const uint8_t *data, uint16_t size);
SettingsStorageStatus settingsStorageGameRead(const char *game_name, uint16_t expected_struct_version, uint8_t *buffer, uint16_t *size);
SettingsStorageStatus settingsStorageGameDelete(const char *game_name);
bool settingsStorageGameExists(const char *game_name);

/* ----- management / introspection ----- */
uint16_t settingsStorageGameCount(void);
uint16_t settingsStorageGameCapacity(void);
SettingsStorageStatus settingsStorageGameInfoByIndex(uint16_t slot_index, SettingsGameInfo *info_out);
SettingsStorageStatus settingsStorageEvictOldest(SettingsGameInfo *evicted_out);

/* ----- current-game binding (loader + game-facing ConsoleAPI) ----- */
SettingsStorageStatus settingsStorageBindGame(const char *game_name);
void settingsStorageUnbindGame(void);
SettingsStorageStatus settingsStorageCurrentGameWrite(uint16_t struct_version, const uint8_t *data, uint16_t size);
SettingsStorageStatus settingsStorageCurrentGameRead(uint16_t expected_struct_version, uint8_t *buffer, uint16_t *size);
SettingsStorageStatus settingsStorageCurrentGameDelete(void);

#endif /* __SETTINGS_STORAGE_H */
