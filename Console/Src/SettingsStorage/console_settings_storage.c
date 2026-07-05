#include "console_settings_storage.h"
#include "settings_storage.h"
#include "backlight.h"
#include "logger.h"
#include <stddef.h>
#include <string.h>

void consoleSettingsResetDefaults(ConsoleSettings *const settings)
{
    if (settings == NULL)
    {
        return;
    }
    memset(settings, 0, sizeof(*settings));
    settings->audio_enabled = 1U;                        /* audio on by default */
    settings->wifi_valid = 0U;                           /* no WiFi credentials yet */
    settings->brightness = BACKLIGHT_DEFAULT_PERCENT;    /* full brightness by default */
}

SettingsStorageStatus consoleSettingsLoad(ConsoleSettings *const settings)
{
    if (settings == NULL)
    {
        return SETTINGS_STORAGE_STATUS_INVALID_ARG;
    }

    uint16_t size = (uint16_t)sizeof(*settings);
    const SettingsStorageStatus st =
        settingsStorageConsoleRead(CONSOLE_SETTINGS_VERSION, (uint8_t *)settings, &size);

    if (st != SETTINGS_STORAGE_STATUS_OK || size != sizeof(*settings))
    {
        consoleSettingsResetDefaults(settings);
        LOGGER_LOG_INFO(LOGGER_SETTINGS, "console settings defaulted (%d)", (int)st);
        return st;
    }
    return SETTINGS_STORAGE_STATUS_OK;
}

SettingsStorageStatus consoleSettingsSave(const ConsoleSettings *const settings)
{
    if (settings == NULL)
    {
        return SETTINGS_STORAGE_STATUS_INVALID_ARG;
    }
    return settingsStorageConsoleWrite(CONSOLE_SETTINGS_VERSION, (const uint8_t *)settings, (uint16_t)sizeof(*settings));
}
