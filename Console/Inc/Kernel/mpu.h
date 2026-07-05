#ifndef __KERNEL_MPU_H
#define __KERNEL_MPU_H

#include <stdint.h>

/*
 * MPU confinement for loaded games. The console runs privileged and keeps the
 * full default memory map (PRIVDEFENA); an unprivileged game can only touch the
 * regions opened for it. Anything else — console RAM, peripherals, flash — traps
 * as a MemManage fault.
 */

/* Enable the MPU with the privileged default map. No game regions yet. Boot-time. */
void mpuInit(void);

/* Open the running game's regions: GAME_RAM (unprivileged RWX), the CCM asset
 * arena (unprivileged RW, execute-never), and a no-access stack-overflow guard
 * band at stack_guard_base (from the game header — the .stack_guard section in
 * app.ld). A base that is not a valid 256-byte-aligned band inside GAME_RAM
 * leaves the guard disabled (the game runs, just unguarded). Called just before
 * entering a game. */
void mpuConfigureForGame(uint32_t stack_guard_base);

/* Close the game's regions so no unprivileged access survives the game. Called on
 * clean exit and on crash recovery. */
void mpuReleaseGame(void);

#endif /* __KERNEL_MPU_H */
