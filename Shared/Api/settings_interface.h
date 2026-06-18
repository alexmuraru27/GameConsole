#ifndef __SETTINGS_API_H
#define __SETTINGS_API_H

#include <stdint.h>

/*
 * Shared settings contract — only what a loaded game needs to talk to the
 * settings ConsoleAPI. A game never names or keys its save: the loader binds the
 * running game's slot by its .bin name, and the game just reads/writes its data
 * (if its header opted in). The name/key types are therefore console-internal
 * and live in Console/Inc/SettingsStorage/settings_storage.h, not here.
 */

/* Result of every settings operation (0 == success). */
typedef enum
{
    SETTINGS_STORAGE_STATUS_OK = 0U,               /* success */
    SETTINGS_STORAGE_STATUS_NOT_FOUND = 1U,        /* no save (e.g. first run, or game not allowed) */
    SETTINGS_STORAGE_STATUS_NOT_INITIALIZED = 2U,  /* settingsStorageInit() has not run / failed */
    SETTINGS_STORAGE_STATUS_CHECKSUM_MISMATCH = 3U,/* stored CRC did not match (corrupted) */
    SETTINGS_STORAGE_STATUS_VERSION_MISMATCH = 4U, /* stored struct version != expected */
    SETTINGS_STORAGE_STATUS_STORAGE_FULL = 5U,     /* no free game slot left */
    SETTINGS_STORAGE_STATUS_BUFFER_TOO_SMALL = 6U, /* caller buffer < stored data size */
    SETTINGS_STORAGE_STATUS_INVALID_ARG = 7U,      /* NULL / out-of-range argument */
    SETTINGS_STORAGE_STATUS_EEPROM_ERROR = 8U,     /* I2C / EEPROM access failed */
} SettingsStorageStatus;

/* Largest payload a single game save can hold (one 2 KB slot minus its header/CRC). */
#define SETTINGS_GAME_MAX_DATA 2042U

#endif /* __SETTINGS_API_H */
