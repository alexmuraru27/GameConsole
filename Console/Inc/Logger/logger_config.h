#ifndef __LOGGER_CONFIG_H
#define __LOGGER_CONFIG_H

/* Master switch. Set to 0 to strip every log site from the build. */
#define LOGGER_ENABLED 1

/*
 * Global severity ceiling applied on top of the per-channel levels below.
 * Ordering: NONE < ERROR(0) < WARN(1) < INFO(2) < DEBUG(3). A site prints only
 * when its severity is <= its channel's level AND <= this ceiling, so this is the
 * single knob that strips, say, all DEBUG from a production build in one line.
 * Leave at LOGGER_LEVEL_DEBUG to let the per-channel levels alone decide.
 */
#define LOGGER_MAX_LEVEL LOGGER_LEVEL_DEBUG

/*
 * Per-channel verbosity. Each channel carries its own threshold: a site at
 * <severity> survives only when <severity> <= the channel's level here, so
 *   LOGGER_LEVEL_NONE   -> silenced
 *   LOGGER_LEVEL_ERROR  -> errors only
 *   LOGGER_LEVEL_WARN   -> + warnings
 *   LOGGER_LEVEL_INFO   -> + lifecycle / once-per-boot milestones
 *   LOGGER_LEVEL_DEBUG  -> + per-driver detail
 * Every level is a compile-time constant, so lowering one folds its quieter sites
 * away to zero flash and zero cycles (the format string with them).
 *
 * Current policy: warnings + errors everywhere, full DEBUG only on the bring-up /
 * network path under active debugging (CORE, NETWORK, SDIO, ESP01).
 */
#define LOGGER_CORE LOGGER_LEVEL_DEBUG
#define LOGGER_RENDERER LOGGER_LEVEL_WARN
#define LOGGER_DISPLAY LOGGER_LEVEL_WARN
#define LOGGER_LOADER LOGGER_LEVEL_WARN
#define LOGGER_ASSETS LOGGER_LEVEL_WARN
#define LOGGER_BUZZER LOGGER_LEVEL_WARN
#define LOGGER_JOYSTICK LOGGER_LEVEL_WARN
#define LOGGER_SETTINGS LOGGER_LEVEL_WARN
#define LOGGER_SDIO LOGGER_LEVEL_DEBUG
#define LOGGER_EEPROM LOGGER_LEVEL_WARN
#define LOGGER_NETWORK LOGGER_LEVEL_DEBUG
#define LOGGER_USART LOGGER_LEVEL_DEBUG  /* USART1/ESP-01 link: DMA rx/tx faults, overruns, baud changes */
#define LOGGER_FLASHER LOGGER_LEVEL_WARN /* ESP-01 firmware flashing over USART1 */
#define LOGGER_ESP01 LOGGER_LEVEL_DEBUG  /* diagnostics from inside the ESP firmware, forwarded over UART */
#define LOGGER_MENU LOGGER_LEVEL_DEBUG
#define LOGGER_GAME LOGGER_LEVEL_WARN   /* logs funnelled in from loaded games via api.log() */
#define LOGGER_KERNEL LOGGER_LEVEL_WARN /* syscall trust-boundary events: rejected pointers, bad ids */

#endif /* __LOGGER_CONFIG_H */
