# The Renderer

This document is the full, ground-up explanation of the GameConsole renderer:
what it is, how a frame flows through it, how the inner pixel loop works, and how
the compositor is built for speed on a fully-painted screen. If you have never
seen this code before, read it top to bottom; by the end you should understand
not just *what* the code does but *why* each decision is made.

- **Source:** [`Console/Src/Renderer/renderer.c`](../Console/Src/Renderer/renderer.c)
  (the per-frame compositor + z-sort),
  [`Console/Src/Renderer/renderer_text.c`](../Console/Src/Renderer/renderer_text.c)
  (console-side text),
  [`Console/Src/Renderer/renderer_palette.c`](../Console/Src/Renderer/renderer_palette.c)
  (the Pixel Forge system palette),
  [`Console/Inc/Renderer/renderer.h`](../Console/Inc/Renderer/renderer.h)
- **Model shared with games:** [`Shared/Api/renderer_interface.h`](../Shared/Api/renderer_interface.h)
- **Display driver it feeds:** [`Console/Src/Devices/ILI9341.c`](../Console/Src/Devices/ILI9341.c)
- **Hardware:** STM32F407VET6 (Cortex-M4F @ 168 MHz), ILI9341 320×240 over FSMC

---

## Table of contents

1. [What the renderer is](#1-what-the-renderer-is)
2. [The public API](#2-the-public-api)
3. [Pixel formats: 2bpp / 4bpp packed tiles](#3-pixel-formats-2bpp--4bpp-packed-tiles)
4. [Why scanline chunks (and not a framebuffer)](#4-why-scanline-chunks-and-not-a-framebuffer)
5. [The frame pipeline](#5-the-frame-pipeline)
6. [Stage by stage](#6-stage-by-stage)
7. [Inside the compositor (the hot loop)](#7-inside-the-compositor-the-hot-loop)
8. [How to profile it yourself](#8-how-to-profile-it-yourself)
9. [Using the renderer](#9-using-the-renderer)
10. [Limitations & future work](#10-limitations--future-work)
11. [Glossary / quick reference](#11-glossary--quick-reference)

---

## 1. What the renderer is

The renderer is a **scanline sprite compositor** with a carefully tuned hot
path. Games describe a frame as arrays of **sprites** — small indexed-color
images with a position, a size, a draw order (`z`), and a palette. The renderer
sorts them, paints them into the 320×240 screen using the *painter's algorithm*
(back sprites first, front sprites last), and streams the result to the LCD.

It is deliberately *not* a NES-style tile engine (pattern tables, name tables,
attribute tables, OAM). It is a direct, flat sprite list. That makes it simple to
reason about and straightforward to make fast: the per-pixel inner loops (§7) are
where nearly all the frame time goes, so that is where the design invests.

```
   GAME                              RENDERER                         SCREEN
 ┌────────┐  rendererSubmitLayer() ┌───────────────────────┐  DMA   ┌──────────┐
 │ sprite │ ─────────────────────► │  sort → bin → composite│ ─────► │ ILI9341  │
 │ arrays │                        │      (this file)       │        │ 320×240  │
 └────────┘  rendererRender()      └───────────────────────┘        └──────────┘
```

Key facts:

| Property            | Value                                            |
| ------------------- | ------------------------------------------------ |
| Screen              | 320 × 240, RGB565 (16-bit color)                 |
| Sprite formats      | 2bpp (4 colors) and 4bpp (16 colors), indexed    |
| Transparency        | palette index `0` is transparent                 |
| Layers              | `LAYER_BG`, `LAYER_FG`, `LAYER_UI` (back→front)  |
| Ordering            | by `z` ascending within a layer, then by layer   |
| Output path         | 16-line scanline strips, double-buffered, DMA    |
| Frame cost          | composite-bound; the pixel-pushing DMA is fully hidden behind compositing (§6.5) |

The renderer TU (`renderer.c`) is compiled `-O3` even in debug builds (a
file-level `#pragma GCC optimize("O3")`) while the rest of the firmware stays at
`-Og` for debuggability — it is the per-frame hot path, so the inner loops must
be properly scheduled.

---

## 2. The public API

The surface lives in [`renderer.h`](../Console/Inc/Renderer/renderer.h); the
sprite model it shares with games is in
[`renderer_interface.h`](../Shared/Api/renderer_interface.h):

```c
#define RENDERER_WIDTH  320U
#define RENDERER_HEIGHT 240U

typedef enum {
    /* Bits 0-1 mirror the Pixel Forge asset (gfx_asset.h): format + opacity. */
    SPRITE_IS_FMT_4BPP = (1U << 0),  // pixel format: 0 = 2bpp, 1 = 4bpp
    SPRITE_OPAQUE      = (1U << 1),  // no transparent pixels: skip the index==0 test
    /* Bits 2+ are set by game logic. */
    SPRITE_FLIP_H      = (1U << 2),
    SPRITE_FLIP_V      = (1U << 3),
} SpriteFlags;

typedef enum { LAYER_BG = 0, LAYER_FG = 1, LAYER_UI = 2, LAYER_COUNT = 3 } Layer;

typedef struct {
    int16_t  x, y;          // top-left on screen (signed: may be off-screen)
    uint16_t w, h;          // size in pixels
    uint8_t  z;             // draw order within the layer (0 = back)
    uint8_t  flags;         // SpriteFlags bitmask (format in bit 0, opacity in bit 1)
    uint16_t id;            // free field (fills the byte reclaimed by folding format into flags)
    const uint8_t  *pixels; // packed 2bpp/4bpp pixel data
    const uint16_t *palette;// RGB565 colors; index 0 is transparent
} Sprite;
```

The **pixel format is a flag bit**, not a separate field: `SPRITE_IS_FMT_4BPP`
(bit 0) clear means 2bpp, set means 4bpp. Bits 0–1 are laid out to match the
Pixel Forge `GfxAsset` header, so a loaded asset's flags byte drops straight into
a sprite's `flags`.

### Functions

Game-facing (also reachable console-side, and exposed to games through the
`ConsoleAPI` / SVC syscalls):

```c
void     rendererClear(void);                    // start of each frame: drop every layer
void     rendererSetBackground(uint16_t color);  // RGB565 fill where no sprite draws
void     rendererSubmitLayer(Layer layer, const Sprite *sprites, uint16_t count);
void     rendererDrawText(Layer layer, int16_t x, int16_t y, uint8_t z,
                          FontSize font, uint8_t scale, uint16_t color, const char *text);
void     rendererRender(void);                   // sort + bin + composite + DMA
uint16_t rendererGetWidthPixels(void);
uint16_t rendererGetHeightPixels(void);
uint16_t rendererSystemColor(uint8_t system_index); // Pixel Forge system index (0-63) → RGB565
```

Console-only (never a game syscall):

```c
void rendererInit(void);        // once, at boot (devicesInit): build tables + clear buffers
void rendererResetState(void);  // per game launch: drop layers + disable background
void rendererGetBackground(bool *enabled, uint16_t *color);   // snapshot for OS modals
void rendererSetBackgroundState(bool enabled, uint16_t color);// restore for OS modals
```

`rendererResetState()` is called by the kernel when it launches a game, so a game
starts from a clean slate and never inherits the menu's background if it forgets
to set its own. `rendererGetBackground`/`rendererSetBackgroundState` let a
full-screen OS modal (the `osTextInput` keyboard) borrow the screen over a
running game, paint its own backdrop, then hand the background state back
untouched.

A frame is always the same three steps: **clear → submit every layer → render.**

```c
rendererClear();
rendererSubmitLayer(LAYER_BG, bg_sprites, bg_count);
rendererSubmitLayer(LAYER_FG, fg_sprites, fg_count);
rendererDrawText(LAYER_UI, 4, 4, 10, FONT_5x7, 1, 0xFFFF, "SCORE 42");
rendererRender();
```

`rendererSubmitLayer` **borrows** each layer's array — it stores the base pointer
and count, nothing is copied. The submitted `Sprite` array (and every `pixels` /
`palette` it points at) must stay valid until `rendererRender()` returns.

### Console-side text: `rendererDrawText`

Text is one syscall, not a sprite-per-glyph array built by the caller.
`rendererDrawText` (implemented in
[`renderer_text.c`](../Console/Src/Renderer/renderer_text.c)) expands a whole
string into one glyph `Sprite` per printable character on a console scratch pool
(`s_text_sprites`, capacity `TEXT_SPRITE_CAP` = 256), each tagged with its layer
and tinted one RGB565 color. The z-sort (§6.2) folds those glyph sprites into the
matching layer's run alongside the game's submitted sprites, so they z-order
naturally. Glyphs are drawn from the built-in fonts; `scale` is an integer, `1` =
native, clamped to `RENDERER_TEXT_MAX_SCALE` (= 4). Scaled glyph bitmaps are
unpacked once by `fontScale` into a small cross-frame cache (`GLYPH_CACHE_SLOTS` =
10) and reused; scale 1 reads the glyph straight out of flash. Alignment stays
caller-side — the pen advances `(fontGlyphW(font) + 1) * scale` per character.
Like sprites, text is re-issued every frame (the text pool is cleared each
`rendererClear` / `rendererRender`). This is the one text path everywhere: all
three apps and the console menus route through it.

### The system palette: `rendererSystemColor`

Pixel Forge stores palette entries as indices into a fixed 64-color **system
palette**; a game turns a loaded `GfxAsset`'s index palette into a render-ready
RGB565 palette by mapping each index through `rendererSystemColor(index)`. The
table lives in [`renderer_palette.c`](../Console/Src/Renderer/renderer_palette.c)
as pure data (`s_system_palette[64]`), split out of the `-O3` renderer TU because
it is referenced only by this cold lookup.

---

## 3. Pixel formats: 2bpp / 4bpp packed tiles

Sprites store **palette indices**, not colors. This is how a 16×16 tile costs
64 bytes (2bpp) instead of 512 bytes (raw RGB565) — an 8× saving that matters a
lot on a 192 KB-RAM MCU. The formats are **packed (chunky)**: the pixel bits are
packed left-to-right *within each byte*, not spread across bit-planes.

### 2bpp (4 colors)

Each pixel is 2 bits, so **one byte holds four pixels**, packed most-significant
pair first (left to right):

```
 byte b:   bit7 bit6 | bit5 bit4 | bit3 bit2 | bit1 bit0
            └ px0 ┘     └ px1 ┘     └ px2 ┘     └ px3 ┘
                       (px0 is the leftmost pixel)

 index(px0) = (b >> 6) & 3      index(px2) = (b >> 2) & 3
 index(px1) = (b >> 4) & 3      index(px3) = (b     ) & 3
```

A row of `w` pixels uses `(w + 3) / 4` bytes (rounded up). The color is
`palette[index]`, except **index 0 means "transparent"** — those pixels are
skipped so whatever is behind shows through.

### 4bpp (16 colors)

Each pixel is 4 bits (a nibble), so **one byte holds two pixels**, high nibble
first:

```
 byte b:   bit7..bit4 | bit3..bit0
            └  px0  ┘    └  px1  ┘

 index(px0) = b >> 4        index(px1) = b & 0x0F
```

A row uses `(w + 1) / 2` bytes. Again index 0 is transparent.

> These formats are produced by **Pixel Forge** (`.bin`/`.c` exports) — see
> [game_template/README.md](game_template/README.md) and [tools/graphics/README.md](../tools/graphics/README.md).

---

## 4. Why scanline chunks (and not a framebuffer)

The obvious design is a full framebuffer: 320 × 240 × 2 bytes = **153,600 bytes**.
The STM32F407 has 128 KB of regular SRAM (plus 64 KB CCM that **DMA cannot
reach**). A full RGB565 framebuffer simply does not fit, and even if it did it
would swallow most of RAM.

So the renderer works in **horizontal chunks of `RENDERER_SCANLINES` rows** (16):

```
         320 px wide
   ┌────────────────────┐ y=0
   │      chunk 0        │ 16 rows  ── composite ──► s_scanline_buffer[0] ──DMA──►┐
   ├────────────────────┤ y=16                                                     │
   │      chunk 1        │ 16 rows  ── composite ──► s_scanline_buffer[1] ──DMA──► │ ILI9341
   ├────────────────────┤ y=32                                                     │
   │        ...          │                                                         │
   ├────────────────────┤ y=224                                                    │
   │      chunk 14       │ 16 rows                                                  │
   └────────────────────┘ y=240
       15 chunks = ceil(240 / 16)
```

Each chunk buffer is `16 × 320 × 2 = 10,240 bytes`; there are **two** of them
(20,480 bytes total) for double-buffering. This is the central memory/bandwidth
trade: we give up a persistent framebuffer and in return rebuild each strip from
the sprite list every frame.

```c
#define RENDERER_SCANLINES 16U
static uint16_t s_scanline_buffer[2U][RENDERER_SCANLINES * RENDERER_WIDTH] __attribute__((aligned(4)));
```

> **Chunk height is a RAM/FPS knob.** Smaller chunks shrink the scanline buffer
> (the renderer's only N-dependent RAM), so RAM falls monotonically as you lower
> `RENDERER_SCANLINES`. The cost is FPS: there are more strips, and each one has a
> fixed per-chunk overhead (its background fill and bin walk), so smaller chunks
> raise it. `16` is the chosen balance.

The buffers are **4-byte aligned** on purpose — the opaque blitter writes two
pixels at a time with a 32-bit store (see §7.4), which requires word alignment.

---

## 5. The frame pipeline

`rendererRender()` runs three stages — sort, bin, then a composite+DMA loop:

```
 ┌─────────────────────────────────────────────────────────────────────────┐
 │ rendererRender()                                                         │
 │                                                                          │
 │  1. sortSpritesByZ()        counting sort each layer by z (ascending),   │
 │                             folding in that layer's console-text glyphs   │
 │  2. binSpritesIntoChunks()  append each sprite to every chunk it spans,  │
 │                             in draw order (BG→FG→UI, z-ascending)         │
 │                                                                          │
 │  3. for chunk = 0..14:                                                   │
 │       renderScanlineChunk()   ── composite this chunk's bin into a back  │
 │                                  buffer (no reject; the bin already fits) │
 │       ili9341DrawScanlines()  ── DMA the strip to the LCD                 │
 │                                                                          │
 └─────────────────────────────────────────────────────────────────────────┘
```

The submit-side (`rendererClear` / `rendererSubmitLayer`) just borrows each
layer's sprite array pointer + count. All the work is in `render`.

---

## 6. Stage by stage

### 6.1 Submit

```c
void rendererSubmitLayer(Layer layer, const Sprite *sprites, uint16_t count) {
    if (layer >= LAYER_COUNT) return;
    uint16_t avail = MAX_SPRITES_TOTAL - (the other layers' active counts);
    if (count > avail) count = avail;   // silently clamp on overflow
    s_layer_sprites[layer] = sprites;   // borrow the game-owned array
    s_active_sprites[layer] = count;
}
```

A layer arrives as one game-owned array, borrowed (pointer + count) until
`rendererRender()` returns — nothing is copied. The three layers share a single
**`MAX_SPRITES_TOTAL` = 1050** sprite budget instead of fixed per-layer
capacities: a frame may split it any way it likes (1050 background sprites and
nothing else is fine); only the total is capped. A submission that would push the
total past the budget is clamped to what is left — resubmitting a layer releases
its previous share first. Overflow is dropped rather than asserted — a missing
sprite is a gentler failure than a crash on an embedded console.

### 6.2 Counting sort by `z`

Within each layer, sprites must be drawn in `z` order (low z = behind). `z` is a
`uint8_t`, so the natural choice is a **counting sort** — O(n) with a 256-bucket
histogram, no comparisons:

```c
uint16_t z_count[256];
memset(z_count, 0, sizeof(z_count));
for (i) z_count[sprite[i].z]++;          // 1) histogram (game sprites + this layer's text glyphs)
prefix-sum z_count into start offsets;   // 2) exclusive prefix sum, based at the run's start
for (i) s_sorted[z_count[z]++] = &sprite[i]; // 3) scatter (stable)
```

The three layers scatter into one **flat pool** (`s_sorted`), each layer's run
starting where the previous layer's ended (the prefix sum is based at the run's
start offset). Console-text glyphs (§2) tagged to a layer are histogrammed and
scattered with that layer's game sprites, *after* them at equal `z`, so same-z
text lands on top. After the last layer, `s_sorted[0..total)` is the frame's
complete painter order: ascending `z` within each layer, layers in `BG → FG → UI`
order. The sort is **stable**, so sprites with equal `z` keep submission order.

### 6.3 Selecting a chunk's sprites: per-chunk bins

Once the layers are z-sorted, `binSpritesIntoChunks()` distributes the sprites
into one list per chunk — appending each sprite to every chunk its vertical
extent covers:

```c
for (i, sprite in s_sorted[0 .. total])    // the pool is already in draw order
    first = clamp(sprite->y)            / RENDERER_SCANLINES;
    last  = (sprite->y + sprite->h - 1) / RENDERER_SCANLINES;
    for (c = first; c <= last; c++)
        if (s_chunk_bin_count[c] < CHUNK_BIN_CAP)
            s_chunk_bin[c][s_chunk_bin_count[c]++] = i;  // append the pool index
```

Because the bins are filled **in draw order** — layers `BG → FG → UI`, ascending
z within each — every chunk's bin already holds exactly the sprites it must paint,
in the order it must paint them (`BG(low z) → BG(high z) → FG → UI`).
`renderScanlineChunk()` then just walks its own bin, with **no vertical reject and
no sprites that miss the strip**:

```c
const uint16_t *bin = s_chunk_bin[start_y / RENDERER_SCANLINES];  // pool indices
for (i in 0 .. s_chunk_bin_count[chunk])
    composite s_sorted[bin[i]] into this strip;   // guaranteed to overlap
```

The bins are sized by sprite **count**, not height: `s_chunk_bin[NUM_CHUNKS]
[CHUNK_BIN_CAP]`. A tall sprite simply lands in more chunks' bins (one 2-byte
index each), never overflowing a height-based bound. A chunk that would exceed
`CHUNK_BIN_CAP` (200, far above any realistic per-chunk count) drops the extra
sprites rather than corrupting memory. Entries are `uint16_t` z-sort-pool indices,
not `Sprite*` — half the RAM of a pointer bin for one extra `s_sorted[]` load per
sprite per chunk. Cost: `15 chunks × 200 × 2 B = 6,000 B`.

### 6.4 Per-chunk compositing

```c
for (chunk_index = 0; scanline_y < 240; chunk_index++) {
    buffer_index = chunk_index & 1;                       // ping-pong A/B
    renderScanlineChunk(s_scanline_buffer[buffer_index], scanline_y, lines);
    ili9341DrawScanlines(lines, scanline_y, lines * 320, s_scanline_buffer[buffer_index]);
    scanline_y += lines;
}
```

`renderScanlineChunk` paints every sprite in the chunk into the strip buffer,
back-to-front (optionally over a background fill first). Then
`ili9341DrawScanlines` hands the strip to DMA.

### 6.5 Double-buffered DMA overlap

This is why there are *two* scanline buffers. While DMA streams chunk *N* out of
buffer A, the CPU is already compositing chunk *N+1* into buffer B. The display
driver only blocks (waits for the previous DMA to finish) at the *start* of the
next transfer, by which time it is usually already done.

```
 time ────────────────────────────────────────────────────────────►
 CPU:  [compose c0]   [compose c1]   [compose c2]   [compose c3]
 DMA:               [── DMA c0 ──] [── DMA c1 ──] [── DMA c2 ──]
        buffer A=c0/c2 (even),  buffer B=c1/c3 (odd) — never the one in flight
```

Compositing a busy strip takes far longer than its DMA, so the DMA is essentially
**free** — fully hidden behind the CPU work. The FSMC write timing (≈36 ns/pixel)
gives a ~2.7 ms full-screen floor, far below the composite cost of a busy scene.

### 6.6 The decoded-tile cache

Unpacking 2bpp/4bpp + a palette lookup *per pixel* is wasted work when the same
tile is drawn many times — the result is always identical. So opaque, unflipped
tiles up to 16×16 are decoded to a **ready-made RGB565 buffer once per frame**,
keyed by `(pixels, palette, w, h)`; every later instance just **copies rows**.

```c
// renderScanlineChunk, per opaque sprite:
const uint16_t *rgb = tileCacheLookup(sprite, is_4bpp); // decode-on-first-use
if (rgb) compositeCachedOpaque(..., rgb);               // word-copy each row
else     composite2bppOpaque / composite4bpp(...);      // miss → normal unpack
```

- **Decode-on-first-use.** The first time a `(pixels, palette, size)` is seen this
  frame it is unpacked into the next free slot (`TILE_CACHE_SLOTS = 8`); later hits
  return the slot. A miss — flipped, larger than 16×16 (`TILE_CACHE_MAX_PX = 256`),
  or the cache is full — takes the normal compositor, so correctness never depends
  on the cache.
- **Reset every frame.** A palette's *contents* can change under the same pointer
  (animation), so slots are dropped at the top of `rendererRender()`.
- **The copy is hand-rolled.** newlib-nano's `memcpy` is a **byte** loop, so
  `compositeCachedOpaque` uses a word-copy loop instead (two pixels per 32-bit
  store, taken only when the tile width is even so source alignment is stable
  across rows; the odd/misaligned case falls back to a per-pixel copy).

Once decoded, 2bpp and 4bpp are the same RGB565 row copy, so the format no longer
matters — the gain scales with reuse. A tile drawn only once pays decode + copy
for no benefit (see §10). **RAM: 4.1 KB** (`TILE_CACHE_SLOTS = 8`, each 524 B).

---

## 7. Inside the compositor (the hot loop)

This is where nearly all the frame time goes. The structure is three layers of
function, from outer to inner:

```
 renderScanlineChunk(chunk)
   └─ for each sprite in chunk's bin:
        opaque + cacheable? → compositeCachedOpaque(sprite, rgb)   ← row copy
        else composite2bppOpaque / composite2bppTransparent / composite4bpp
          └─ clipSprite()                                          ← clip once
          └─ for each row of the sprite in this chunk:
               blitRow2bppOpaque / blitRow2bppTransparent /
               blitRow4bpp / blitRowFlip                           ← the pixel loop
```

**How the compositor is built.** The speed of the inner loop comes from a stack
of specific techniques, each detailed below:

- **Byte-at-a-time unpack with constant shifts** — read a source byte once,
  extract its four (2bpp) or two (4bpp) pixels with fixed `>>6 >>4 >>2` shifts
  instead of a per-pixel variable shift (§7.3).
- **`-O3` on this translation unit only** via `#pragma GCC optimize("O3")`, while
  the rest of the firmware stays `-Og` for debuggability.
- **Split 2bpp/4bpp compositors and split opaque/transparent 2bpp blits** — each
  routine inlines only its own row blit, so no single function carries both a
  16-entry palette and both masking paths and spills registers in the inner loop.
- **`SPRITE_OPAQUE` fast path** — an opaque sprite skips the per-pixel index-0
  test entirely (§7.4).
- **Fixed-address palette LUT `s_lut`** — the palette is copied out of slow flash
  into a fixed-address SRAM scratch once per sprite, giving single-instruction
  per-pixel lookups (§7.5).
- **Two pixels per 32-bit store via `s_pair`** — the opaque path (and the covered
  interior of the transparent path) packs two adjacent pixels into one word and
  emits one store per two pixels (§7.4).
- **The decoded-tile cache** — reused tiles skip unpack + lookup entirely (§6.6).
- **A "byte is fully opaque" table `s_byte_full_2bpp`** — lets the transparent
  blit take the two-store pair path on interior bytes and only branch per-pixel on
  the edge bytes (§7.3).

### 7.1 Sprite-major, not scanline-major

A subtle but important choice. The naive scanline renderer loops *rows* on the
outside and *sprites* on the inside:

```c
for (row in chunk)            // ← outer
    for (sprite in chunk)     // ← inner
        draw one scanline of sprite;   // re-clips the sprite EVERY row
```

The compositor flips it — **sprite on the outside, its rows on the inside**:

```c
for (sprite in chunk)          // ← outer
    clip the sprite ONCE;
    for (row of sprite)        // ← inner
        blit that row;
```

Both produce identical pixels (it is still painter's order: an earlier sprite is
fully painted before a later one overdraws it). But the sprite-major form computes
the clip rectangle, source stride, palette table, and base pointers **once per
sprite** instead of once per scanline. For a 16-tall tile that is a 16×
reduction of the setup overhead.

### 7.2 Clip once: `clipSprite()`

`clipSprite()` intersects the sprite with the chunk and the screen, and returns a
small `SpriteClip` describing exactly what to draw:

```
            x_start         x_end
              │               │
   ┌──────────┼───────────────┼─────┐  start_y      (chunk top)
   │  (above) │  visible span │     │
   │    ┌─────┼───────────────┼──┐  │  ← sprite, may overhang the chunk
   │    │     │    sprite      │ │  │     on any side
   └────┼─────┼───────────────┼──┼──┘  start_y+count (chunk bottom)
        │     │
        │  col_left = first *source* column that is visible
        └─ off-screen-left part is skipped
```

```c
typedef struct {
    uint16_t row_top, row_bot;  // screen rows [top, bot) to draw
    uint16_t x_start;           // first screen column
    uint16_t span;              // visible width
    uint16_t col_left;          // first visible *source* column
    uint16_t src_top;           // source row for row_top (vertical flip applied here)
} SpriteClip;
```

Off-screen sprites return `false` and are skipped entirely. It guards the signed
edges (a sprite entirely off the top or left has a non-positive bottom/right that
would otherwise wrap huge when cast to `uint16_t`). Vertical flip is folded into
`src_top` + a negative row step so the inner loop never re-checks it.

### 7.3 The transparent path: `blitRow2bppTransparent`

The inner loop walks the source **one byte at a time** (four 2bpp pixels) using
*constant* shifts, and classifies each byte three ways:

```c
while (n >= 4) {
    uint8_t b = *p++;
    if (s_byte_full_2bpp[b]) {             // interior: no index-0 pixel → pair-store
        q[0] = s_pair[b >> 4];             //   two pixels, one 32-bit store
        q[1] = s_pair[b & 0x0F];           //   two pixels, one 32-bit store
    } else if (b) {                        // edge: mixed → per-pixel, skip index 0
        uint8_t i0 = (b >> 6) & 3; if (i0) d[0] = s_lut[i0];
        uint8_t i1 = (b >> 4) & 3; if (i1) d[1] = s_lut[i1];
        uint8_t i2 = (b >> 2) & 3; if (i2) d[2] = s_lut[i2];
        uint8_t i3 =  b       & 3; if (i3) d[3] = s_lut[i3];
    }                                      // b == 0: four transparent pixels → skip all four
    q += 2; d += 4; n -= 4;
}
```

The three cases cover the three ways a source byte can look:

- **Fully covered** (`s_byte_full_2bpp[b]` — none of its four indices is 0): it
  writes all four pixels through the pair-LUT at opaque speed. Sprite *interiors*
  are almost all this case, so a transparent sprite runs at opaque speed except at
  its edges.
- **Mixed** (some index-0, some not): the four `if (idx)` per-pixel tests, so the
  transparent pixels show through.
- **All transparent** (`b == 0`): a single test skips four pixels — perfect for
  text and sparse sprites, which are mostly index 0.

The byte is fetched **once** and its pixels extracted with constant shifts
(`>>6 >>4 >>2`) instead of a per-pixel `6 - ((col&3)<<1)`. Unaligned left/right
edges are handled by tiny **head** and **tail** loops that bring `col` to a
4-pixel boundary; the body then runs aligned. (A destination that is not
word-aligned uses a matching scalar body — same classification, four 16-bit
stores instead of the pair-LUT.)

### 7.4 The opaque path: `blitRow2bppOpaque` and 32-bit stores

Most of a typical scene is **fully opaque** — backgrounds, filled panels, solid
tiles. Those sprites have *no* index-0 pixels, so every per-pixel `if (idx)` test
is wasted work. Sprites declare this with `SPRITE_OPAQUE`, and the dispatch in
`renderScanlineChunk` routes 2bpp opaque sprites to `blitRow2bppOpaque` — a blit
with no transparency tests at all.

The opaque path packs two adjacent pixels into a single 32-bit word via a **pair
lookup table**, so the loop emits **one 32-bit store per two pixels** instead of
two 16-bit stores:

```
 s_lut[4]   : RGB565 color for each 2-bit index
 s_pair[16] : two pixels packed into a 32-bit word, indexed by a 4-bit nibble

   nibble k = (hi2 << 2) | lo2
   s_pair[k] = s_lut[hi2] | (s_lut[lo2] << 16)
               └ low 16b ┘   └ high 16b ┘
               (little-endian ⇒ low half lands at the lower screen x)

 opaque blit, word-aligned destination:
   byte b ─► q[0] = s_pair[b >> 4];    // pixels 0,1  → 1 store
             q[1] = s_pair[b & 0x0F];  // pixels 2,3  → 1 store
   = 4 pixels in 2 stores, no per-pixel branch
```

```c
if (((uintptr_t)d & 3U) == 0U) {           // destination word-aligned?
    uint32_t *q = (uint32_t *)d;
    while (n >= 4) {
        uint8_t b = *p++;
        q[0] = s_pair[b >> 4];
        q[1] = s_pair[b & 0x0F];
        q += 2; d += 4; n -= 4;
    }
}
```

`s_pair[]` is rebuilt (16 entries, in `build2bppPalette`) once per 2bpp palette
change — consecutive sprites sharing a palette skip the rebuild. The head loop has
already fixed alignment, so this fast path applies to any 2bpp sprite whose
visible left edge lands on a word boundary; the rare misaligned case falls back to
four scalar stores.

**4bpp is different.** 4bpp has no two-for-one store (two source pixels are a
whole byte already), so `blitRow4bpp` takes a **compile-time-constant `opaque`
argument** and is specialized by the compiler into two versions — one with the
per-pixel index-0 test, one without — with zero runtime dispatch in the loop. Its
opaque body issues two independent 16-bit stores per byte (a 32-bit combine was
measured slower: the `orr` serializes the two palette loads, while separate stores
pipeline freely through the M4 store buffer).

### 7.5 The palette lives at a fixed address: `s_lut`

Sprite palettes are `const` and therefore live in **flash**, which has wait
states. Reading `palette[idx]` *per pixel* from flash is slow. So each compositor
copies the palette into a file-scope scratch array **once per sprite** (only when
it changed since the last sprite — `s_lut_pal` / `s_pair_pal` cache the pointer):

```c
static uint16_t s_lut[16];   // fixed address, in fast SRAM
...
s_lut[0]=pal[0]; s_lut[1]=pal[1]; s_lut[2]=pal[2]; s_lut[3]=pal[3];  // 2bpp
```

It is **file-scope, not a local array**, because a fixed link-time address lets
the compiler emit a single hoisted base register and then `ldrh [base, idx, lsl #1]`
per pixel; a local array lands on the stack and the compiler recomputes
`sp + offset` per pixel. The renderer is single-threaded (one `rendererRender` at
a time), so a shared scratch is safe.

### 7.6 The flipped path: `blitRowFlip`

Horizontal flip is the only case that cannot walk the source left-to-right by
byte, so it stays a simple per-pixel loop (`blitRowFlip`, handling both 2bpp and
4bpp). Flips are uncommon and not worth the code complexity of a reversed byte
unpack. Vertical flip, by contrast, costs nothing extra — it is folded into
`clipSprite`'s `src_top` and a negative row step.

### 7.7 Where the code runs: I-cached flash, not `.RamFunc`

The hot compositors run from **flash**, leaning on the instruction cache
(`RENDERER_HOT` names their placement). The reason is **bus contention, not wait
states**: if the code ran from `.RamFunc` (SRAM), every instruction fetch would
compete with the per-pixel stores on the same SRAM across the AHB bus matrix.
Running the code from I-cached flash moves fetch onto the I-bus/I-cache and hands
the full SRAM bandwidth to the store-bound inner loop.

`RENDERER_HOT` is kept as a named attribute so placement is a one-line switch back
to `.RamFunc` if a very text-heavy workload ever wants the hot code in SRAM (the
transparent compositor is ~1 KB, ≈ the whole I-cache). This is still *not* CCM —
the Cortex-M4 cannot fetch instructions from CCM on the F4, and the scanline
buffers must stay in DMA-reachable SRAM regardless.

---

## 8. How to profile it yourself

The Cortex-M4 has a free-running cycle counter (`DWT->CYCCNT`). It is already
enabled by `swoInit()`, so you can read it directly. At 168 MHz, **168 cycles =
1 µs**. To time the whole compositor for a frame:

```c
uint32_t c0 = DWT->CYCCNT;
rendererRender();
uint32_t cyc = DWT->CYCCNT - c0;        // exact cycles for the whole frame
// ... accumulate min / max / average, print over SWO every couple of seconds
printf("render avg=%luus (%lu cyc)\n", cyc/168, cyc);
```

To measure **pure composite cost** (no display output), temporarily skip the
`ili9341DrawScanlines()` call in `rendererRender()` and compare.

The shipping benchmark is the loadable app
[`Apps/TestRenderer/`](../Apps/TestRenderer/Src/main.c) — an endless auto-scroller
that sweeps its sprite load 50%→100%→50% of the renderer's shared 1050-sprite
budget (filled as 3 × 350) with a triangle wave and draws live current/min/avg/max
FPS plus the load % on screen, running uncapped so you can watch throughput track
the load. It paces its scroll by `getDeltaTimeUs()` so speed stays constant even
as the frame rate swings.

Workflow on the bench:

```bash
make -C Apps/TestRenderer all   # build the benchmark app; deploy it to the SD Games/ folder
make flashswo                   # flash the console + start SWO trace
```

> Tip: read the actual disassembly when a change does not move the needle —
> `arm-none-eabi-objdump -d build/Console/renderer.o`. Register spills and
> stack-relative palette addressing are only obvious in the generated code.

---

## 9. Using the renderer

A minimal game frame:

```c
/* palette: index 0 transparent, then up to 3 (2bpp) or 15 (4bpp) colors */
static const uint16_t pal[4] = { 0x0000, 0xF800, 0x07E0, 0xFFFF };

/* a 16×16 2bpp tile, 64 bytes of packed indices */
static const uint8_t tile[ (16*16)/4 ] = { /* ... */ };

static Sprite bg[1];

/* per frame */
rendererClear();

bg[0] = (Sprite){
    .x = 32, .y = 48, .w = 16, .h = 16,
    .z = 0,
    .flags = SPRITE_OPAQUE,        // 2bpp (bit 0 clear), opaque; set flip bits as needed
    .pixels = tile,
    .palette = pal,
};
rendererSubmitLayer(LAYER_BG, bg, 1);
/* ... submit the other layers, draw text ... */
rendererDrawText(LAYER_UI, 4, 4, 10, FONT_5x7, 1, 0xFFFF, "SCORE 42");

rendererRender();                  // composites + streams to the LCD
```

Rules of thumb:
- **The pixel format is `flags` bit 0.** Leave it clear for 2bpp, set
  `SPRITE_IS_FMT_4BPP` for 4bpp. There is no separate `format` field.
- **Set `SPRITE_OPAQUE` only when the sprite truly has no transparent pixels.**
  It is a promise: the fast path does not test for index 0, so a stray transparent
  pixel is drawn as `palette[0]` instead of showing through.
- Use `z` to order within a layer; use layers for coarse front/back grouping
  (UI always on top of FG, FG always on top of BG).
- The submitted array (and its `pixels`/`palette`) must stay valid until
  `rendererRender()` returns — the renderer borrows it, it does not copy.
- The three layers share one `MAX_SPRITES_TOTAL` (1050) sprite budget;
  submitting past what the other layers left of it silently clamps the extras.
- Draw all text with `rendererDrawText` — one call per string, no per-glyph
  sprite array of your own.

---

## 10. Limitations & future work

### Current limitations

- **No clipping rectangle / no rotation / no scaling.** Sprites are axis-aligned,
  1:1, full-screen-clipped only. This is intentional simplicity.
- **Capacity is fixed at compile time** (`MAX_SPRITES_TOTAL`, `CHUNK_BIN_CAP`,
  `TEXT_SPRITE_CAP`). Overflow drops sprites/glyphs.
- **`s_lut` / `s_pair` / the per-chunk bins / the tile cache are shared scratch** —
  correct only because rendering is single-threaded and non-reentrant. Do not call
  the renderer from an ISR.

### Shipping optimizations

Two content-adaptive optimizations sit alongside the per-instruction blits:

1. **Per-chunk binning (§6.3).** Every sprite is appended once to each chunk it
   spans, in draw order, so a chunk composites only the sprites that actually
   touch it — no per-chunk vertical reject scan. Content-independent; sized by
   sprite count, capped per chunk. **RAM: 6 KB** (`15 chunks × 200 × 2 B`).

2. **Decoded-tile cache (§6.6).** Each opaque, unflipped, ≤16×16 tile is unpacked
   to a ready-made RGB565 buffer once per frame (keyed by `pixels` + `palette` +
   size); every later instance composites with a plain word-copy. Once decoded,
   4bpp is as cheap as 2bpp. The win scales with **tile reuse** — a tile drawn
   once costs decode + copy, so low-reuse scenes gain nothing. **RAM: 4.1 KB**
   (`TILE_CACHE_SLOTS = 8`, each ≤16×16).

### Future work

- **Front-to-back + occlusion — the biggest theoretical win, and the riskiest.**
  The design is pure painter's algorithm: a pixel hidden behind an opaque sprite
  is still composited then overwritten. Drawing front-to-back with a per-scanline
  **coverage mask** would skip occluded spans. **RAM:** ~0 (a coverage bitmap per
  strip). **Caveats:** the optimized blits are so cheap that the coverage
  bookkeeping only pays back at high overdraw — it can *lose* on lightly layered
  scenes — and it is the most invasive change to the core loop.

---

## 11. Glossary / quick reference

| Term | Meaning |
| ---- | ------- |
| **Chunk** | A horizontal strip of `RENDERER_SCANLINES` (16) scanlines; the unit of compositing + DMA. |
| **Layer** | Coarse front/back grouping: `BG < FG < UI`. |
| **`z`** | Fine draw order within a layer; low = behind. |
| **2bpp / 4bpp** | 2 or 4 bits per pixel (packed within each byte); 4 or 16 palette indices. Format is `flags` bit 0 (`SPRITE_IS_FMT_4BPP`). |
| **Index 0** | Always transparent. |
| **`SPRITE_OPAQUE`** | Promise that a sprite has no index-0 pixels → branch-free fast blit. |
| **`s_lut`** | Fixed-address SRAM copy of the active palette (fast per-pixel lookup). |
| **`s_pair`** | 16-entry table packing two adjacent 2bpp pixels into one 32-bit word. |
| **`s_byte_full_2bpp`** | 256-entry table: is a 2bpp source byte fully opaque (no index-0 pixel)? Lets the transparent blit fast-path covered interior bytes. |
| **Per-chunk bin** | `s_chunk_bin[chunk][]` — z-sort-pool indices of the sprites that touch a chunk, in draw order, built once per frame. |
| **Tile cache** | `s_tile_cache[]` — opaque tiles decoded to RGB565 once per frame, keyed by `(pixels, palette, size)`; instances blit by row copy. |
| **`RENDERER_HOT`** | Attribute naming the hot compositors' placement (I-cached flash). |
| **`.RamFunc`** | Linker section placing functions in SRAM for execution — the fallback `RENDERER_HOT` could switch to, unused by default. |
| **Painter's algorithm** | Draw back-to-front; later sprites overwrite earlier ones. |

| Constant | Value | Where |
| -------- | ----- | ----- |
| `RENDERER_WIDTH` × `RENDERER_HEIGHT` | 320 × 240 | `renderer.h` / `renderer_interface.h` |
| `RENDERER_SCANLINES` | 16 (chunk height; 15 strips/frame) | `renderer.c` |
| `MAX_SPRITES_TOTAL` | 1050 (one budget shared by the three layers) | `renderer.c` |
| `CHUNK_BIN_CAP` | 200 (per-chunk sprite cap) | `renderer.c` |
| `TILE_CACHE_SLOTS` | 8 (decoded opaque tiles, ≤16×16) | `renderer.c` |
| `TEXT_SPRITE_CAP` | 256 (console-text glyphs per frame) | `renderer_internal.h` |
| `RENDERER_TEXT_MAX_SCALE` | 4 (max integer text scale) | `renderer.h` |
| Scanline buffers | 2 × 16 × 320 × 2 B = 20,480 B | `renderer.c` |
| Per-chunk bins | 15 × 200 × 2 B = 6,000 B | `renderer.c` |
| Z-sort pool | (1050 + 256) × 4 B = 5,224 B | `renderer.c` |
| Tile cache | 8 × 524 B = 4,192 B | `renderer.c` |

---

*Profiling on this platform uses `DWT->CYCCNT` on the STM32F407VET6 at 168 MHz;
the shipping renderer benchmark is the loadable app `Apps/TestRenderer/`, which
draws live FPS on screen as it sweeps its sprite load.*
