#include "Kernel/syscall.h"
#include "syscall_numbers.h"

#include <stddef.h>
#include <stm32f407xx.h>

#include "Peripherals/sysclock.h"
#include "Peripherals/systime.h"
#include "Peripherals/rng.h"
#include "Devices/buzzer.h"
#include "Devices/joystick.h"
#include "Renderer/renderer.h"
#include "Loader/asset_loader.h"
#include "SettingsStorage/settings_storage.h"
#include "Fonts/fonts.h"
#include "Fonts/font_utils.h"
#include "Loader/game_loader.h"
#include "Multiplayer/mp_session.h"
#include "Kernel/scheduler.h"
#include "Kernel/os_services.h"
#include "Kernel/syscall_validate.h"
#include "Logger/logger.h"

/* Reject a syscall whose pointer/argument validation failed: log it on the kernel
 * channel and return `ret` to the game. Used at the ~15 guard sites in the switch
 * (which all have `id` in scope) so the rejection is one line, not four. */
#define SYSCALL_REJECT(ret)                                                       \
    do                                                                            \
    {                                                                             \
        LOGGER_LOG_WARN(LOGGER_KERNEL, "syscall %lu rejected: bad pointer",       \
                        (unsigned long)id);                                       \
        return (ret);                                                             \
    } while (0)

/*
 * A game's delay(), bounded and preemptible. SYS_DELAY runs in the SVC handler,
 * which shares PendSV's (lowest) priority, so a plain delay() would out-wait the
 * PendSV the liveness deadline pends to abandon a runaway game: the callback
 * deadline (SysTick, higher priority) would fire and set the leave pending, but
 * PendSV could not preempt the equal-priority handler to act on it — the console
 * would freeze until the IWDG hard-resets it. So poll instead: a wrap-safe busy
 * wait that bails the instant a session leave is pending (the deadline tick, a
 * crash, or gameExit have all set PENDSVSET by then), letting the handler return so
 * PendSV tail-chains and the game recovers gracefully. The clamp to one callback
 * budget is an independent backstop, bounding the wait even if the deadline never
 * fired.
 */
