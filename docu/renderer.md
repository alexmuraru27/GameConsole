# The Renderer

This document is the full, ground-up explanation of the GameConsole renderer:
what it is, how a frame flows through it, how the inner pixel loop works, and —
in detail — every optimization that took it from **24 FPS to 76 FPS** on a busy,
fully-painted screen. If you have never seen this code before, read it top to
bottom; by the end you should understand not just *what* the code does but *why*
each decision was made.

- **Source:** [`Console/Src/Renderer/renderer.c`](../Console/Src/Renderer/renderer.c),
  [`Console/Inc/Renderer/renderer.h`](../Console/Inc/Renderer/renderer.h)
- **Display driver it feeds:** [`Console/Src/Devices/ILI9341.c`](../Console/Src/Devices/ILI9341.c)
- **Hardware:** STM32F407VET6 (Cortex-M4F @ 168 MHz), ILI9341 320×240 over FSMC

---

## Table of contents

1. [What the renderer is](#1-what-the-renderer-is)
2. [The public API](#2-the-public-api)
3. [Pixel formats: 2bpp / 4bpp planar tiles](#3-pixel-formats-2bpp--4bpp-planar-tiles)
4. [Why scanline chunks (and not a framebuffer)](#4-why-scanline-chunks-and-not-a-framebuffer)
5. [The frame pipeline](#5-the-frame-pipeline)
6. [Stage by stage](#6-stage-by-stage)
7. [Inside the compositor (the hot loop)](#7-inside-the-compositor-the-hot-loop)
8. [The optimization journey: 24 → 76 FPS](#8-the-optimization-journey-24--76-fps)
9. [How to profile it yourself](#9-how-to-profile-it-yourself)
10. [Using the renderer](#10-using-the-renderer)
11. [Limitations & future work](#11-limitations--future-work)
12. [Glossary / quick reference](#12-glossary--quick-reference)

---

## 1. What the renderer is

The renderer is a **scanline sprite compositor**. Games describe a frame as a
list of **sprites** — small indexed-color images with a position, a size, a
draw order (`z`), and a palette. The renderer sorts them, paints them into the
320×240 screen using the *painter's algorithm* (back sprites first, front
sprites last), and streams the result to the LCD.

It is deliberately *not* the old NES-style tile engine (pattern tables, name
tables, attribute tables, OAM). It is a direct, flat sprite list. That makes it
simple to reason about and — as we will see — straightforward to make fast.

```
   GAME                           RENDERER                         SCREEN
 ┌────────┐   rendererSubmit()  ┌───────────────────────┐  DMA   ┌──────────┐
 │ sprite │ ──────────────────► │  sort → composite     │ ─────► │ ILI9341  │
 │  list  │                     │      (this file)      │        │ 320×240  │
 └────────┘   rendererRender()  └───────────────────────┘        └──────────┘
```

Key facts:

| Property            | Value                                            |
| ------------------- | ------------------------------------------------ |
| Screen              | 320 × 240, RGB565 (16-bit color)                 |
| Sprite formats      | 2bpp (4 colors) and 4bpp (16 colors), indexed    |
| Transparency        | palette index `0` is transparent                 |
| Layers              | `LAYER_BG`, `LAYER_FG`, `LAYER_UI` (back→front)  |
| Ordering            | by `z` ascending within a layer, then by layer   |
| Output path         | 8-line scanline strips, double-buffered, DMA     |
| Frame cost (busy)   | ~13.6 ms render at the 8-line default → fits 30 FPS with headroom |

---

## 2. The public API

The entire surface lives in [`renderer.h`](../Console/Inc/Renderer/renderer.h):

```c
#define RENDERER_WIDTH  320U
#define RENDERER_HEIGHT 240U

typedef enum {
    SPRITE_FLIP_H = (1U << 0),
    SPRITE_FLIP_V = (1U << 1),
    SPRITE_OPAQUE = (1U << 2),  // no transparent pixels: take the fast path
} SpriteFlags;

typedef enum { GFX_FMT_2BPP = 1, GFX_FMT_4BPP = 2 } GfxFormat;

typedef enum { LAYER_BG = 0, LAYER_FG = 1, LAYER_UI = 2, LAYER_COUNT = 3 } Layer;

typedef struct {
    int16_t  x, y;          // top-left on screen (signed: may be off-screen)
    uint16_t w, h;          // size in pixels
    uint8_t  z;             // draw order within the layer (0 = back)
    uint8_t  flags;         // SpriteFlags bitmask
    uint8_t  format;        // GfxFormat
    const uint8_t  *pixels; // packed 2bpp/4bpp pixel data
    const uint16_t *palette;// RGB565 colors; index 0 is transparent
} Sprite;

void     rendererInit(void);                       // once, at boot
void     rendererClear(void);                      // start of each frame
void     rendererSubmit(Layer layer, const Sprite *sprite);
void     rendererRender(void);                     // sort + composite + DMA
uint16_t rendererGetWidthPixels(void);
uint16_t rendererGetHeightPixels(void);
```

A frame is always the same three steps: **clear → submit every sprite → render.**

```c
rendererClear();
for (each visible thing) rendererSubmit(layer, &sprite);
rendererRender();
```

`rendererSubmit` *copies* the sprite into an internal per-layer array, so the
caller's `Sprite` can be a stack temporary. The `pixels` and `palette` pointers
must stay valid until `rendererRender` returns.

---

## 3. Pixel formats: 2bpp / 4bpp planar tiles

Sprites store **palette indices**, not colors. This is how a 16×16 tile costs
64 bytes (2bpp) instead of 512 bytes (raw RGB565) — an 8× saving that matters a
lot on a 192 KB-RAM MCU.

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

So the renderer works in **horizontal chunks of `RENDERER_SCANLINES` rows** (8):

```
         320 px wide
   ┌────────────────────┐ y=0
   │      chunk 0        │  8 rows  ── composite ──► s_scanline_buffer[0] ──DMA──►┐
   ├────────────────────┤ y=8                                                      │
   │      chunk 1        │  8 rows  ── composite ──► s_scanline_buffer[1] ──DMA──► │ ILI9341
   ├────────────────────┤ y=16                                                     │
   │        ...          │                                                         │
   ├────────────────────┤ y=232                                                    │
   │      chunk 29       │  8 rows                                                  │
   └────────────────────┘ y=240
       30 chunks = ceil(240 / 8)
```

Each chunk buffer is `8 × 320 × 2 = 5,120 bytes`; there are **two** of them
(10,240 bytes total) for double-buffering. This is the central memory/bandwidth
trade: we give up a persistent framebuffer and in return rebuild each strip from
the sprite list every frame.

```c
#define RENDERER_SCANLINES 8U
static uint16_t s_scanline_buffer[2U][RENDERER_SCANLINES * RENDERER_WIDTH] __attribute__((aligned(4)));
```

> **Chunk height is a RAM/FPS knob.** Smaller chunks shrink the scanline buffer
> (the renderer's only N-dependent RAM), so RAM falls monotonically as you lower
> `RENDERER_SCANLINES`. The cost is FPS: there are more strips, and each one
> re-scans the sprite lists (see §6.3), so per-chunk overhead rises. `8` is the
> chosen balance — half the buffer of 16 for a modest FPS cost.

The buffers are **4-byte aligned** on purpose — the opaque blitter writes two
pixels at a time with a 32-bit store (see §7.4), which requires word alignment.

---

## 5. The frame pipeline

`rendererRender()` runs three stages — sort, bin, then a composite+DMA loop:

```
 ┌─────────────────────────────────────────────────────────────────────────┐
 │ rendererRender()                                                         │
 │                                                                          │
 │  1. sortSpritesByZ()        counting sort each layer by z (ascending)    │
 │  2. binSpritesIntoChunks()  append each sprite to every chunk it spans,  │
 │                             in draw order (BG→FG→UI, z-ascending)         │
 │                                                                          │
 │  3. for chunk = 0..14:                                                   │
 │       renderScanlineChunk()   ── composite this chunk's bin into a back  │
 │                                  buffer (no reject; the bin already fits) │
 │       ili9341DrawScanlines()  ── DMA the previous/this strip to the LCD  │
 │                                                                          │
 └─────────────────────────────────────────────────────────────────────────┘
```

The submit-side (`rendererClear` / `rendererSubmitLayer`) just borrows each
layer's sprite array pointer + count. All the work is in `render`.

---

## 6. Stage by stage

### 6.1 Submit

```c
void rendererSubmit(Layer layer, const Sprite *sprite) {
    if (layer >= LAYER_COUNT) return;
    uint16_t count = s_active_sprites[layer];
    if (count >= s_max_sprites_per_layer[layer]) return; // silently drop on overflow
    s_sprites[layer][count] = *sprite;                   // copy by value
    s_active_sprites[layer] = count + 1U;
}
```

There are three fixed-capacity arrays (`MAX_SPRITES_BG = 320`, `FG = 256`,
`UI = 256`). Overflow is dropped rather than asserted — a missing sprite is a
gentler failure than a crash on an embedded console.

### 6.2 Counting sort by `z`

Within each layer, sprites must be drawn in `z` order (low z = behind). `z` is a
`uint8_t`, so the natural choice is a **counting sort** — O(n) with a 256-bucket
histogram, no comparisons:

```c
uint16_t z_count[256];
memset(z_count, 0, sizeof(z_count));
for (i) z_count[sprite[i].z]++;          // 1) histogram
prefix-sum z_count into start offsets;   // 2) exclusive prefix sum
for (i) s_sorted[layer][z_count[z]++] = &sprite[i]; // 3) scatter (stable)
```

It produces `s_sorted[layer][]` = pointers to sprites in ascending `z`. The sort
is **stable**, so sprites with equal `z` keep submission order.

### 6.3 Selecting a chunk's sprites: per-chunk bins

Once the layers are z-sorted, `binSpritesIntoChunks()` distributes the sprites
into one list per chunk — appending each sprite to every chunk its vertical
extent covers:

```c
for (layer in BG, FG, UI)                  // back-to-front layer order
    for (sprite in s_sorted[layer])        // ascending z within the layer
        first = clamp(sprite->y)            / RENDERER_SCANLINES;
        last  = (sprite->y + sprite->h - 1) / RENDERER_SCANLINES;
        for (c = first; c <= last; c++)
            if (s_chunk_bin_count[c] < CHUNK_BIN_CAP)
                s_chunk_bin[c][s_chunk_bin_count[c]++] = sprite;  // append
```

Because the bins are filled **in draw order** — layers `BG → FG → UI`, ascending
z within each — every chunk's bin already holds exactly the sprites it must paint,
in the order it must paint them (`BG(low z) → BG(high z) → FG → UI`).
`renderScanlineChunk()` then just walks its own bin, with **no vertical reject and
no sprites that miss the strip**:

```c
const Sprite *const *bin = s_chunk_bin[start_y / RENDERER_SCANLINES];
for (i in 0 .. s_chunk_bin_count[chunk])
    composite bin[i] into this strip;      // guaranteed to overlap
```

This replaced an earlier **scan**, where every chunk walked *all* sorted sprites
and rejected the ones it didn't overlap with a cheap `y`/`h` compare. With ~725
sprites over 15 chunks that is ~10,900 reject tests per frame — about **17 %** of
the frame. Binning pays a single up-front pass (~1,100 appends for the same scene)
and removes the reject entirely: measured **+3 to +5 FPS across every
format/opacity mode**.

> **Why this is safe where the old flat pool was not.** A still-earlier design
> pre-binned into one flat `s_chunk_pool` that had to be sized for the worst case
> — *every* sprite at the *tallest* height — so a few tall sprites could overflow
> it and blank the frame; it was removed for that reason. The per-chunk bins are
> sized by sprite **count**, not height: `s_chunk_bin[NUM_CHUNKS][CHUNK_BIN_CAP]`.
> A tall sprite simply lands in more chunks' bins (one 4-byte pointer each), never
> overflowing a height-based bound. A chunk that would exceed `CHUNK_BIN_CAP` (200,
> far above any realistic per-chunk count) drops the extra sprites rather than
> corrupting memory. Cost: `15 × 200 × 4 B = 12 KB`.

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
back-to-front. Then `ili9341DrawScanlines` hands the strip to DMA.

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

Because compositing a busy strip takes far longer than its DMA (see §8), the DMA
is essentially **free** — fully hidden behind the CPU work. Measured: turning the
DMA off entirely only changed the render time by ~4 %.

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
  return the slot. A miss — flipped, larger than 16×16, or the cache is full —
  takes the normal compositor, so correctness never depends on the cache.
- **Reset every frame.** A palette's *contents* can change under the same pointer
  (animation), so slots are dropped at the top of `rendererRender()`.
- **The copy is hand-rolled.** newlib-nano's `memcpy` is a **byte** loop — using it
  made this path *3× slower* than the unpack it replaced. A word-copy loop (two
  pixels per 32-bit store, taken only when the width is even so source alignment is
  stable across rows) is what makes it a win.

Result on the benchmark — opaque **2bpp 78 → 100 FPS**, **4bpp 55 → 101 FPS**: once
decoded, 2bpp and 4bpp are the same RGB565 copy, so the 4bpp penalty vanishes. The
gain scales with reuse; a tile drawn only once pays decode + copy for no benefit
(see §11). **RAM: 5.3 KB.**

---

## 7. Inside the compositor (the hot loop)

This is where ~95 % of the frame time goes, so it is where all the optimization
effort went. The structure is three layers of function, from outer to inner:

```
 renderScanlineChunk(chunk)
   └─ for each sprite in chunk:
        composite2bpp(sprite)  /  composite4bpp(sprite)   ← per-sprite setup
          └─ clipSprite()                                  ← clip once
          └─ for each row of the sprite in this chunk:
               blitRow2bpp() / blitRow4bpp() / blitRowFlip ← the pixel loop
```

### 7.1 Sprite-major, not scanline-major

A subtle but important choice. The naive scanline renderer loops *rows* on the
outside and *sprites* on the inside:

```c
for (row in chunk)            // ← outer
    for (sprite in chunk)     // ← inner
        draw one scanline of sprite;   // re-clips the sprite EVERY row
```

We flip it — **sprite on the outside, its rows on the inside**:

```c
for (sprite in chunk)          // ← outer
    clip the sprite ONCE;
    for (row of sprite)        // ← inner
        blit that row;
```

Both produce identical pixels (it is still painter's order: an earlier sprite is
fully painted before a later one overdraws it). But the sprite-major form lets us
compute the clip rectangle, source stride, palette table, and base pointers
**once per sprite** instead of once per scanline. For a 16-tall tile that is a
16× reduction of the setup overhead.

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

Off-screen sprites return `false` and are skipped entirely. Vertical flip is
folded into `src_top` + a negative row step so the inner loop never re-checks it.

### 7.3 The transparent path: unpack a whole byte, skip transparent runs

The inner loop walks the source **one byte at a time** (four 2bpp pixels) using
*constant* shifts. Compare with the original naive loop:

```c
// BEFORE — per pixel: variable shift, reload the same byte 4×, branch
for (i = 0; i < span; i++) {
    uint8_t idx = (row[col >> 2] >> (6 - ((col & 3) << 1))) & 3;  // variable shift!
    if (idx) dst[i] = palette[idx];
    col++;
}
```

```c
// AFTER — per byte: load once, four constant-shift extracts, one skip test
while (n >= 4) {
    uint8_t b = *p++;
    if (b) {                              // 0x00 ⇒ four transparent pixels: skip all four
        uint8_t i0 = (b >> 6) & 3; if (i0) d[0] = s_lut[i0];
        uint8_t i1 = (b >> 4) & 3; if (i1) d[1] = s_lut[i1];
        uint8_t i2 = (b >> 2) & 3; if (i2) d[2] = s_lut[i2];
        uint8_t i3 =  b       & 3; if (i3) d[3] = s_lut[i3];
    }
    d += 4; n -= 4;
}
```

Two wins here:
- **Constant shifts** (`>>6 >>4 >>2`) instead of a per-pixel `6 - ((col&3)<<1)`,
  and the byte is fetched **once** instead of four times.
- **`if (b)`** skips four fully-transparent pixels with a single test — perfect
  for text and sparse sprites, which are mostly index 0.

Unaligned left/right edges are handled by tiny **head** and **tail** loops that
bring `col` to a 4-pixel boundary; the body then runs aligned.

### 7.4 The opaque path: a flag, a palette table, and 32-bit stores

Most of a typical scene is **fully opaque** — backgrounds, filled panels, solid
tiles. Those sprites have *no* index-0 pixels, so every per-pixel `if (idx)` test
is wasted work. Sprites can declare this with `SPRITE_OPAQUE`, which selects a
branch-free blit.

`opaque` is passed to `blitRow2bpp` as a **compile-time-constant** argument from
two distinct call sites, so the compiler specializes two versions — one with the
transparency tests, one without — with zero runtime dispatch in the loop.

The opaque path goes one step further. Two adjacent pixels are packed into a
single 32-bit word via a **pair lookup table**, so the loop emits **one 32-bit
store per two pixels** instead of two 16-bit stores:

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

`s_pair[]` is rebuilt (16 entries) once per opaque sprite. The head loop has
already fixed alignment, so this fast path applies to any opaque sprite whose
visible left edge lands on a word boundary; the rare misaligned case falls back
to four scalar stores.

### 7.5 The palette lives at a fixed address: `s_lut`

Sprite palettes are `const` and therefore live in **flash**, which has wait
states. Reading `palette[idx]` *per pixel* from flash is slow. So each compositor
copies the palette into a file-scope scratch array **once per sprite**:

```c
static uint16_t s_lut[16];   // fixed address, in fast SRAM
...
s_lut[0]=pal[0]; s_lut[1]=pal[1]; s_lut[2]=pal[2]; s_lut[3]=pal[3];  // 2bpp
```

Why file-scope and not a local array? Because a **fixed link-time address** lets
the compiler emit a single hoisted base register and then `ldrh [base, idx, lsl #1]`
per pixel. A local array landed on the stack and the compiler kept recomputing
`sp + offset` *per pixel* — several wasted instructions each. The renderer is
single-threaded (one `rendererRender` at a time), so a shared scratch is safe.

### 7.6 The flipped path

Horizontal flip is the only case that cannot walk the source left-to-right by
byte, so it stays a simple per-pixel loop (`blitRowFlip`). Flips are uncommon and
not worth the code complexity of a reversed byte unpack.

### 7.7 Where the code runs: `.RamFunc`

The three compositor functions are placed in `.RamFunc` (linked into SRAM at
`CONSOLE_RAM`) rather than executed from flash. We **measured both**: SRAM
execution was faster (1.99 M vs 2.16 M cycles for the busy scene). Note this is
*not* CCM — the Cortex-M4 cannot fetch instructions from CCM on the F4, and the
scanline buffers must stay in DMA-reachable SRAM anyway.

---

## 8. The optimization journey: 24 → 76 FPS

All numbers below are from the **same stress scene** in `main.c`: a fully painted
screen with a 20×15 opaque background checkerboard, a 15×15 opaque foreground
fill, 100 transparent text glyphs, a 10×10 opaque panel, and a cursor —
**~166,000 composited pixels** over a 76,800-pixel screen (a **2.17× overdraw**).
Render time was measured with the cycle-accurate `DWT->CYCCNT` counter (see §9).

| Step | Change | Render (composite-only) | FPS |
| ---- | ------ | ----------------------- | --- |
| 0 | **Baseline** — per-pixel variable shift, reload byte ×4, palette in flash, clip every scanline | 6.73 M cyc (40.0 ms) | **24** |
| 1 | Byte-at-a-time unpack + constant shifts; hoist clipping out of the per-scanline loop (sprite-major) | 4.11 M | 39 |
| 2 | Compile *just this file* at `-O3` (`#pragma GCC optimize`) while the rest stays `-Og` | 3.45 M | 47 |
| 3 | Split the combined 2bpp/4bpp routine into separate functions (the combined one spilled registers badly) | 3.27 M | 49 |
| 4 | `SPRITE_OPAQUE` fast path + move the palette LUT to a fixed address (`s_lut`) | 2.31 M | 69 |
| 5 | Pack two pixels per 32-bit store via `s_pair[]` | **1.99 M** | **79** |
|   | *(final, with DMA on)* | **2.08 M (12.4 ms)** | **76** |

That is a **3.2× end-to-end speedup**, and the per-composited-pixel cost dropped
from ~40 cycles to ~12.

### Why each step worked

- **Step 1 — the algorithm, not the compiler.** The original loop did a
  *variable* shift and reloaded the same source byte for all four of its pixels,
  and it recomputed the clip rectangle for every scanline of every sprite.
  Reading a byte once and extracting four pixels with constant shifts, plus
  computing the clip once per sprite, is the single biggest structural win.

- **Step 2 — optimize the hot file only.** The whole firmware builds at `-Og`
  for debuggability. The renderer is the per-frame hot path, so it gets
  `#pragma GCC optimize("O3")` at the top of the file. This keeps the rest of the
  firmware debuggable while letting the inner loops be properly scheduled.

- **Step 3 — register pressure.** A single function containing both the 2bpp and
  4bpp paths (the 4bpp variant needs a 16-entry palette) overflowed the register
  file and spilled to the stack *inside the inner loop*. Two focused functions
  each fit in registers. We confirmed this by reading the disassembly.

- **Step 4 — stop doing pointless work.** Two things: (a) opaque sprites do not
  need a transparency test per pixel — a compile-time-constant `opaque` flag
  removes it; (b) the palette was being read from flash (wait states) and, when
  cached in a *local* array, addressed with a per-pixel stack-relative
  recompute. A *fixed-address* `s_lut` gave clean single-instruction lookups.
  Together these jumped 49 → 69 FPS.

- **Step 5 — wider stores.** RGB565 is 16-bit, but the Cortex-M4 stores 32 bits
  just as cheaply. Packing two pixels into a word halves the store count on the
  dominant opaque path.

### Two findings worth remembering

- **DMA is not the bottleneck.** Compositing dominates so thoroughly that the
  pixel-pushing DMA hides completely behind it (~4 % difference with DMA off).
  The FSMC write timing (≈36 ns/pixel) gives a ~2.7 ms full-screen floor — far
  below the composite cost.
- **Flash (with the ART cache) was *slower* than SRAM here.** Intuition says
  "code in RAM avoids flash wait states," but we measured it both ways; SRAM
  (`.RamFunc`) won, so the compositors stay there.

---

## 9. How to profile it yourself

The Cortex-M4 has a free-running cycle counter (`DWT->CYCCNT`). It is already
enabled by `swoInit()`, so you can read it directly. At 168 MHz, **168 cycles =
1 µs**. The profiling harness currently lives in `main.c`:

```c
uint32_t c0 = DWT->CYCCNT;
rendererRender();
uint32_t cyc = DWT->CYCCNT - c0;        // exact cycles for the whole frame
// ... accumulate min / max / average, print over SWO every couple of seconds
printf("FPS=%lu render avg=%luus (%lu cyc)\n", fps, cyc/168, cyc);
```

To measure **pure composite cost** (no display output), temporarily skip the
`ili9341DrawScanlines()` call in `rendererRender()` and compare.

Workflow on the bench:

```bash
make -C Console flash          # program + reset
timeout 8 ./tools/scripts/swo.sh   # capture SWO prints for a few seconds
pkill -9 -f openocd            # free the SWD/debug connection for the next flash
```

> Tip: read the actual disassembly when a change does not move the needle —
> `arm-none-eabi-objdump -d build/Console/renderer.o`. Several of the wins above
> (register spills, stack-relative palette addressing) were only obvious in the
> generated code.

---

## 10. Using the renderer

A minimal frame, modeled on `main.c`:

```c
/* one-time */
rendererInit();

/* palette: index 0 transparent, then up to 3 (2bpp) or 15 (4bpp) colors */
static const uint16_t pal[4] = { 0x0000, 0xF800, 0x07E0, 0xFFFF };

/* a 16×16 2bpp tile, 64 bytes of packed indices */
static const uint8_t tile[ (16*16)/4 ] = { /* ... */ };

/* per frame */
rendererClear();

Sprite s = {
    .x = 32, .y = 48, .w = 16, .h = 16,
    .z = 0,
    .flags = SPRITE_OPAQUE,        // set ONLY if the tile has no index-0 pixels
    .format = GFX_FMT_2BPP,
    .pixels = tile,
    .palette = pal,
};
rendererSubmit(LAYER_BG, &s);
/* ... submit the rest ... */

rendererRender();                  // composites + streams to the LCD
```

Rules of thumb:
- **Set `SPRITE_OPAQUE` only when the sprite truly has no transparent pixels.**
  It is a promise: the fast path does not test for index 0, so a stray
  transparent pixel will be drawn as `palette[0]` instead of showing through.
- Use `z` to order within a layer; use layers for coarse front/back grouping
  (UI always on top of FG, FG always on top of BG).
- Submitting more than a layer's capacity silently drops the extras.

---

## 11. Limitations & future work

### Current limitations

- **No clipping rectangle / no rotation / no scaling.** Sprites are axis-aligned,
  1:1, full-screen-clipped only. This is intentional simplicity.
- **Capacity is fixed at compile time** (`MAX_SPRITES_*`, `CHUNK_BIN_CAP`).
  Overflow drops sprites.
- **`s_lut` / `s_pair` / the per-chunk bins are shared scratch** — correct only
  because rendering is single-threaded and non-reentrant. Do not call the renderer
  from an ISR.

### Possible further optimizations

Three architectural levers were identified once the per-instruction blits hit
their floor. Two are implemented; the third is documented here with its RAM cost,
expected gain, and risk so the trade-off is on record.

1. **Per-chunk binning — _implemented_ (see §6.3).** Replaced the per-chunk
   reject scan with pre-built per-chunk bins, removing ~17 % of the frame (the
   reject) for **+3 to +5 FPS across every mode**. **RAM: 12 KB**
   (`15 chunks × 200 × 4 B`); sized by sprite count, capped per chunk. This is a
   pure, content-independent win — it does not depend on tile reuse or scene
   layering.

2. **Decoded-tile cache — _implemented_ (see §6.6).** Each opaque, unflipped,
   small tile is unpacked to a ready-made RGB565 buffer once per frame (keyed by
   `pixels` + `palette` + size); every instance then composites with a plain
   word-copy instead of re-running the 2bpp/4bpp unpack + palette lookup. On the
   benchmark (a few tiles drawn 700+ times) this took **opaque 2bpp 78 → 100 FPS**
   and **opaque 4bpp 55 → 101 FPS** — 4bpp catches up to 2bpp completely, because
   once decoded both formats are just RGB565. **RAM: 5.3 KB** (`TILE_CACHE_SLOTS =
   8`, each ≤16×16). **Caveats:** the win scales with **tile reuse** — a tile drawn
   once costs decode + copy, slightly *more* than a single unpack, so low-reuse
   scenes gain nothing; transparent tiles keep the masking compositor; and the
   `memcpy` from newlib-nano is a byte loop, so the copy is hand-rolled (a word
   loop), which mattered a lot (the byte-loop version was **3× slower**).

3. **Front-to-back + occlusion — _future_, biggest theoretical win, riskiest.**
   The design is pure painter's algorithm: a pixel hidden behind an opaque sprite
   is still composited then overwritten (the stress scene has **2.17× overdraw**).
   Drawing front-to-back with a per-scanline **coverage mask** would skip occluded
   spans. **RAM:** ~0 (a coverage bitmap per strip). **Caveats:** the optimized
   blits are so cheap that the coverage bookkeeping only pays back above ~7×
   overdraw — it can *lose* on lightly layered scenes — and it is the most
   invasive change to the core loop.

---

## 12. Glossary / quick reference

| Term | Meaning |
| ---- | ------- |
| **Chunk** | A horizontal strip of `RENDERER_SCANLINES` (16) scanlines; the unit of compositing + DMA. |
| **Layer** | Coarse front/back grouping: `BG < FG < UI`. |
| **`z`** | Fine draw order within a layer; low = behind. |
| **2bpp / 4bpp** | 2 or 4 bits per pixel; 4 or 16 palette indices. |
| **Index 0** | Always transparent. |
| **`SPRITE_OPAQUE`** | Promise that a sprite has no index-0 pixels → branch-free fast blit. |
| **`s_lut`** | Fixed-address SRAM copy of the active palette (fast per-pixel lookup). |
| **`s_pair`** | 16-entry table packing two adjacent pixels into one 32-bit word. |
| **`s_byte_full_2bpp`** | 256-entry table: is a 2bpp source byte fully opaque (no index-0 pixel)? Lets the transparent blit fast-path covered interior bytes. |
| **Per-chunk bin** | `s_chunk_bin[chunk][]` — the sprites that touch a chunk, in draw order, built once per frame (replaces the per-chunk reject). |
| **Tile cache** | `s_tile_cache[]` — opaque tiles decoded to RGB565 once per frame, keyed by `(pixels, palette, size)`; instances blit by row copy. |
| **`.RamFunc`** | Linker section placing hot functions in SRAM for execution. |
| **Painter's algorithm** | Draw back-to-front; later sprites overwrite earlier ones. |

| Constant | Value | Where |
| -------- | ----- | ----- |
| `RENDERER_WIDTH` × `RENDERER_HEIGHT` | 320 × 240 | `renderer.h` |
| `RENDERER_SCANLINES` | 16 (chunk height; 15 strips/frame) | `renderer.c` |
| `MAX_SPRITES_BG / FG / UI` | 350 / 350 / 350 | `renderer.c` |
| `CHUNK_BIN_CAP` | 200 (per-chunk sprite cap) | `renderer.c` |
| `TILE_CACHE_SLOTS` | 8 (decoded opaque tiles, ≤16×16) | `renderer.c` |
| Scanline buffers | 2 × 16 × 320 × 2 B = 20,480 B | `renderer.c` |
| Per-chunk bins | 15 × 200 × 4 B = 12,000 B | `renderer.c` |
| Tile cache | 8 × (12 + 512) B ≈ 5,376 B | `renderer.c` |

---

*Benchmarks in this document were captured on the STM32F407VET6 at 168 MHz with
`DWT->CYCCNT`, against the stress scene in `Console/Src/main.c`.*
