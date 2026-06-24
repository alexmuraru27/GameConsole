#ifndef __CONSOLE_SETTINGS_STORAGE_H
#define __CONSOLE_SETTINGS_STORAGE_H
#include "stdbool.h"
#include "stdint.h"
#include "settings_interface.h"    /* SettingsStorageStatus */
#include "multiplayer_interface.h" /* MP_NAME_MAX (player name length) */

/*
 * Typed view of the console's own settings blob. Thin wrapper over the generic
 * settingsStorageConsole* API: it owns the ConsoleSettings struct + its version
 * so the rest of the firmware reads/writes settings by field, not by raw bytes.
 */

/* v3 added player_name (the multiplayer display name). */
#define CONSOLE_SETTINGS_VERSION 3U

/* WiFi credential field sizes (+1 for the NUL). Mirror NP_SSID_MAX/NP_PASS_MAX. */
#define CONSOLE_WIFI_SSID_SIZE 33U
#define CONSOLE_WIFI_PASS_SIZE 64U

/* Player display name shown to other consoles in multiplayer (+1 for the NUL). */
#define CONSOLE_PLAYER_NAME_SIZE (MP_NAME_MAX + 1U)

typedef struct
{
    uint8_t audio_enabled;
    uint8_t wifi_valid;                     /* 1 = the SSID/pass below are set */
    char wifi_ssid[CONSOLE_WIFI_SSID_SIZE]; /* NUL-terminated */
    char wifi_pass[CONSOLE_WIFI_PASS_SIZE]; /* NUL-terminated */
    char player_name[CONSOLE_PLAYER_NAME_SIZE]; /* empty => UID-derived default */
} __attribute__((packed)) ConsoleSettings;

/* Fill `settings` with the factory defaults (audio on, no WiFi creds). */
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
