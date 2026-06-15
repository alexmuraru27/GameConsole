#ifndef __FUNCTION_API_H
#define __FUNCTION_API_H

typedef struct
{
    // SYSTIME
    uint32_t (*getSysTime)(void);
    void (*delay)(uint32_t sys_time_delta);

    // SOUND
    uint8_t (*buzzerGetMaxTracks)();
    bool (*buzzerPlay)(uint8_t track_number, bool is_looped, const uint16_t *const notes_data, uint16_t notes_number);
    bool (*buzzerPlayWithFlag)(uint8_t track_number, bool is_looped, const uint16_t *const notes_data, uint16_t notes_number, bool *on_done_flag);
    bool (*buzzerPause)(uint8_t track_number);
    bool (*buzzerResume)(uint8_t track_number);
    bool (*buzzerStop)(uint8_t track_number);
    void (*buzzerStopAll)();

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
    bool (*joystickIsAnyButtonPressed)(void);

    // RENDERING
    void (*rendererInit)(void);
    void (*rendererRender)(void);

    // ASSETS
    uint8_t (*assetLoaderGetAssetMetadata)(uint32_t asset_id, AssetMetaData *asset_metadata_out);
    uint8_t (*assetLoaderGetAssetData)(uint32_t asset_id, uint8_t *const buffer, const uint8_t buffer_size_bytes);
    uint8_t (*assetLoaderGetAssetHeader)(AssetHeader *asset_header);

    void (*log)(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
} ConsoleAPI;

#define API_MAGIC 0xDEADBEEFU

typedef struct
{
    uint32_t magic;
    uint32_t version;
    ConsoleAPI api;
} ConsoleAPIHeader;

#define DECLARE_API_HEADER_PTR(var_name)              \
    extern ConsoleAPIHeader __game_console_api_start; \
    static ConsoleAPIHeader *var_name = (ConsoleAPIHeader *)&__game_console_api_start

#endif