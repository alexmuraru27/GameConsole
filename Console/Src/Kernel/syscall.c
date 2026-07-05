#include "syscall.h"
#include "syscall_numbers.h"

#include <stddef.h>
#include <stm32f407xx.h>

#include "sysclock.h"
#include "buzzer.h"
#include "joystick.h"
#include "renderer.h"
#include "asset_loader.h"
#include "settings_storage.h"
#include "fonts.h"
#include "font_utils.h"
#include "game_loader.h"
#include "mp_session.h"
#include "logger.h"

/*
 * Pointer validation — the confused-deputy defense. A game passes pointers into
 * its own memory; the privileged dispatcher must confirm a range lies wholly
 * inside something the game owns before dereferencing it on the game's behalf,
 * or a malicious game could make the console read/write console RAM, peripherals,
 * etc. through a syscall.
 *
 *   write target: GAME_RAM or the CCM asset arena (both RW for the game).
 *   read source : the above plus console flash — sprite pixels/palettes may point
 *                 at built-in font glyphs in flash, which the renderer reads. Flash
 *                 is contiguous and side-effect-free, so a bounded over-read there
 *                 cannot fault the kernel or touch live state.
 */
/* Region bounds come from the linker script (common.ld) as absolute symbols, so
 * the trust boundary tracks the real memory map instead of hard-coded addresses.
 * These are linker symbols — their *address* is the value, hence the &__sym casts. */
extern uint32_t __game_ram_start, __game_ram_size;
extern uint32_t __game_ram_asset_start, __game_ram_asset_size;
extern uint32_t __console_flash_start, __console_flash_size;

static bool rangeWithin(uint32_t a, uint32_t len, uint32_t base, uint32_t size)
{
    uint32_t end = a + len;
    if (end < a) /* address overflow */
    {
        return false;
    }
    return (a >= base) && (end <= base + size);
}

static bool gameCanWrite(const void *p, uint32_t len)
{
    const uint32_t a = (uint32_t)p;
    if (len == 0U)
    {
        return true;
    }
    return rangeWithin(a, len, (uint32_t)&__game_ram_start, (uint32_t)&__game_ram_size) ||
           rangeWithin(a, len, (uint32_t)&__game_ram_asset_start, (uint32_t)&__game_ram_asset_size);
}

static bool gameCanRead(const void *p, uint32_t len)
{
    const uint32_t a = (uint32_t)p;
    if (len == 0U)
    {
        return true;
    }
    return gameCanWrite(p, len) ||
           rangeWithin(a, len, (uint32_t)&__console_flash_start, (uint32_t)&__console_flash_size);
}

/* Packed pixel / palette extents the renderer will read for one sprite. */
static uint32_t spritePixelBytes(const Sprite *s)
{
    const uint32_t bpp = (s->flags & SPRITE_IS_FMT_4BPP) ? 4U : 2U;
    return ((uint32_t)s->w * (uint32_t)s->h * bpp + 7U) / 8U;
}

static uint32_t spritePaletteBytes(const Sprite *s)
{
    const uint32_t slots = (s->flags & SPRITE_IS_FMT_4BPP) ? 16U : 4U;
    return slots * sizeof(uint16_t);
}

/* A submitted layer is safe to render iff the array and every sprite's pixel and
 * palette data lie in readable game-accessible memory. */
static bool spritesValid(const Sprite *sprites, uint16_t count)
{
    if (!gameCanRead(sprites, (uint32_t)count * sizeof(Sprite)))
    {
        return false;
    }
    for (uint16_t i = 0U; i < count; i++)
    {
        if (!gameCanRead(sprites[i].pixels, spritePixelBytes(&sprites[i])) ||
            !gameCanRead(sprites[i].palette, spritePaletteBytes(&sprites[i])))
        {
            return false;
        }
    }
    return true;
}

/*
 * The dispatcher is deliberately an explicit switch rather than a generated
 * table: this is the trust boundary between the console and an untrusted game,
 * so every argument cast — and (from Phase D) every pointer validation — is meant
 * to be read in one place.
 *
 * `frame` is the exception frame the hardware stacked for the game on its PSP:
 * frame[0..3] = r0-r3 (the call arguments), frame[4] = r12 (the syscall id),
 * frame[6] = return PC. The dispatcher's return value is written back into
 * frame[0] so it lands in the caller's r0 on exception return.
 */
