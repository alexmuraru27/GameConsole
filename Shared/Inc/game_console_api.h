
#ifndef __CONSOLE_API_H
#define __CONSOLE_API_H
#include <stdint.h>
#include "stdbool.h"

typedef struct
{
    char magic[4];
    uint32_t version;
    uint32_t asset_count;
} __attribute__((packed, aligned(1))) AssetHeader;

typedef struct
{
    uint32_t id;
    uint32_t type;
    uint32_t asset_size;
    uint32_t memory_size;
} __attribute__((packed, aligned(1))) AssetMetaData;

typedef struct
{
    AssetMetaData metadata;
    uint8_t data[];
} __attribute__((packed, aligned(1))) AssetData8;

typedef struct
{
    AssetMetaData metadata;
    uint16_t data[];
} __attribute__((packed, aligned(1))) AssetData16;

typedef struct
{
    AssetMetaData metadata;
    uint32_t data[];
} __attribute__((packed, aligned(1))) AssetData32;

//  aligned(1) -> ensure continuous structure asset space
#define _ASSET_SECTION_HEADER __attribute__((section(".assets.header"), aligned(1)))
#define _ASSET_SECTION_DATA __attribute__((section(".assets.data"), aligned(1)))

#define EXPAND_DATA(...) __VA_ARGS__

#define DEFINE_ASSET_HEADER(char_4_magic, version_nr, asset_count_nr) \
    _ASSET_SECTION_HEADER const AssetHeader asset_header = {          \
        .magic = char_4_magic,                                        \
        .version = version_nr,                                        \
        .asset_count = asset_count_nr}

#define _DEFINE_ASSET(name, asset_data_struct, data_type, asset_id, asset_type, asset_data)                                                                   \
    _ASSET_SECTION_DATA const asset_data_struct name = {                                                                                                      \
        .metadata = {asset_id, asset_type, sizeof((data_type[]){EXPAND_DATA asset_data}) / sizeof(data_type), sizeof((data_type[]){EXPAND_DATA asset_data})}, \
        .data = {EXPAND_DATA asset_data}}

#define DEFINE_ASSET_8(name, asset_id, asset_type, asset_data) \
    _DEFINE_ASSET(name, AssetData8, uint8_t, asset_id, asset_type, asset_data)

#define DEFINE_ASSET_16(name, asset_id, asset_type, asset_data) \
    _DEFINE_ASSET(name, AssetData16, uint16_t, asset_id, asset_type, asset_data)

#define DEFINE_ASSET_32(name, asset_id, asset_type, asset_data) \
    _DEFINE_ASSET(name, AssetData32, uint32_t, asset_id, asset_type, asset_data)

typedef struct
{
    // SYSTIME
    uint32_t (*getSysTime)(void);
    void (*delay)(uint32_t sys_time_delta);

    // USART DEBUG
    void (*debugChar)(char c);
    void (*debugString)(const char *str);
    void (*debugInt)(uint32_t num);
    void (*debugHex)(uint32_t num);
    void (*debugBinary)(uint32_t num, uint8_t width);

    // SOUND
    uint8_t (*buzzerGetMaxTracks)();
    bool (*buzzerPlay)(uint8_t track_number, bool is_looped, const uint16_t *const frequencies_hz, const uint16_t *const durations_ms, uint16_t notes_number);
    bool (*buzzerPlayWithCallback)(uint8_t track_number, bool is_looped, const uint16_t *const frequencies_hz, const uint16_t *const durations_ms, uint16_t notes_number, void (*on_done_callback)(void));
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
    void (*rendererInit)(void);
    void (*rendererPatternTableClear)();
    void (*rendererNameTableClear)();
    void (*rendererAttributeTableClear)();
    void (*rendererOamClear)();
    void (*rendererFramePaletteSpriteClear)();
    void (*rendererFramePaletteBgClear)();
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

    // ASSETS
    uint8_t (*assetLoaderGetAssetMetadata)(uint32_t asset_id, AssetMetaData *asset_metadata_out);
    uint8_t (*assetLoaderGetAssetData)(uint32_t asset_id, uint8_t *buffer);
    uint8_t (*assetLoaderGetAssetHeader)(AssetHeader *asset_header);
} ConsoleAPI;

#define API_MAGIC 0xDEADBEEFU
#define API_VERSION 1

typedef struct
{
    uint32_t magic;
    uint32_t version;
    ConsoleAPI api;
} ConsoleAPIHeader;

#define DECLARE_API_HEADER_PTR(var_name)              \
    extern ConsoleAPIHeader __game_console_api_start; \
    ConsoleAPIHeader *var_name = (ConsoleAPIHeader *)&__game_console_api_start

typedef struct
{
    uint32_t magic; // Just to identify it's a valid game file
    uint32_t header_start;
    uint32_t header_end;
    uint32_t text_start;
    uint32_t text_end;
    uint32_t ro_data_start;
    uint32_t ro_data_end;
    uint32_t data_start;
    uint32_t data_end;
    uint32_t bss_start;
    uint32_t bss_end;
    uint32_t assets_start;
    uint32_t assets_end;
    uint32_t entry_point;
} GameBinaryHeader;

// MAGIC = GAME in ASCII
#define DECLARE_GAME_BINARY_HEADER(header_var_name, entry_func)         \
    extern uint32_t __game_header_start, __game_header_end;             \
    extern uint32_t __game_text_start, __game_text_end;                 \
    extern uint32_t __game_ro_data_start, __game_ro_data_end;           \
    extern uint32_t __game_data_init_start, __game_data_init_end;       \
    extern uint32_t __game_data_no_init_start, __game_data_no_init_end; \
    extern uint32_t __game_code_assets_start, __game_code_assets_end;   \
    __attribute__((section(".game_header")))                            \
    const GameBinaryHeader header_var_name = {                          \
        .magic = 0x47414D45,                                            \
        .header_start = (uint32_t)&__game_header_start,                 \
        .header_end = (uint32_t)&__game_header_end,                     \
        .text_start = (uint32_t)&__game_text_start,                     \
        .text_end = (uint32_t)&__game_text_end,                         \
        .ro_data_start = (uint32_t)&__game_ro_data_start,               \
        .ro_data_end = (uint32_t)&__game_ro_data_end,                   \
        .data_start = (uint32_t)&__game_data_init_start,                \
        .data_end = (uint32_t)&__game_data_init_end,                    \
        .bss_start = (uint32_t)&__game_data_no_init_start,              \
        .bss_end = (uint32_t)&__game_data_no_init_end,                  \
        .assets_start = (uint32_t)&__game_code_assets_start,            \
        .assets_end = (uint32_t)&__game_code_assets_end,                \
        .entry_point = (uint32_t)&entry_func}

#endif /* __CONSOLE_API_H */