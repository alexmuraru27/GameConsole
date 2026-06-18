#include "game_console.h"
#include "sysclock.h"
#include "usart.h"
#include "joystick.h"
#include "renderer.h"
#include "buzzer.h"
#include "gpio.h"
#include "dma.h"
#include "ILI9341.h"
#include "adc.h"
#include "timer.h"
#include "sdio.h"
#include "ff.h"
#include "string.h"
#include "asset_loader.h"
#include "loader.h"
#include "i2c.h"
#include "external_eeprom.h"
#include "settings_storage.h"
#include "console_settings_storage.h"
#include "swo.h"
#include "logger.h"
#include "fonts.h"
#include "font_utils.h"
#include "faults.h"
#include "syscall.h"
#include "mpu.h"
#include "stdio.h"

const uint16_t s_boot_notes[] = {
    NOTE_D4, 450, NOTE_FS4, 400, NOTE_A4, 350, NOTE_D5, 300, NOTE_FS5, 250, NOTE_A5, 200,
    NOTE_D6, 150, NOTE_A5, 200, NOTE_FS5, 250, NOTE_D5, 300, NOTE_A4, 600};

static void playBootSong()
{
    buzzerPlay(0, false, s_boot_notes, sizeof(s_boot_notes) / sizeof(uint16_t) / 2U);
}

static const uint16_t s_step_notes[] = {
    NOTE_C4,
    80,
    NOTE_D4,
    80,
    NOTE_E4,
    80,
    NOTE_F4,
    80,
    NOTE_G4,
    80,
    NOTE_A4,
    80,
    NOTE_B4,
    80,
    NOTE_C5,
    80,
    NOTE_D5,
    80,
    NOTE_E5,
    80,
    NOTE_F5,
    80,
};

static void beep_step(uint8_t step)
{
    if (step >= (sizeof(s_step_notes) / sizeof(uint16_t) / 2U))
    {
        LOGGER_LOG_ERROR(LOGGER_CORE, "beep_step: step %u out of range", step);
        return;
    }
    buzzerPlay(0, false, &s_step_notes[step * 2U], 1);
    delay(150);
}

/* The minimal core every later phase (and the boot beeps) depends on: trace,
 * GPIO, timers, buzzer. Silent — runs before the mute flag is known. */
static void coreInit()
{
    swoInit(2000000);
    LOGGER_LOG_INFO(LOGGER_CORE, "=== GameConsole boot ===");
    faultsInit();   /* enable + decode MemManage/Bus/Usage faults once SWO is up */
    syscallInit();  /* set SVC/PendSV priorities for the game syscall trap */
    gpioInit();
    timerInit();
    buzzerInit();
    LOGGER_LOG_INFO(LOGGER_CORE, "core up: trace/faults/syscall/gpio/timers/buzzer");
}

/* The I2C storage stack (EEPROM + settings). Brought up on its own, before any
 * boot sound, so the persisted mute flag can be read and applied first; none of
 * it depends on the display, DMA or SD card. Silent. */
static void storageInit()
{
    i2cInit();
    externalEepromInit(EXTERNAL_EEPROM_AT24C512_ADDRESS);
    settingsStorageInit();
    LOGGER_LOG_INFO(LOGGER_CORE, "storage up: I2C/EEPROM/settings");
}

/* Apply the persisted console settings to the hardware. Runs before the boot
 * scale and boot song so a console muted by the user boots silent. The settings
 * menu owns the editable copy and the write-through path; this is the
 * read-and-apply side. */
static void applyConsoleSettings(void)
{
    ConsoleSettings cs;
    consoleSettingsLoad(&cs); /* fills defaults on miss/corrupt */
    buzzerSetMute(!cs.audio_enabled);
}

static void peripheralsInit()
{
    beep_step(0);
    dmaInit((uint32_t)FSMC_DATA_ADDRESS);
    beep_step(1);
    usartInit();
    beep_step(2);
    adcInit();
    LOGGER_LOG_INFO(LOGGER_CORE, "peripherals up: DMA/USART/ADC");
}

static void devicesInit()
{
    beep_step(3);
    ili9341Init(1U, ILI9341_WIDTH, ILI9341_HEIGHT);
    beep_step(4);
    rendererInit();
    beep_step(5);
    joystickInit();
    beep_step(6);
    loaderMediaInit();
    beep_step(7);
    LOGGER_LOG_INFO(LOGGER_CORE, "devices up: display/renderer/joystick/SD");
}

void gameConsoleInit()
{
    coreInit();
    storageInit();
    applyConsoleSettings(); /* honor the persisted mute before the first beep */

    peripheralsInit();
    devicesInit();
    mpuInit(); /* arm MPU confinement before any game can run */
    beep_step(8);
    playBootSong();
    LOGGER_LOG_INFO(LOGGER_CORE, "console ready");
}