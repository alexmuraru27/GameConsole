#include "console_syscalls.h"
#include "syscall_numbers.h"
#include "header_interface.h"
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
uint32_t getDeltaTimeUs(void) { return svcCall(SYS_GET_DELTA_US, 0, 0, 0, 0); }

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

/* ---- multiplayer ---- */
MpRole mpGetRole(void) { return (MpRole)svcCall(SYS_MP_GET_ROLE, 0, 0, 0, 0); }
MpStatus mpHostStart(void) { return (MpStatus)svcCall(SYS_MP_HOST_START, 0, 0, 0, 0); }
MpStatus mpJoinStart(void) { return (MpStatus)svcCall(SYS_MP_JOIN_START, 0, 0, 0, 0); }

int mpScanHosts(MpHostInfo *out, int max)
{
    return (int)svcCall(SYS_MP_SCAN_HOSTS, (uint32_t)out, (uint32_t)max, 0, 0);
}

MpStatus mpJoin(uint8_t host_handle) { return (MpStatus)svcCall(SYS_MP_JOIN, host_handle, 0, 0, 0); }
void mpStop(void) { (void)svcCall(SYS_MP_STOP, 0, 0, 0, 0); }
uint8_t mpGetSelfIndex(void) { return (uint8_t)svcCall(SYS_MP_GET_SELF_INDEX, 0, 0, 0, 0); }
uint8_t mpGetPlayerCount(void) { return (uint8_t)svcCall(SYS_MP_GET_PLAYER_COUNT, 0, 0, 0, 0); }
bool mpIsConnected(uint8_t index) { return (bool)svcCall(SYS_MP_IS_CONNECTED, index, 0, 0, 0); }

int mpGetName(uint8_t index, char *buf, int max)
{
    return (int)svcCall(SYS_MP_GET_NAME, index, (uint32_t)buf, (uint32_t)max, 0);
}

int mpGetSelfName(char *buf, int max)
{
    return (int)svcCall(SYS_MP_GET_SELF_NAME, (uint32_t)buf, (uint32_t)max, 0, 0);
}

bool mpSend(uint8_t dst_index, const void *data, uint16_t len)
{
    return (bool)svcCall(SYS_MP_SEND, dst_index, (uint32_t)data, len, 0);
}

int mpReceive(uint8_t *src_index_out, void *data, uint16_t max)
{
    return (int)svcCall(SYS_MP_RECEIVE, (uint32_t)src_index_out, (uint32_t)data, max, 0);
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

/*
 * Runtime glue the OS drives. The game image carries no startup.s and no vector
 * table; the kernel reaches these two stubs through the binary header (filled by
 * DECLARE_GAME_HEADER) and uses them to run the game as a set of OS-called
 * callbacks rather than a self-contained main().
 */

/* C-runtime bootstrap. The OS invokes this once (unprivileged, on a fresh PSP)
 * before the game's init: it zeroes .bss — NOLOAD in the .bin, so the game clears
 * it itself, exactly as a reset handler would — and runs static constructors,
 * then returns to the OS via _game_return. */
extern uint32_t _sbss, _ebss;
extern void __libc_init_array(void);

void _game_start(void)
{
    for (uint32_t *p = &_sbss; p < &_ebss; ++p)
    {
        *p = 0U;
    }
    __libc_init_array();
}

/* Callback return trampoline. The kernel installs this as the return address (LR)
 * of every callback it invokes, so when a callback returns, control lands here
 * and traps back to the OS — which then runs the next step of its loop. The
 * frame is abandoned by the switch, so this never actually returns. */
__attribute__((naked)) void _game_return(void)
{
    __asm volatile(
        "mov r12, %0 \n"
        "svc #0      \n"
        : : "i"(SYS_FRAME_DONE) : "r12");
}
