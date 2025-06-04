
#ifndef __CONSOLE_API_H
#define __CONSOLE_API_H
#include <stdint.h>
#include "stdbool.h"

typedef struct
{
    // SYSTIME
    uint32_t (*getSysTime)(void);

    // USART DEBUG
    void (*usartBufferFlush)(void);
    void (*debugChar)(char c);
    void (*debugString)(const char *str);
    void (*debugInt)(uint32_t num);
    void (*debugHex)(uint32_t num);
    void (*debugBinary)(uint32_t num, uint8_t width);

    // SOUND
    uint8_t (*buzzerGetMaxTracks)();
    bool (*buzzerPlay)(uint8_t track_number, bool is_looped, uint16_t *frequencies_hz, uint16_t *durations_ms, uint16_t notes_number);
    bool (*buzzerPlayWithCallback)(uint8_t track_number, bool is_looped, uint16_t *frequencies_hz, uint16_t *durations_ms, uint16_t notes_number, void (*on_done_callback)(void));
    bool (*buzzerPause)(uint8_t track_number);
    bool (*buzzerResume)(uint8_t track_number);
    bool (*buzzerStop)(uint8_t track_number);

    // JOYSTICKS
    bool (*joystickGetRBtnUp)(void);
    bool (*joystickGetRBtnRight)(void);
    bool (*joystickGetRBtnDown)(void);
    bool (*joystickGetRBtnLeft)(void);
    bool (*joystickGetLBtnUp)(void);
    bool (*joystickGetLBtnRight)(void);
    bool (*joystickGetLBtnDown)(void);
    bool (*joystickGetLBtnLeft)(void);
    bool (*joystickGetSpecialBtn1)(void);
    bool (*joystickGetSpecialBtn2)(void);
    uint8_t (*joystickGetRAnalogY)(void);
    uint8_t (*joystickGetRAnalogX)(void);
    uint8_t (*joystickGetLAnalogY)(void);
    uint8_t (*joystickGetLAnalogX)(void);

    // RENDERING
    void (*rendererRender)(void);
    void (*rendererSetDirtyCompleteRedraw)(void);
    uint16_t (*rendererGetWidthPixels)();
    uint16_t (*rendererGetHeightPixels)();
    uint16_t (*rendererGetWidthTiles)();
    uint16_t (*rendererGetHeightTiles)();
    uint16_t (*rendererGetTilePixelSize)();
    uint16_t (*rendererGetTileMemorySize)();
    uint16_t (*rendererGetFramePaletteSize)();
    uint16_t (*rendererGetFrameSubPaletteSize)();
    uint16_t (*rendererGetPatternTableSize)();
    uint16_t (*rendererGetNameTableSize)();
    uint16_t (*rendererGetOamSize)();
    void (*rendererFramePaletteSetSprite)(uint8_t palette_idx, uint8_t color_idx, uint8_t system_palette_idx);
    void (*rendererFramePaletteSetSpriteMultiple)(uint8_t palette_idx, uint8_t system_palette_idx_1, uint8_t system_palette_idx_2, uint8_t system_palette_idx_3);
    void (*rendererFramePaletteSetBackground)(uint8_t palette_idx, uint8_t color_idx, uint8_t system_palette_idx);
    void (*rendererFramePaletteSetBackgroundMultiple)(uint8_t palette_idx, uint8_t system_palette_idx_1, uint8_t system_palette_idx_2, uint8_t system_palette_idx_3);
    void (*rendererPatternTableSetTile)(uint8_t pattern_table_idx, const uint8_t *tile_data, uint8_t tile_size);
    void (*rendererPatternTableClear)();
    void (*rendererNameTableSetTile)(uint8_t tile_x, uint8_t tile_y, uint8_t pattern_table_idx);
    void (*rendererOamClearEntry)(uint8_t oam_idx);
    void (*rendererOamSetXYPos)(uint8_t oam_idx, uint8_t x_pos, uint8_t y_pos);
    void (*rendererOamSetFlipV)(uint8_t oam_idx, bool is_flip_v);
    void (*rendererOamSetFlipH)(uint8_t oam_idx, bool is_flip_h);
    void (*rendererOamSetPriorityLow)(uint8_t oam_idx, bool is_priority_low);
    void (*rendererOamSetPaletteIdx)(uint8_t oam_idx, uint8_t palette_idx);
    void (*rendererOamSetTileIdx)(uint8_t oam_idx, uint8_t tile_idx);
    uint8_t (*rendererOamGetXPos)(uint8_t oam_idx);
    bool (*rendererOamGetFlipV)(uint8_t oam_idx);
    bool (*rendererOamGetFlipH)(uint8_t oam_idx);
    bool (*rendererOamGetPriorityLow)(uint8_t oam_idx);
    uint8_t (*rendererOamGetPaletteIdx)(uint8_t oam_idx);
    uint8_t (*rendererOamGetTileIdx)(uint8_t oam_idx);
    uint8_t (*rendererOamGetYPos)(uint8_t oam_idx);
    void (*rendererAttributeTableSetPalette)(uint8_t tile_x, uint8_t tile_y, uint8_t palette);
    uint8_t (*rendererAttributeTableGetPalette)(uint8_t tile_x, uint8_t tile_y);
    void (*rendererAttributeTableSetFlipV)(uint8_t tile_x, uint8_t tile_y, bool isFlipV);
    bool (*rendererAttributeTableGetFlipV)(uint8_t tile_x, uint8_t tile_y);
    void (*rendererAttributeTableSetFlipH)(uint8_t tile_x, uint8_t tile_y, bool isFlipH);
    bool (*rendererAttributeTableGetFlipH)(uint8_t tile_x, uint8_t tile_y);
    void (*rendererAttributeTableSetPriorityHigh)(uint8_t tile_x, uint8_t tile_y, bool is_priority_high);
    bool (*rendererAttributeTableGetPriorityHigh)(uint8_t tile_x, uint8_t tile_y);
} ConsoleAPI;

#define API_MAGIC 0xDEADBEEFU
#define API_VERSION 1

typedef struct
{
    uint32_t magic;
    uint32_t version;
    ConsoleAPI *api;
} ConsoleAPIHeader;

#endif /* __CONSOLE_API_H */