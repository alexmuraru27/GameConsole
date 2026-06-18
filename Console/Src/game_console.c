#include "game_console.h"
#include "game_console_api.h"
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
#include "stdio.h"

extern uint32_t __game_console_api_start; // Linker symbol
#define API_PTR ((ConsoleAPIHeader *)&__game_console_api_start)

/* Game-facing settings shims: route the running game's read/write/clear to its
 * bound save slot and narrow the SettingsStorageStatus result to the uint8_t the
 * ConsoleAPI table exposes (0 == OK). */
static uint8_t apiSettingsRead(uint16_t expected_version, uint8_t *buffer, uint16_t *size)
{
    return (uint8_t)settingsStorageCurrentGameRead(expected_version, buffer, size);
}

static uint8_t apiSettingsWrite(uint16_t version, const uint8_t *data, uint16_t size)
{
    return (uint8_t)settingsStorageCurrentGameWrite(version, data, size);
}

static uint8_t apiSettingsClear(void)
{
    return (uint8_t)settingsStorageCurrentGameDelete();
}

static void gameConsoleExposeApi()
{
    const ConsoleAPI api =
        {
            // SYSTIME
            .getSysTime = &getSysTime,
            .delay = &delay,

            // SOUND
            .buzzerGetMaxTracks = &buzzerGetMaxTracks,
            .buzzerPlay = &buzzerPlay,
            .buzzerPlayWithFlag = &buzzerPlayWithFlag,
            .buzzerPause = &buzzerPause,
            .buzzerResume = &buzzerResume,
            .buzzerStop = &buzzerStop,
            .buzzerStopAll = &buzzerStopAll,
            // JOYSTICKS
            .joystickGetRBtnUp = &joystickGetRBtnUp,
            .joystickGetRBtnRight = &joystickGetRBtnRight,
            .joystickGetRBtnDown = &joystickGetRBtnDown,
            .joystickGetRBtnLeft = &joystickGetRBtnLeft,
            .joystickGetLBtnUp = &joystickGetLBtnUp,
            .joystickGetLBtnRight = &joystickGetLBtnRight,
            .joystickGetLBtnDown = &joystickGetLBtnDown,
            .joystickGetLBtnLeft = &joystickGetLBtnLeft,
            .joystickGetSpecialBtn1 = &joystickGetSpecialBtn1,
            .joystickGetSpecialBtn2 = &joystickGetSpecialBtn2,
            .joystickGetRAnalogY = &joystickGetRAnalogY,
            .joystickGetRAnalogX = &joystickGetRAnalogX,
            .joystickGetLAnalogY = &joystickGetLAnalogY,
            .joystickGetLAnalogX = &joystickGetLAnalogX,
            .joystickIsAnyButtonPressed = &joystickIsAnyButtonPressed,
            // RENDERING
            .rendererInit = &rendererInit,
            .rendererClear = &rendererClear,
            .rendererSetBackground = &rendererSetBackground,
            .rendererSubmitLayer = &rendererSubmitLayer,
            .rendererRender = &rendererRender,
            .rendererGetWidthPixels = &rendererGetWidthPixels,
            .rendererGetHeightPixels = &rendererGetHeightPixels,
            .rendererSystemColor = &rendererSystemColor,
            // ASSETS
            .assetLoaderGetAssetMetadata = &assetLoaderGetAssetMetadata,
            .assetLoaderGetAssetData = &assetLoaderGetAssetData,
            // SETTINGS
            .settingsRead = &apiSettingsRead,
            .settingsWrite = &apiSettingsWrite,
            .settingsClear = &apiSettingsClear,
            // FONTS
            .fontGlyphW = &fontGlyphW,
            .fontGlyphH = &fontGlyphH,
            .fontGet = &fontGet,
            .fontSize = &fontSize,
            .fontScale = &fontScale,
            // LOGGING
            .log = &loggerGameLog};

    const ConsoleAPIHeader api_header = {
        .magic = API_MAGIC,
        .version = 2U, /* v2: added the SETTINGS calls */
        .api = api};

    *API_PTR = api_header;
}

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
    gpioInit();
    timerInit();
    buzzerInit();
}

/* The I2C storage stack (EEPROM + settings). Brought up on its own, before any
 * boot sound, so the persisted mute flag can be read and applied first; none of
 * it depends on the display, DMA or SD card. Silent. */
static void storageInit()
{
    i2cInit();
    externalEepromInit(EXTERNAL_EEPROM_AT24C512_ADDRESS);
    settingsStorageInit();
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
}

void gameConsoleInit()
{
    coreInit();
    storageInit();
    applyConsoleSettings(); /* honor the persisted mute before the first beep */

    peripheralsInit();
    devicesInit();
    gameConsoleExposeApi();
    beep_step(8);
    playBootSong();
}