static void gameDelay(uint32_t ms)
{
    if (ms > GAME_CALLBACK_BUDGET_MS)
    {
        ms = GAME_CALLBACK_BUDGET_MS;
    }
    const uint32_t deadline = getSysTime() + ms;
    while ((int32_t)(deadline - getSysTime()) > 0)
    {
        if ((SCB->ICSR & SCB_ICSR_PENDSVSET_Msk) != 0U)
        {
            break; /* a leave is pending — return so PendSV can end the session */
        }
    }
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
        gameDelay(a[0]);
        return 0;
    case SYS_GET_DELTA_US:
        return gameLoaderGetDeltaUs();
    case SYS_GET_RANDOM:
        return rngGetRandom();

    /* ---- buzzer ---- */
    case SYS_BUZZER_GET_MAX_TRACKS:
        return buzzerGetMaxTracks();
    case SYS_BUZZER_PLAY:
        if (!gameCanRead((const void *)a[2], a[3] * 2U * sizeof(uint16_t)))
        {
            SYSCALL_REJECT(false);
        }
        return buzzerPlay((uint8_t)a[0], (bool)a[1], (const uint16_t *)a[2], (uint16_t)a[3]);
    case SYS_BUZZER_PLAY_WITH_FLAG:
    {
        const SyscallBuzzerPlayArgs *args = (const SyscallBuzzerPlayArgs *)a[0];
        if (!gameCanRead(args, sizeof(*args)) ||
            !gameCanRead(args->notes_data, args->notes_number * 2U * sizeof(uint16_t)) ||
            (args->on_done_flag != NULL && !gameCanWrite(args->on_done_flag, sizeof(bool))))
        {
            SYSCALL_REJECT(false);
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

    /* ---- input ---- */
    case SYS_INPUT_GET_STATE:
        /* The frame was already latched by the kernel at the collect seam; this
         * just copies the snapshot into the game's buffer. */
        if (!gameCanWrite((void *)a[0], sizeof(InputState)))
        {
            SYSCALL_REJECT(0);
        }
        joystickGetState((InputState *)a[0]);
        return 0;

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
    case SYS_RENDERER_DRAW_TEXT:
    {
        const SyscallDrawTextArgs *args = (const SyscallDrawTextArgs *)a[0];
        char textbuf[128];
        /* Validate the marshalled struct, then copy the string into a bounded
         * kernel buffer (per-byte range-checked) so a non-terminated or oversized
         * game string can't walk the renderer off the end of game memory. */
        if (!gameCanRead(args, sizeof(*args)) ||
            !gameCopyStringIn(textbuf, sizeof(textbuf), args->text))
        {
            SYSCALL_REJECT(0);
        }
        rendererDrawText((Layer)args->layer, args->x, args->y, args->z,
                         (FontSize)args->font, args->scale, args->color, textbuf);
        return 0;
    }

    /* ---- assets ---- */
    case SYS_ASSET_METADATA:
        if (!gameCanWrite((void *)a[1], sizeof(AssetMetaData)))
        {
            SYSCALL_REJECT(ASSET_LOADER_RET_ERR);
        }
        return assetLoaderGetAssetMetadata(a[0], (AssetMetaData *)a[1]);
    case SYS_ASSET_DATA:
        if (!gameCanWrite((void *)a[1], a[2]))
        {
            SYSCALL_REJECT(ASSET_LOADER_RET_ERR);
        }
        return assetLoaderGetAssetData(a[0], (uint8_t *)a[1], a[2]);

    /* ---- settings (routed to the running game's bound slot) ---- */
    case SYS_SETTINGS_READ:
    {
        uint16_t *size = (uint16_t *)a[2];
        if (!gameCanWrite(size, sizeof(uint16_t)) || !gameCanWrite((void *)a[1], *size))
        {
            SYSCALL_REJECT((uint8_t)SETTINGS_STORAGE_STATUS_INVALID_ARG);
        }
        return (uint8_t)settingsStorageCurrentGameRead((uint16_t)a[0], (uint8_t *)a[1], size);
    }
    case SYS_SETTINGS_WRITE:
        if (!gameCanRead((const void *)a[1], (uint16_t)a[2]))
        {
            SYSCALL_REJECT((uint8_t)SETTINGS_STORAGE_STATUS_INVALID_ARG);
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
            SYSCALL_REJECT(0);
        }
        fontGet((uint8_t)a[0], (FontSize)a[1], (const uint8_t **)a[2]);
        return 0;
    case SYS_FONT_SIZE:
        return fontSize((FontSize)a[0], (uint8_t)a[1]);
    case SYS_FONT_SCALE:
        if (!gameCanWrite((void *)a[3], fontSize((FontSize)a[1], (uint8_t)a[2])))
        {
            SYSCALL_REJECT(0);
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
            SYSCALL_REJECT(0);
        }
        loggerGameLog("%.*s", (int)a[1], (const char *)a[0]);
        return 0;

    /* ---- OS UI services ---- */
    case SYS_OS_TEXT_INPUT:
    {
        /* a[0] = title (game C-string), a[1] = out buffer, a[2] = capacity. */
        char title[48];
        char *const buf = (char *)a[1];
        const uint16_t max = (uint16_t)a[2];
        if (max == 0U || !gameCanWrite(buf, max) ||
            !gameCopyStringIn(title, sizeof(title), (const char *)a[0]))
        {
            SYSCALL_REJECT(false);
        }
        LOGGER_LOG_INFO(LOGGER_KERNEL, "osTextInput: opening keyboard modal (cap=%u)", (unsigned)max);
        /* The keyboard blocks for as long as the player types; exempt this callback
         * from the liveness deadline while it is open, then re-arm a fresh budget.
         * (The IWDG is fed from inside the keyboard loop.) */
        kernelSuspendCallbackDeadline();
        const bool confirmed = osServicesTextInput(title, buf, max);
        kernelResumeCallbackDeadline();
        LOGGER_LOG_INFO(LOGGER_KERNEL, "osTextInput: %s", confirmed ? "confirmed" : "cancelled");
        return (uint32_t)confirmed;
    }

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
            SYSCALL_REJECT((uint32_t)(-1));
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
            SYSCALL_REJECT(0);
        }
        return (uint32_t)mpSessionGetName((uint8_t)a[0], (char *)a[1], (int)a[2]);
    case SYS_MP_GET_SELF_NAME:
        if (!gameCanWrite((void *)a[0], a[1]))
        {
            SYSCALL_REJECT(0);
        }
        return (uint32_t)mpSessionGetSelfName((char *)a[0], (int)a[1]);
    case SYS_MP_SEND:
        if (!gameCanRead((const void *)a[1], a[2]))
        {
            SYSCALL_REJECT(false);
        }
        return (uint32_t)mpSessionSend((uint8_t)a[0], (const uint8_t *)a[1], (uint16_t)a[2]);
    case SYS_MP_RECEIVE:
        if (!gameCanWrite((void *)a[0], sizeof(uint8_t)) || !gameCanWrite((void *)a[1], a[2]))
        {
            SYSCALL_REJECT(0);
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
