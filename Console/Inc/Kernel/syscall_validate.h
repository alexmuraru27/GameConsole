#ifndef __KERNEL_SYSCALL_VALIDATE_H
#define __KERNEL_SYSCALL_VALIDATE_H

#include <stdbool.h>
#include <stdint.h>
#include "renderer_interface.h" /* Sprite */

/*
 * Pointer validation — the confused-deputy defense for the syscall dispatcher.
 * A game passes pointers into its own memory; the privileged dispatcher must
 * confirm a range lies wholly inside something the game owns before dereferencing
 * it on the game's behalf, or a malicious game could make the console read/write
 * console RAM, peripherals, etc. through a syscall. Kept in its own unit so the
 * security boundary can be read (and tested) without the 300-line dispatch switch.
 *
 *   write target: GAME_RAM or the CCM asset arena (both RW for the game).
 *   read source : the above plus console flash — sprite pixels/palettes may point
 *                 at built-in font glyphs in flash, which the renderer reads. Flash
 *                 is contiguous and side-effect-free, so a bounded over-read there
 *                 cannot fault the kernel or touch live state.
 */

/* True if [p, p+len) lies wholly within game-writable memory (len 0 => true). */
bool gameCanWrite(const void *p, uint32_t len);

/* True if [p, p+len) lies wholly within game-readable memory (writable + flash). */
bool gameCanRead(const void *p, uint32_t len);

/* Copy a NUL-terminated game string into `dst` (bounded by dst_size), validating
 * each byte is game-readable first. Refuses (false) if the string walks outside the
 * game's memory; leaves `dst` NUL-terminated on success (truncating if too long). */
bool gameCopyStringIn(char *dst, uint32_t dst_size, const char *src);

/* True if the sprite array and every sprite's pixel + palette data lie in readable
 * game-accessible memory (safe for the renderer to composite). */
bool spritesValid(const Sprite *sprites, uint16_t count);

#endif /* __KERNEL_SYSCALL_VALIDATE_H */
