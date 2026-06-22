#include "flash_ll.h"
#include <stm32f407xx.h>

/*
 * The erase/program primitives are placed in .RamFunc so they execute from SRAM:
 * while a program or erase is in flight the flash bus stalls every access, so the
 * code driving it cannot live in flash. They poll FLASH->SR directly (the flash
 * *registers* stay accessible during an operation; only the flash *array* stalls)
 * and run with interrupts masked so no ISR is fetched from the stalled array
 * mid-operation. Unlock/lock are trivial register writes but share the section so
 * the whole driver is callable from the bootloader's apply path.
 */

/* All sticky error/status flags, cleared write-1-to-clear before each operation. */
#define FLASH_SR_ALL_ERRORS (FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_PGPERR | FLASH_SR_PGSERR)
#define FLASH_SR_ALL_FLAGS (FLASH_SR_EOP | FLASH_SR_ALL_ERRORS)

#define RAMFUNC __attribute__((section(".RamFunc"), noinline))

RAMFUNC void flashLlUnlock(void)
{
    if (FLASH->CR & FLASH_CR_LOCK)
    {
        FLASH->KEYR = 0x45670123U;
        FLASH->KEYR = 0xCDEF89ABU;
    }
}

RAMFUNC void flashLlLock(void)
{
    FLASH->CR |= FLASH_CR_LOCK;
}

RAMFUNC bool flashLlEraseSector(uint32_t sector)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();

    while (FLASH->SR & FLASH_SR_BSY)
    {
    }
    FLASH->SR = FLASH_SR_ALL_FLAGS;

    /* x32 program size, select-sector erase of `sector`, then start. */
    FLASH->CR &= ~(FLASH_CR_PSIZE | FLASH_CR_SNB);
    FLASH->CR |= FLASH_CR_PSIZE_1; /* PSIZE = 0b10 (x32) */
    FLASH->CR |= (sector << FLASH_CR_SNB_Pos) & FLASH_CR_SNB;
    FLASH->CR |= FLASH_CR_SER;
    FLASH->CR |= FLASH_CR_STRT;

    while (FLASH->SR & FLASH_SR_BSY)
    {
    }
    FLASH->CR &= ~(FLASH_CR_SER | FLASH_CR_SNB);

    const bool ok = (FLASH->SR & FLASH_SR_ALL_ERRORS) == 0U;
    if (!primask)
    {
        __enable_irq();
    }
    return ok;
}

RAMFUNC bool flashLlProgramWord(uint32_t addr, uint32_t word)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();

    while (FLASH->SR & FLASH_SR_BSY)
    {
    }
    FLASH->SR = FLASH_SR_ALL_FLAGS;

    FLASH->CR &= ~FLASH_CR_PSIZE;
    FLASH->CR |= FLASH_CR_PSIZE_1; /* x32 */
    FLASH->CR |= FLASH_CR_PG;

    *(volatile uint32_t *)addr = word;
    __DSB();

    while (FLASH->SR & FLASH_SR_BSY)
    {
    }
    FLASH->CR &= ~FLASH_CR_PG;

    const bool ok = ((FLASH->SR & FLASH_SR_ALL_ERRORS) == 0U) &&
                    (*(volatile uint32_t *)addr == word);
    if (!primask)
    {
        __enable_irq();
    }
    return ok;
}
