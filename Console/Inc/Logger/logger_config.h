#ifndef __LOGGER_CONFIG_H
#define __LOGGER_CONFIG_H

/* Master switch. Set to 0 to strip every log site from the build. */
#define LOGGER_ENABLED 1

/*
 * Highest severity still emitted. Levels are ordered
 * ERROR(0) < WARN(1) < INFO(2) < DEBUG(3); a site prints only when its level is
 * <= LOGGER_MAX_LEVEL. Drop this to LOGGER_LEVEL_WARN for a quieter build.
 */
#define LOGGER_MAX_LEVEL LOGGER_LEVEL_DEBUG

/* Per-channel switches (1 = enabled, 0 = silenced). */
#define LOGGER_CORE 1
#define LOGGER_RENDERER 1
#define LOGGER_DISPLAY 1
#define LOGGER_LOADER 1
#define LOGGER_ASSETS 1
#define LOGGER_BUZZER 1
#define LOGGER_JOYSTICK 1
#define LOGGER_SETTINGS 1
#define LOGGER_SDIO 1
#define LOGGER_EEPROM 1
#define LOGGER_NETWORK 1
#define LOGGER_FLASHER 1 /* ESP-01 firmware flashing over USART1 */
#define LOGGER_ESP01 1   /* diagnostics from inside the ESP firmware, forwarded over UART */
#define LOGGER_MENU 1
#define LOGGER_GAME 1   /* logs funnelled in from loaded games via api.log() */
#define LOGGER_KERNEL 1 /* syscall trust-boundary events: rejected pointers, bad ids */

#endif /* __LOGGER_CONFIG_H */
