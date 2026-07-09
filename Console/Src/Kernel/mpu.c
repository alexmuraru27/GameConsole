#include "Kernel/mpu.h"
#include <stm32f407xx.h>
#include "mpu_armv7.h"
#include "Logger/logger.h"

/* GAME_RAM / CCM arena bounds come from common.ld. */
extern uint32_t __game_ram_start;
extern uint32_t __game_ram_size;
extern uint32_t __game_ram_asset_start;

#define MPU_REGION_GAME_RAM 0U
#define MPU_REGION_GAME_CCM 1U
#define MPU_REGION_GAME_GUARD 2U

/* Size of the game's stack-overflow guard band (must match the .stack_guard
 * section in linker/app.ld and be a valid ARMv7-M region size). */
#define STACK_GUARD_SIZE 256U

/* Normal, write-back internal memory (TEX=0, C=1, B=1), non-shareable. */
#define MEM_TEX 0U
#define MEM_SHAREABLE 0U
#define MEM_CACHEABLE 1U
#define MEM_BUFFERABLE 1U

void mpuInit(void)
{
    ARM_MPU_Disable();
    /* PRIVDEFENA: privileged code keeps the full default map; unprivileged code
     * gets only what later regions grant. No game is running yet, so no
     * unprivileged regions are programmed. */
    ARM_MPU_Enable(MPU_CTRL_PRIVDEFENA_Msk);
    LOGGER_LOG_INFO(LOGGER_KERNEL, "MPU enabled (PRIVDEFENA, no game regions yet)");
}

void mpuConfigureForGame(uint32_t stack_guard_base)
{
    /* GAME_RAM (32 KB): unprivileged read/write/execute — game code + data + stack. */
    ARM_MPU_SetRegionEx(MPU_REGION_GAME_RAM,
                        (uint32_t)&__game_ram_start,
                        ARM_MPU_RASR(0U /* executable */, ARM_MPU_AP_FULL, MEM_TEX,
                                     MEM_SHAREABLE, MEM_CACHEABLE, MEM_BUFFERABLE,
                                     0U /* no sub-region disable */, ARM_MPU_REGION_SIZE_32KB));

    /* CCM asset arena (64 KB): unprivileged read/write, execute-never (data only). */
    ARM_MPU_SetRegionEx(MPU_REGION_GAME_CCM,
                        (uint32_t)&__game_ram_asset_start,
                        ARM_MPU_RASR(1U /* execute-never */, ARM_MPU_AP_FULL, MEM_TEX,
                                     MEM_SHAREABLE, MEM_CACHEABLE, MEM_BUFFERABLE,
                                     0U, ARM_MPU_REGION_SIZE_64KB));

    /* Stack-overflow guard (256 B): a no-access band the game's descending PSP hits
     * on overflow — AP_PRIV denies the unprivileged game while leaving the console
     * untouched, and it overlaps region 0 so its stricter permission wins (higher
     * region number). The base comes from the game header (the .stack_guard section
     * in app.ld, 256-aligned). Validated to sit fully inside GAME_RAM; a bad/zero
     * base just leaves the region disabled (the game still runs, only unguarded). */
    const uint32_t game_ram_base = (uint32_t)&__game_ram_start;
    const uint32_t game_ram_end = game_ram_base + (uint32_t)&__game_ram_size;
    if ((stack_guard_base & (STACK_GUARD_SIZE - 1U)) == 0U &&
        stack_guard_base >= game_ram_base &&
        stack_guard_base + STACK_GUARD_SIZE <= game_ram_end)
    {
        ARM_MPU_SetRegionEx(MPU_REGION_GAME_GUARD,
                            stack_guard_base,
                            ARM_MPU_RASR(1U /* execute-never */, ARM_MPU_AP_PRIV, MEM_TEX,
                                         MEM_SHAREABLE, MEM_CACHEABLE, MEM_BUFFERABLE,
                                         0U, ARM_MPU_REGION_SIZE_256B));
        LOGGER_LOG_DEBUG(LOGGER_KERNEL, "MPU: stack guard armed at 0x%08lX (%u B)",
                         (unsigned long)stack_guard_base, (unsigned)STACK_GUARD_SIZE);
    }
    else
    {
        ARM_MPU_SetRegionEx(MPU_REGION_GAME_GUARD, 0U, 0U); /* disabled — run unguarded */
        LOGGER_LOG_WARN(LOGGER_KERNEL, "MPU: stack guard base 0x%08lX invalid; running unguarded",
                        (unsigned long)stack_guard_base);
    }

    __DSB();
    __ISB();
    LOGGER_LOG_DEBUG(LOGGER_KERNEL, "MPU: game regions armed (GAME_RAM rwx, CCM rw/xn, stack guard)");
}

void mpuReleaseGame(void)
{
    ARM_MPU_SetRegionEx(MPU_REGION_GAME_RAM, 0U, 0U); /* RASR=0 -> region disabled */
    ARM_MPU_SetRegionEx(MPU_REGION_GAME_CCM, 0U, 0U);
    ARM_MPU_SetRegionEx(MPU_REGION_GAME_GUARD, 0U, 0U);
    __DSB();
    __ISB();
}
