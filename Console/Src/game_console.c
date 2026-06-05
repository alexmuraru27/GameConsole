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
            .buzzerPlayWithCallback = &buzzerPlayWithCallback,
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
            .rendererPatternTableClear = &rendererPatternTableClear,
            .rendererNameTableClear = &rendererNameTableClear,
            .rendererAttributeTableClear = &rendererAttributeTableClear,
            .rendererOamClear = &rendererOamClear,
            .rendererFramePaletteSpriteClear = &rendererFramePaletteSpriteClear,
            .rendererFramePaletteBgClear = &rendererFramePaletteBgClear,
            .rendererRender = &rendererRender,
            .rendererSetDirtyCompleteRedraw = &rendererSetDirtyCompleteRedraw,
            .rendererGetWidthPixels = &rendererGetWidthPixels,
            .rendererGetHeightPixels = &rendererGetHeightPixels,
            .rendererGetWidthTiles = &rendererGetWidthTiles,
            .rendererGetHeightTiles = &rendererGetHeightTiles,
            .rendererGetTilePixelSize = &rendererGetTilePixelSize,
            .rendererGetTileMemorySize = &rendererGetTileMemorySize,
            .rendererGetFramePaletteSize = &rendererGetFramePaletteSize,
            .rendererGetFrameSubPaletteSize = &rendererGetFrameSubPaletteSize,
            .rendererGetPatternTableSize = &rendererGetPatternTableSize,
            .rendererGetNameTableSize = &rendererGetNameTableSize,
            .rendererGetOamSize = &rendererGetOamSize,
            .rendererFramePaletteSetSprite = &rendererFramePaletteSetSprite,
            .rendererFramePaletteSetSpriteMultiple = &rendererFramePaletteSetSpriteMultiple,
            .rendererFramePaletteSetBackground = &rendererFramePaletteSetBackground,
            .rendererFramePaletteSetBackgroundMultiple = &rendererFramePaletteSetBackgroundMultiple,
            .rendererPatternTableSetTile = &rendererPatternTableSetTile,
            .rendererNameTableSetTile = &rendererNameTableSetTile,
            .rendererOamClearEntry = &rendererOamClearEntry,
            .rendererOamSetXYPos = &rendererOamSetXYPos,
            .rendererOamSetFlipV = &rendererOamSetFlipV,
            .rendererOamSetFlipH = &rendererOamSetFlipH,
            .rendererOamSetPriorityLow = &rendererOamSetPriorityLow,
            .rendererOamSetPaletteIdx = &rendererOamSetPaletteIdx,
            .rendererOamSetTileIdx = &rendererOamSetTileIdx,
            .rendererOamGetXPos = &rendererOamGetXPos,
            .rendererOamGetFlipV = &rendererOamGetFlipV,
            .rendererOamGetFlipH = &rendererOamGetFlipH,
            .rendererOamGetPriorityLow = &rendererOamGetPriorityLow,
            .rendererOamGetPaletteIdx = &rendererOamGetPaletteIdx,
            .rendererOamGetTileIdx = &rendererOamGetTileIdx,
            .rendererOamGetYPos = &rendererOamGetYPos,
            .rendererAttributeTableSetPalette = &rendererAttributeTableSetPalette,
            .rendererAttributeTableGetPalette = &rendererAttributeTableGetPalette,
            .rendererAttributeTableSetFlipV = &rendererAttributeTableSetFlipV,
            .rendererAttributeTableGetFlipV = &rendererAttributeTableGetFlipV,
            .rendererAttributeTableSetFlipH = &rendererAttributeTableSetFlipH,
            .rendererAttributeTableGetFlipH = &rendererAttributeTableGetFlipH,
            .rendererAttributeTableSetPriorityHigh = &rendererAttributeTableSetPriorityHigh,
            .rendererAttributeTableGetPriorityHigh = &rendererAttributeTableGetPriorityHigh,
            // ASSETS
            .assetLoaderGetAssetMetadata = &assetLoaderGetAssetMetadata,
            .assetLoaderGetAssetData = &assetLoaderGetAssetData,
            .assetLoaderGetAssetHeader = &assetLoaderGetAssetHeader};

    const ConsoleAPIHeader api_header = {
        .magic = API_MAGIC,
        .version = 1U,
        .api = api};

    *API_PTR = api_header;
}

const uint16_t s_boot_melody[] = {
    NOTE_D4, NOTE_FS4, NOTE_A4, NOTE_D5, NOTE_FS5, NOTE_A5,
    NOTE_D6, NOTE_A5, NOTE_FS5, NOTE_D5, NOTE_A4};

const uint16_t s_boot_tempo[] = {
    450, 400, 350, 300, 250, 200,
    150, 200, 250, 300, 600};

static void playBootSong()
{
    buzzerPlay(0, false, s_boot_melody, s_boot_tempo, sizeof(s_boot_melody) / sizeof(uint16_t));
}

static const uint16_t s_step_notes[] = {
    NOTE_C4, NOTE_D4, NOTE_E4, NOTE_F4, NOTE_G4,
    NOTE_A4, NOTE_B4, NOTE_C5, NOTE_D5, NOTE_E5};
static const uint16_t s_step_tempo[] = {80};

static void beep_step(uint8_t step)
{
    buzzerPlay(0, false, &s_step_notes[step], s_step_tempo, 1);
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
    dmaInit();
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
    ili9341Init(1U, rendererGetWidthPixels(), rendererGetHeightPixels());
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