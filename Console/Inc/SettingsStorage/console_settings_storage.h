#ifndef __CONSOLE_SETTINGS_STORAGE_H
#define __CONSOLE_SETTINGS_STORAGE_H
#include "stdbool.h"
#include "stdint.h"

#define CONSOLE_SETTINGS_VERSION 1U
typedef struct
{
    uint8_t audio_enabled;
} __attribute__((packed)) ConsoleSettings;

#endif /* __CONSOLE_SETTINGS_STORAGE_H */
