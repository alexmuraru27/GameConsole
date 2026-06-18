#include "console_syscalls.h"
#include "syscall_numbers.h"
#include <stdarg.h>
#include <stdio.h>

/*
 * Game-side syscall stubs. Each wrapper marshals its arguments into r0-r3, the
 * syscall id into r12, and executes `svc #0`. The console traps it, runs the
 * real work on the kernel stack, and returns the result in r0.
 *
 * r12 (IP) is caller-saved and not an argument register, so it carries the id
 * without disturbing the four argument registers the AAPCS already filled.
 */
static inline uint32_t svcCall(uint32_t id, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    register uint32_t r0 __asm("r0") = a0;
    register uint32_t r1 __asm("r1") = a1;
    register uint32_t r2 __asm("r2") = a2;
    register uint32_t r3 __asm("r3") = a3;
    register uint32_t r12 __asm("r12") = id;

    __asm volatile("svc #0"
                   : "+r"(r0)
                   : "r"(r1), "r"(r2), "r"(r3), "r"(r12)
                   : "memory");
    return r0;
}

/* ---- system time ---- */
uint32_t getSysTime(void) { return svcCall(SYS_GET_SYSTIME, 0, 0, 0, 0); }
void delay(uint32_t d) { (void)svcCall(SYS_DELAY, d, 0, 0, 0); }

/* ---- buzzer ---- */
uint8_t buzzerGetMaxTracks(void) { return (uint8_t)svcCall(SYS_BUZZER_GET_MAX_TRACKS, 0, 0, 0, 0); }

bool buzzerPlay(uint8_t track, bool looped, const uint16_t *notes, uint16_t count)
{
    return (bool)svcCall(SYS_BUZZER_PLAY, track, looped, (uint32_t)notes, count);
}

bool buzzerPlayWithFlag(uint8_t track, bool looped, const uint16_t *notes, uint16_t count, bool *on_done_flag)
{
    /* >4 register args: bundle them in game RAM and pass one validated pointer. */
    const SyscallBuzzerPlayArgs args = {
        .track = track,
        .is_looped = looped,
        .notes_data = notes,
        .notes_number = count,
        .on_done_flag = on_done_flag};
    return (bool)svcCall(SYS_BUZZER_PLAY_WITH_FLAG, (uint32_t)&args, 0, 0, 0);
}

bool buzzerPause(uint8_t track) { return (bool)svcCall(SYS_BUZZER_PAUSE, track, 0, 0, 0); }
bool buzzerResume(uint8_t track) { return (bool)svcCall(SYS_BUZZER_RESUME, track, 0, 0, 0); }
bool buzzerStop(uint8_t track) { return (bool)svcCall(SYS_BUZZER_STOP, track, 0, 0, 0); }
void buzzerStopAll(void) { (void)svcCall(SYS_BUZZER_STOP_ALL, 0, 0, 0, 0); }

/* ---- joysticks ---- */
bool joystickGetRBtnUp(void) { return (bool)svcCall(SYS_JOY_R_UP, 0, 0, 0, 0); }
bool joystickGetRBtnRight(void) { return (bool)svcCall(SYS_JOY_R_RIGHT, 0, 0, 0, 0); }
bool joystickGetRBtnDown(void) { return (bool)svcCall(SYS_JOY_R_DOWN, 0, 0, 0, 0); }
bool joystickGetRBtnLeft(void) { return (bool)svcCall(SYS_JOY_R_LEFT, 0, 0, 0, 0); }
bool joystickGetLBtnUp(void) { return (bool)svcCall(SYS_JOY_L_UP, 0, 0, 0, 0); }
bool joystickGetLBtnRight(void) { return (bool)svcCall(SYS_JOY_L_RIGHT, 0, 0, 0, 0); }
bool joystickGetLBtnDown(void) { return (bool)svcCall(SYS_JOY_L_DOWN, 0, 0, 0, 0); }
bool joystickGetLBtnLeft(void) { return (bool)svcCall(SYS_JOY_L_LEFT, 0, 0, 0, 0); }
bool joystickGetSpecialBtn1(void) { return (bool)svcCall(SYS_JOY_SPECIAL1, 0, 0, 0, 0); }
bool joystickGetSpecialBtn2(void) { return (bool)svcCall(SYS_JOY_SPECIAL2, 0, 0, 0, 0); }
JoystickAxisState joystickGetRAnalogY(void) { return (JoystickAxisState)svcCall(SYS_JOY_R_ANALOG_Y, 0, 0, 0, 0); }
JoystickAxisState joystickGetRAnalogX(void) { return (JoystickAxisState)svcCall(SYS_JOY_R_ANALOG_X, 0, 0, 0, 0); }
JoystickAxisState joystickGetLAnalogY(void) { return (JoystickAxisState)svcCall(SYS_JOY_L_ANALOG_Y, 0, 0, 0, 0); }
JoystickAxisState joystickGetLAnalogX(void) { return (JoystickAxisState)svcCall(SYS_JOY_L_ANALOG_X, 0, 0, 0, 0); }
bool joystickIsAnyButtonPressed(void) { return (bool)svcCall(SYS_JOY_ANY_PRESSED, 0, 0, 0, 0); }

