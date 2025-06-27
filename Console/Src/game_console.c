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
#include "spi.h"

extern uint32_t __game_console_api_start; // Linker symbol
#define API_PTR ((ConsoleAPIHeader *)&__game_console_api_start)

static void gameConsoleExposeApi()
{
    const ConsoleAPI api =
        {
            // SYSTIME
            .getSysTime = &getSysTime,
            .delay = &delay,

            // USART DEBUG
            .debugChar = &debugChar,
            .debugString = &debugString,
            .debugInt = &debugInt,
            .debugHex = &debugHex,
            .debugBinary = &debugBinary,
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

static FATFS s_fatfs;
static void peripheralsInit()
{
    gpioInit();
    dmaInit();
    usartInit();
    timerInit();
    spiInit();
    adcInit();
    i2cInit();
}

static void devicesInit()
{
    ili9341Init(3U, rendererGetWidthPixels(), rendererGetHeightPixels());
    rendererInit();
    joystickInit();
    buzzerInit();
    externalEepromInit(EXTERNAL_EEPROM_AT24C256_ADDRESS);
    playBootSong();
    f_mount(&s_fatfs, "0:", 1U);
}

void gameConsoleInit()
{
    peripheralsInit();
    devicesInit();
    gameConsoleExposeApi();
}