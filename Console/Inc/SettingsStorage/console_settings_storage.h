#ifndef __CONSOLE_SETTINGS_STORAGE_H
#define __CONSOLE_SETTINGS_STORAGE_H
#include "stdbool.h"
#include "stdint.h"
#include "settings_interface.h" /* SettingsStorageStatus */

/*
 * Typed view of the console's own settings blob. Thin wrapper over the generic
 * settingsStorageConsole* API: it owns the ConsoleSettings struct + its version
 * so the rest of the firmware reads/writes settings by field, not by raw bytes.
 */

#define CONSOLE_SETTINGS_VERSION 1U

typedef struct
{
    uint8_t audio_enabled;
} __attribute__((packed)) ConsoleSettings;

/* Fill `settings` with the factory defaults (audio on). */
void consoleSettingsResetDefaults(ConsoleSettings *settings);

/*
 * Load the persisted console settings. On a missing / corrupt / version-mismatched
 * blob, `settings` is filled with defaults and the underlying status is returned
 * (so the caller may notice it was defaulted); on success returns OK. Either way
 * `settings` holds a usable struct.
 */
SettingsStorageStatus consoleSettingsLoad(ConsoleSettings *settings);

/* Persist the console settings. */
SettingsStorageStatus consoleSettingsSave(const ConsoleSettings *settings);

#endif /* __CONSOLE_SETTINGS_STORAGE_H */
