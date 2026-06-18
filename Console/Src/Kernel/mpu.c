#include "mpu.h"
#include <stm32f407xx.h>
#include "mpu_armv7.h"

/* GAME_RAM / CCM arena bounds come from common.ld. */
extern uint32_t __game_ram_start;
extern uint32_t __game_ram_asset_start;

#define MPU_REGION_GAME_RAM 0U
#define MPU_REGION_GAME_CCM 1U

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
}

void mpuConfigureForGame(void)
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

    __DSB();
    __ISB();
}

void mpuReleaseGame(void)
{
    ARM_MPU_SetRegionEx(MPU_REGION_GAME_RAM, 0U, 0U); /* RASR=0 -> region disabled */
    ARM_MPU_SetRegionEx(MPU_REGION_GAME_CCM, 0U, 0U);
    __DSB();
    __ISB();
}