/* ---- renderer ---- */
void rendererInit(void) { (void)svcCall(SYS_RENDERER_INIT, 0, 0, 0, 0); }
void rendererClear(void) { (void)svcCall(SYS_RENDERER_CLEAR, 0, 0, 0, 0); }
void rendererSetBackground(uint16_t color) { (void)svcCall(SYS_RENDERER_SET_BACKGROUND, color, 0, 0, 0); }

void rendererSubmitLayer(Layer layer, const Sprite *sprites, uint16_t count)
{
    (void)svcCall(SYS_RENDERER_SUBMIT_LAYER, (uint32_t)layer, (uint32_t)sprites, count, 0);
}

void rendererRender(void) { (void)svcCall(SYS_RENDERER_RENDER, 0, 0, 0, 0); }
uint16_t rendererGetWidthPixels(void) { return (uint16_t)svcCall(SYS_RENDERER_WIDTH, 0, 0, 0, 0); }
uint16_t rendererGetHeightPixels(void) { return (uint16_t)svcCall(SYS_RENDERER_HEIGHT, 0, 0, 0, 0); }
uint16_t rendererSystemColor(uint8_t idx) { return (uint16_t)svcCall(SYS_RENDERER_SYSTEM_COLOR, idx, 0, 0, 0); }

/* ---- assets ---- */
uint8_t assetLoaderGetAssetMetadata(uint32_t id, AssetMetaData *out)
{
    return (uint8_t)svcCall(SYS_ASSET_METADATA, id, (uint32_t)out, 0, 0);
}

uint8_t assetLoaderGetAssetData(uint32_t id, uint8_t *buffer, uint32_t size)
{
    return (uint8_t)svcCall(SYS_ASSET_DATA, id, (uint32_t)buffer, size, 0);
}

/* ---- settings ---- */
uint8_t settingsRead(uint16_t version, uint8_t *buffer, uint16_t *size)
{
    return (uint8_t)svcCall(SYS_SETTINGS_READ, version, (uint32_t)buffer, (uint32_t)size, 0);
}

uint8_t settingsWrite(uint16_t version, const uint8_t *data, uint16_t size)
{
    return (uint8_t)svcCall(SYS_SETTINGS_WRITE, version, (uint32_t)data, size, 0);
}

uint8_t settingsClear(void) { return (uint8_t)svcCall(SYS_SETTINGS_CLEAR, 0, 0, 0, 0); }

/* ---- fonts ---- */
uint16_t fontGlyphW(FontSize size) { return (uint16_t)svcCall(SYS_FONT_GLYPH_W, (uint32_t)size, 0, 0, 0); }
uint16_t fontGlyphH(FontSize size) { return (uint16_t)svcCall(SYS_FONT_GLYPH_H, (uint32_t)size, 0, 0, 0); }

void fontGet(uint8_t ch, FontSize size, const uint8_t **pixels)
{
    (void)svcCall(SYS_FONT_GET, ch, (uint32_t)size, (uint32_t)pixels, 0);
}

uint16_t fontSize(FontSize size, uint8_t scale)
{
    return (uint16_t)svcCall(SYS_FONT_SIZE, (uint32_t)size, scale, 0, 0);
}

void fontScale(uint8_t ch, FontSize size, uint8_t scale, uint8_t *dst)
{
    (void)svcCall(SYS_FONT_SCALE, ch, (uint32_t)size, scale, (uint32_t)dst);
}

/* ---- logging ---- */
void gameLog(const char *fmt, ...)
{
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (n < 0)
    {
        return;
    }
    if (n > (int)sizeof(buf))
    {
        n = (int)sizeof(buf); /* vsnprintf returns the would-be length; clamp to what we have */
    }
    (void)svcCall(SYS_LOG, (uint32_t)buf, (uint32_t)n, 0, 0);
}

/* ---- lifecycle ---- */
void gameExit(void)
{
    (void)svcCall(SYS_EXIT, 0, 0, 0, 0);
    /* The console does not return control here; spin defensively if it ever does. */
    for (;;)
    {
    }
}
