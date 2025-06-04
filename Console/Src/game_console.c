#include "game_console.h"
#include "game_console_api.h"

#include "sysclock.h"
#include "usart.h"
#include "joystick.h"
#include "renderer.h"
#include "buzzer.h"

extern uint32_t __game_console_api_start; // Linker symbol
#define API_PTR ((ConsoleAPIHeader *)&__game_console_api_start)

static void gameConsoleExposeApi()
{
    const ConsoleAPI api =
        {
            // SYSTIME
            .getSysTime = &getSysTime,
            // USART DEBUG
            .usartBufferFlush = &usartBufferFlush,
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
            // RENDERING
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
            .rendererPatternTableClear = &rendererPatternTableClear,
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
        };

    const ConsoleAPIHeader api_header = {
        .magic = API_MAGIC,
        .version = API_VERSION,
        .api = &api};

    *API_PTR = api_header;
}

void gameConsoleInit()
{
    gameConsoleExposeApi();
}