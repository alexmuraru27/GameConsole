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
#include "i2c.h"
#include "external_eeprom.h"
#include "settings_storage.h"
#include "swo.h"
#include "logger.h"
#include "stdio.h"

extern uint32_t __game_console_api_start; // Linker symbol
#define API_PTR ((ConsoleAPIHeader *)&__game_console_api_start)

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
            .rendererRender = &rendererRender,
            // ASSETS
            .assetLoaderGetAssetMetadata = &assetLoaderGetAssetMetadata,
            .assetLoaderGetAssetData = &assetLoaderGetAssetData,
            .assetLoaderGetAssetHeader = &assetLoaderGetAssetHeader,
            // LOGGING
            .log = &loggerGameLog};

    const ConsoleAPIHeader api_header = {
        .magic = API_MAGIC,
        .version = 1U,
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

static FATFS s_fatfs;
static void peripheralsInit()
{
    swoInit(2000000);
    gpioInit();
    timerInit();
    buzzerInit();
    beep_step(0);
    dmaInit((uint32_t)FSMC_DATA_ADDRESS);
    beep_step(1);
    usartInit();
    beep_step(2);
    adcInit();
    beep_step(3);
    i2cInit();
}

static void devicesInit()
{
    beep_step(4);
    ili9341Init(1U, ILI9341_WIDTH, ILI9341_HEIGHT);
    beep_step(5);
    rendererInit();
    beep_step(6);
    joystickInit();
    beep_step(7);
    externalEepromInit(EXTERNAL_EEPROM_AT24C512_ADDRESS);
    beep_step(8);
    f_mount(&s_fatfs, "0:", 1U);
    settingsStorageInit();
    beep_step(9);
}

void gameConsoleInit()
{
    peripheralsInit();
    devicesInit();
    gameConsoleExposeApi();
    beep_step(10);
    playBootSong();
}