# Game Creation Guide

How to create a new game for the GameConsole platform.

A game is a standalone `.bin` loaded from the SD card and run **unprivileged**, isolated from the console by the MPU. It reaches the console only through **SVC syscalls** — there is no shared struct and no direct linkage to console code. See `docu/kernel.md` for how that isolation works; this guide is the practical how-to.

## Steps

### 1. Use the provided linker script

In your game's Makefile, set the linker script to `../game.ld`. It places the game at its `GAME_RAM` addresses and reserves the CCM asset arena. The whole image is RAM-resident — no LMA copy.

### 2. Link the syscall stub library

The console API is a set of strongly-typed C functions whose bodies are tiny SVC trampolines. Compile the shared stub library into your game and add the include paths (see GameXO's Makefile):

```make
C_SOURCES   += ../Shared/Syscall/console_syscalls.c
C_INCLUDES  += -I../Shared/Api -I../Shared/Syscall
```

Then include the umbrella header, which pulls in the typed prototypes and the shared data types (renderer/font/asset/settings/joystick):

```c
#include "game_console_api.h"
```

### 3. Call the console directly

Every call is a normal, type-checked function — the trap is hidden inside it. There is **no** runtime handshake: the loader already validated the binary's magic and ABI version before running it.

```c
gameLog("score %d", score);              // logs on the GAME channel over SWO
rendererSubmitLayer(LAYER_UI, ui, n);
rendererRender();
if (joystickGetSpecialBtn2()) { ... }     // Special Button 2 = quit, by convention
```

> Game-local `printf()` is unreliable (no backing `_write`); use `gameLog()`.

You run unprivileged and MPU-confined to **GAME_RAM** (32 KB: code + data + stack) and the **CCM asset arena** (64 KB, data only). Touching anything else — console RAM, peripherals, flash — traps as a recoverable fault that drops you back to the menu.

### 4. Provide `startup.s` (entry + binary header)

The game's `startup.s` does two things: it emits the 12-byte binary header at offset 0 of the `.bin`, and it defines `_game_start`, the entry trampoline that zeroes `.bss`, runs constructors, calls `main()`, and traps back out with `gameExit()`. The header constants come straight from the shared ABI header, so nothing is duplicated. This file is the same for every game — copy it verbatim:

```asm
/* startup.s — game entry + binary header. Same for every game. */
#include "header_interface.h"   /* GAME_BINARY_MAGIC, CONSOLE_ABI_VERSION (asm-safe) */

    .syntax unified
    .cpu cortex-m4
    .fpu softvfp
    .thumb

/* ---- Game binary header: the first 12 bytes of the .bin ----
 * Linked first in GAME_RAM by common.ld. The loader reads magic + ABI version to
 * accept the game, then jumps to entry_point. */
    .section .game_header, "a", %progbits
    .p2align 2
    .global game_binary_header
game_binary_header:
    .word GAME_BINARY_MAGIC      /* "GAME" */
    .word CONSOLE_ABI_VERSION    /* must match the console's ABI */
    .word _game_start            /* entry (linker sets the Thumb bit) */
    .size game_binary_header, .-game_binary_header

/* ---- Entry trampoline ---- */
    .global _game_start
    .section .text._game_start, "ax", %progbits
    .type _game_start, %function
_game_start:
    push {lr}

    /* Zero .bss (NOLOAD: not in the .bin), exactly as a reset handler would. */
    ldr r0, =_sbss
    ldr r1, =_ebss
    movs r2, #0
zero_bss_loop:
    cmp r0, r1
    bcs zero_bss_done
    str r2, [r0], #4
    b zero_bss_loop
zero_bss_done:

    bl __libc_init_array         /* static constructors */
    bl main                      /* the game */
    bl gameExit                  /* trap back to the console (unprivileged: can't just return) */
    .size _game_start, .-_game_start
```

Because the file is assembled with `gcc -x assembler-with-cpp`, the `#include` and the `GAME_BINARY_MAGIC` / `CONSOLE_ABI_VERSION` defines resolve through the C preprocessor — make sure `-I../Shared/Api` and `-I../Shared/Syscall` are on the assembler's flags (they are if `ASFLAGS`/the `.s` rule use `CFLAGS`, as in GameXO).

### 5. Add assets

Author assets with the desktop tools and bundle them into a `.pak`:

- [Pixel Forge](../tools/graphics/README.md) exports `GfxAsset` `.bin`/`.c` graphics (2bpp/4bpp, console palette).
- [Music Creator](../tools/music_creator/README.md) exports buzzer tracks as interleaved `(freq, ms)` `.bin`/`.c`.
- The [Asset Packer](../tools/packer/README.md) bundles the loose `.bin` files from a YAML manifest into one `<name>.pak` container plus a generated `<name>AssetEnum.h` of asset IDs.

At runtime, stream an asset by ID into a buffer carved from the CCM asset arena:

```c
AssetMetaData meta;
assetLoaderGetAssetMetadata(ASSET_ID_HERO, &meta);
uint8_t buffer[meta.size];
if (assetLoaderGetAssetData(ASSET_ID_HERO, buffer, sizeof(buffer)) == 0U)
{
    // buffer now holds the asset blob (CRC32-verified by the loader)
}
```

> **Pak naming:** the console binds a game's assets automatically — it opens `<game>.pak` (same base name as `<game>.bin`) when the game starts, so the two files must share a name and both live in the SD card root. A game shipping no assets simply omits the `.pak`. The asset calls return `0` (`ASSET_LOADER_RET_OK`) on success; see `Console/Inc/Loader/asset_loader.h` for the return codes.

### 6. Build and deploy

Build with the provided Makefile and copy the resulting `.bin` and its matching `.pak` to the SD card root. `make deploy` copies the built `.bin` to the SD mount point configured in `common.mk`.

## Minimal main.c example

```c
#include "game_console_api.h"

int main(void)
{
    // No handshake needed — the console validated this binary before loading it.
    gameLog("Hello from my game!");

    while (true)
    {
        // update + render here
        if (joystickGetSpecialBtn2())
        {
            break;  // Special Button 2 returns to the console
        }
    }
    return 0;  // _game_start calls gameExit() for you
}
```

The matching `startup.s` (above) supplies `_game_start` and the binary header — no game-binary boilerplate lives in C anymore.

## Reference

- [Kernel / game isolation](kernel.md) — privilege model, the syscall ABI, MPU protection, fault recovery
- [ConsoleAPI reference](API_README.md)
- [Memory layout](memory.md)
- [Example game source](../GameXO/)
