#ifndef __KERNEL_MPU_H
#define __KERNEL_MPU_H

/*
 * MPU confinement for loaded games. The console runs privileged and keeps the
 * full default memory map (PRIVDEFENA); an unprivileged game can only touch the
 * regions opened for it. Anything else — console RAM, peripherals, flash — traps
 * as a MemManage fault.
 */

/* Enable the MPU with the privileged default map. No game regions yet. Boot-time. */
void mpuInit(void);

/* Open the running game's regions: GAME_RAM (unprivileged RWX) and the CCM asset
 * arena (unprivileged RW, execute-never). Called just before entering a game. */
void mpuConfigureForGame(void);

/* Close the game's regions so no unprivileged access survives the game. Called on
 * clean exit and on crash recovery. */
void mpuReleaseGame(void);

#endif /* __KERNEL_MPU_H */
