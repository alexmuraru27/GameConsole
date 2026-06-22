#ifndef __FLASH_LL_H
#define __FLASH_LL_H

#include <stdint.h>
#include <stdbool.h>

/*
 * Low-level STM32F4 internal-flash driver, shared by the bootloader and the OS
 * self-flasher. Erase and program run from RAM (.RamFunc): on this single-bank
 * part any flash access stalls while a program/erase is in flight, so the routine
 * doing it must not itself be fetched from flash. Each operation also runs with
 * interrupts disabled for its duration. The caller orchestrates from flash (it
 * must never erase/program the sector it is executing from) and reads flash back
 * normally — reads are only stalled, never faulted, between operations.
 */

/* Unlock / relock the flash control register (FLASH->CR). */
void flashLlUnlock(void);
void flashLlLock(void);

/* Erase one sector (0-11). Returns true if no error flag was raised. */
bool flashLlEraseSector(uint32_t sector);

/* Program one 32-bit word at `addr` (must be word-aligned). Returns true if the
 * word reads back equal and no error flag was raised. */
bool flashLlProgramWord(uint32_t addr, uint32_t word);

#endif /* __FLASH_LL_H */