uint32_t svcDispatch(uint32_t id, uint32_t *a)
{
    switch (id)
    {
    /* ---- system time ---- */
    case SYS_GET_SYSTIME:
        return getSysTime();
    case SYS_DELAY:
        delay(a[0]);
        return 0;
    case SYS_GET_DELTA_US:
        return gameLoaderGetDeltaUs();

    /* ---- buzzer ---- */
    case SYS_BUZZER_GET_MAX_TRACKS:
        return buzzerGetMaxTracks();
    case SYS_BUZZER_PLAY:
        if (!gameCanRead((const void *)a[2], a[3] * 2U * sizeof(uint16_t)))
        {
            LOGGER_LOG_WARN(LOGGER_KERNEL, "syscall %lu rejected: bad pointer", (unsigned long)id);
            return false;
        }
        return buzzerPlay((uint8_t)a[0], (bool)a[1], (const uint16_t *)a[2], (uint16_t)a[3]);
    case SYS_BUZZER_PLAY_WITH_FLAG:
    {
        const SyscallBuzzerPlayArgs *args = (const SyscallBuzzerPlayArgs *)a[0];
        if (!gameCanRead(args, sizeof(*args)) ||
            !gameCanRead(args->notes_data, args->notes_number * 2U * sizeof(uint16_t)) ||
            (args->on_done_flag != NULL && !gameCanWrite(args->on_done_flag, sizeof(bool))))
        {
            LOGGER_LOG_WARN(LOGGER_KERNEL, "syscall %lu rejected: bad pointer", (unsigned long)id);
            return false;
        }
        return buzzerPlayWithFlag(args->track, args->is_looped, args->notes_data, args->notes_number, args->on_done_flag);
    }
    case SYS_BUZZER_PAUSE:
        return buzzerPause((uint8_t)a[0]);
    case SYS_BUZZER_RESUME:
        return buzzerResume((uint8_t)a[0]);
    case SYS_BUZZER_STOP:
        return buzzerStop((uint8_t)a[0]);
    case SYS_BUZZER_STOP_ALL:
        buzzerStopAll();
        return 0;

    /* ---- joysticks ---- */
    case SYS_JOY_R_UP:
        return joystickGetRBtnUp();
    case SYS_JOY_R_RIGHT:
        return joystickGetRBtnRight();
    case SYS_JOY_R_DOWN:
        return joystickGetRBtnDown();
    case SYS_JOY_R_LEFT:
        return joystickGetRBtnLeft();
    case SYS_JOY_L_UP:
        return joystickGetLBtnUp();
    case SYS_JOY_L_RIGHT:
        return joystickGetLBtnRight();
    case SYS_JOY_L_DOWN:
        return joystickGetLBtnDown();
    case SYS_JOY_L_LEFT:
        return joystickGetLBtnLeft();
    case SYS_JOY_SPECIAL1:
        return joystickGetSpecialBtn1();
    case SYS_JOY_SPECIAL2:
        return joystickGetSpecialBtn2();
    case SYS_JOY_R_ANALOG_Y:
        return joystickGetRAnalogY();
    case SYS_JOY_R_ANALOG_X:
        return joystickGetRAnalogX();
    case SYS_JOY_L_ANALOG_Y:
        return joystickGetLAnalogY();
    case SYS_JOY_L_ANALOG_X:
        return joystickGetLAnalogX();
    case SYS_JOY_ANY_PRESSED:
        return joystickIsAnyButtonPressed();

    /* ---- renderer ---- */
    case SYS_RENDERER_CLEAR:
        rendererClear();
        return 0;
    case SYS_RENDERER_SET_BACKGROUND:
        rendererSetBackground((uint16_t)a[0]);
        return 0;
    case SYS_RENDERER_SUBMIT_LAYER:
    {
        const Sprite *sprites = (const Sprite *)a[1];
        const uint16_t count = (uint16_t)a[2];
        if (spritesValid(sprites, count))
        {
            rendererSubmitLayer((Layer)a[0], sprites, count);
        }
        else
        {
            LOGGER_LOG_WARN(LOGGER_KERNEL, "syscall %lu rejected: bad sprite pointer(s)", (unsigned long)id);
        }
        return 0;
    }
    case SYS_RENDERER_RENDER:
        rendererRender();
        return 0;
    case SYS_RENDERER_WIDTH:
        return rendererGetWidthPixels();
    case SYS_RENDERER_HEIGHT:
        return rendererGetHeightPixels();
    case SYS_RENDERER_SYSTEM_COLOR:
        return rendererSystemColor((uint8_t)a[0]);

    /* ---- assets ---- */
    case SYS_ASSET_METADATA:
        if (!gameCanWrite((void *)a[1], sizeof(AssetMetaData)))
        {
            LOGGER_LOG_WARN(LOGGER_KERNEL, "syscall %lu rejected: bad pointer", (unsigned long)id);
            return ASSET_LOADER_RET_ERR;
        }
        return assetLoaderGetAssetMetadata(a[0], (AssetMetaData *)a[1]);
    case SYS_ASSET_DATA:
        if (!gameCanWrite((void *)a[1], a[2]))
        {
            LOGGER_LOG_WARN(LOGGER_KERNEL, "syscall %lu rejected: bad pointer", (unsigned long)id);
            return ASSET_LOADER_RET_ERR;
        }
        return assetLoaderGetAssetData(a[0], (uint8_t *)a[1], a[2]);

    /* ---- settings (routed to the running game's bound slot) ---- */
    case SYS_SETTINGS_READ:
    {
        uint16_t *size = (uint16_t *)a[2];
        if (!gameCanWrite(size, sizeof(uint16_t)) || !gameCanWrite((void *)a[1], *size))
        {
            LOGGER_LOG_WARN(LOGGER_KERNEL, "syscall %lu rejected: bad pointer", (unsigned long)id);
            return (uint8_t)SETTINGS_STORAGE_STATUS_INVALID_ARG;
        }
        return (uint8_t)settingsStorageCurrentGameRead((uint16_t)a[0], (uint8_t *)a[1], size);
    }
    case SYS_SETTINGS_WRITE:
        if (!gameCanRead((const void *)a[1], (uint16_t)a[2]))
        {
            LOGGER_LOG_WARN(LOGGER_KERNEL, "syscall %lu rejected: bad pointer", (unsigned long)id);
            return (uint8_t)SETTINGS_STORAGE_STATUS_INVALID_ARG;
        }
        return (uint8_t)settingsStorageCurrentGameWrite((uint16_t)a[0], (const uint8_t *)a[1], (uint16_t)a[2]);
    case SYS_SETTINGS_CLEAR:
        return (uint8_t)settingsStorageCurrentGameDelete();

    /* ---- fonts ---- */
    case SYS_FONT_GLYPH_W:
        return fontGlyphW((FontSize)a[0]);
    case SYS_FONT_GLYPH_H:
        return fontGlyphH((FontSize)a[0]);
    case SYS_FONT_GET:
        if (!gameCanWrite((void *)a[2], sizeof(const uint8_t *)))
        {
            LOGGER_LOG_WARN(LOGGER_KERNEL, "syscall %lu rejected: bad pointer", (unsigned long)id);
            return 0;
        }
        fontGet((uint8_t)a[0], (FontSize)a[1], (const uint8_t **)a[2]);
        return 0;
    case SYS_FONT_SIZE:
        return fontSize((FontSize)a[0], (uint8_t)a[1]);
    case SYS_FONT_SCALE:
        if (!gameCanWrite((void *)a[3], fontSize((FontSize)a[1], (uint8_t)a[2])))
        {
            LOGGER_LOG_WARN(LOGGER_KERNEL, "syscall %lu rejected: bad pointer", (unsigned long)id);
            return 0;
        }
        fontScale((uint8_t)a[0], (FontSize)a[1], (uint8_t)a[2], (uint8_t *)a[3]);
        return 0;

    /* ---- logging ---- */
    case SYS_LOG:
        /* a[0] = buffer (already formatted by the game), a[1] = length. The "%.*s"
         * format and the precision bound mean no game-supplied format specifier is
         * ever interpreted, and the read is length-bounded. */
        if (!gameCanRead((const void *)a[0], a[1]))
        {
            LOGGER_LOG_WARN(LOGGER_KERNEL, "syscall %lu rejected: bad pointer", (unsigned long)id);
            return 0;
        }
        loggerGameLog("%.*s", (int)a[1], (const char *)a[0]);
        return 0;

    /* ---- multiplayer (the game drives; mp_session.c owns the session) ---- */
    case SYS_MP_GET_ROLE:
        return (uint32_t)mpSessionGetRole();
    case SYS_MP_HOST_START:
        return (uint32_t)mpSessionHostStart();
    case SYS_MP_JOIN_START:
        return (uint32_t)mpSessionJoinStart();
    case SYS_MP_SCAN_HOSTS:
    {
        int max = (int)a[1];
        if (max < 0)
        {
            max = 0;
        }
        if (max > 64)
        {
            max = 64; /* mp scan never yields more than MP_MAX_HOSTS; cap guards the multiply */
        }
        if (!gameCanWrite((void *)a[0], (uint32_t)max * sizeof(MpHostInfo)))
        {
            LOGGER_LOG_WARN(LOGGER_KERNEL, "syscall %lu rejected: bad pointer", (unsigned long)id);
            return (uint32_t)(-1);
        }
        return (uint32_t)mpSessionScan((MpHostInfo *)a[0], max);
    }
    case SYS_MP_JOIN:
        return (uint32_t)mpSessionJoin((uint8_t)a[0]);
    case SYS_MP_STOP:
        mpSessionStop();
        return 0;
    case SYS_MP_GET_SELF_INDEX:
        return mpSessionGetSelfIndex();
    case SYS_MP_GET_PLAYER_COUNT:
        return mpSessionGetPlayerCount();
    case SYS_MP_IS_CONNECTED:
        return (uint32_t)mpSessionIsConnected((uint8_t)a[0]);
    case SYS_MP_GET_NAME:
        if (!gameCanWrite((void *)a[1], a[2]))
        {
            LOGGER_LOG_WARN(LOGGER_KERNEL, "syscall %lu rejected: bad pointer", (unsigned long)id);
            return 0;
        }
        return (uint32_t)mpSessionGetName((uint8_t)a[0], (char *)a[1], (int)a[2]);
    case SYS_MP_GET_SELF_NAME:
        if (!gameCanWrite((void *)a[0], a[1]))
        {
            LOGGER_LOG_WARN(LOGGER_KERNEL, "syscall %lu rejected: bad pointer", (unsigned long)id);
            return 0;
        }
        return (uint32_t)mpSessionGetSelfName((char *)a[0], (int)a[1]);
    case SYS_MP_SEND:
        if (!gameCanRead((const void *)a[1], a[2]))
        {
            LOGGER_LOG_WARN(LOGGER_KERNEL, "syscall %lu rejected: bad pointer", (unsigned long)id);
            return false;
        }
        return (uint32_t)mpSessionSend((uint8_t)a[0], (const uint8_t *)a[1], (uint16_t)a[2]);
    case SYS_MP_RECEIVE:
        if (!gameCanWrite((void *)a[0], sizeof(uint8_t)) || !gameCanWrite((void *)a[1], a[2]))
        {
            LOGGER_LOG_WARN(LOGGER_KERNEL, "syscall %lu rejected: bad pointer", (unsigned long)id);
            return 0;
        }
        return (uint32_t)mpSessionReceive((uint8_t *)a[0], (uint8_t *)a[1], (uint16_t)a[2]);

    /* ---- lifecycle ---- */
    case SYS_EXIT:
        /* Unreachable: SYS_EXIT/SYS_INVOKE/SYS_FRAME_DONE are the context switch and
         * are handled in svcHandlerMain (scheduler.c) before svcDispatch is reached. */
        return 0;

    default:
        LOGGER_LOG_ERROR(LOGGER_KERNEL, "bad syscall id %lu", (unsigned long)id);
        return 0;
    }
}

void syscallInit(void)
{
    /* SVC/PendSV at the lowest urgency so a long syscall (e.g. a full render) on
     * the kernel stack stays preemptible by the timer ISRs. */
    NVIC_SetPriority(SVCall_IRQn, 0xFU);
    NVIC_SetPriority(PendSV_IRQn, 0xFU);

    /* SysTick must outrank SVC. A syscall runs in the SVC handler, and some console
     * code it dispatches to busy-waits on the system tick — e.g. the EEPROM write
     * path delays 5 ms per page for the write cycle. SysTick_Config() leaves the
     * tick at the lowest priority (15), the same as SVC, so it could never preempt
     * the handler and getSysTime() would freeze mid-syscall. Raise it above SVC.
     * (Still below the 1 ms buzzer timer at priority 1, so audio timing is unaffected.) */
    NVIC_SetPriority(SysTick_IRQn, 0x2U);
}
