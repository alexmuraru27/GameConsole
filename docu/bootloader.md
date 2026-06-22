# The Bootloader — power-fail-safe OS self-flashing, ground up

The console can reflash **its own firmware** from the SD card (`Firmware/Console.bin`, via Settings → Firmware → Upgrade OS). The hard part isn't writing flash — it's surviving an interruption (dead battery, yanked card) without bricking the device, and being able to verify the new image *before* running it. Both require a small **bootloader** that always runs first and is never overwritten by an update.

This is the ground-up tour. The partition map is the single source of truth in `Console/Inc/Flasher/flash_map.h`; everything else mirrors it.

---

## 1. Why a bootloader is unavoidable

The STM32F407 has a **single flash bank**. While a program/erase is in flight, *every* access to the flash array stalls — including instruction fetches. And to rewrite the firmware you must erase the sector holding the reset vector. So:

- The routine doing the erase/program cannot be fetched from flash → it must run from **SRAM**.
- Once that routine erases sector 0 (the reset vector), the only safe outcome is to finish writing it. If power dies mid-write, the next boot has **no valid reset vector** — an SWD-only recovery.
- "Verify the new image before enabling it" is meaningless if the new image has already overwritten the only code that could do the verifying.

The fix is a second, always-present stage: a **bootloader** in sector 0 that the self-flash never touches. It owns the reset vector, verifies images, and decides what to run. The application moves above it.

---

## 2. The flash map

```
 sector 0     0x08000000  16K   BOOTLOADER   reset vector + apply/verify logic (SWD-flashed once)
 sectors 1-5  0x08004000 240K   APPLICATION  the console OS (links here; sets VTOR here)
 sectors 6-7  0x08040000 256K   STAGING      OsStagingHeader + the pending new image
```

STM32F407 sector geometry: sectors 0-3 = 16K, sector 4 = 64K, sectors 5-7 = 128K.

- The **application** relinks at `0x08004000` (`common.ld` sets `CONSOLE_FLASH` there) and points `SCB->VTOR` at that base in `SystemInit` so its interrupt vectors are found.
- **Staging** is scratch: an update is written there and verified there *before* anything irreversible happens. Its first bytes are the header:

```c
typedef struct {
    uint32_t magic;        /* OS_STAGING_MAGIC ("OSUP") once committed */
    uint32_t image_size;   /* bytes of the image at STAGING_IMAGE_ADDR */
    uint32_t image_crc32;  /* CRC-32 the applied app must match */
    uint32_t header_crc32; /* CRC-32 of the three words above */
} OsStagingHeader;
```

Staging is laid out header-first, image after a fixed gap (so the header can grow):

```
 0x08040000 ┌──────────────────────────────┐ OsStagingHeader (16 B): magic·size·img_crc·hdr_crc
            │ ····· (reserved, 0x200) ····· │
 0x08040200 ├──────────────────────────────┤ STAGING_IMAGE_ADDR ── the new OS image bytes
            │  image (== app .bin, ≤240K)  │
            └──────────────────────────────┘
```

### The two vector tables (boot handoff)

There are **two** vector tables; `VTOR` selects which is live. The hardware's reset
fetch (SP @ `0x08000000`, PC @ `0x08000004`) is fixed and ignores `VTOR`, so the
bootloader's table must sit at `0x08000000` and is always what runs first.

```
        reset / power-on
              │   HW reads SP @ 0x08000000, PC @ 0x08000004  (fixed — ignores VTOR)
              ▼
   ┌──────────────────────────────┐   VTOR = 0x08000000 (reset default)
   │ BOOTLOADER vector table       │   minimal — mostly weak Default_Handler aliases;
   │ @ 0x08000000                  │   the bootloader enables no interrupts of its own
   └──────────────────────────────┘
              │   jumpToApp(): SCB->VTOR = 0x08004000; set MSP; branch to app reset
              ▼
   ┌──────────────────────────────┐   VTOR = 0x08004000 (the app re-asserts it in SystemInit)
   │ APPLICATION vector table      │   real handlers: SysTick, TIM6/7, USART, SVC/PendSV, faults…
   │ @ 0x08004000                  │   every runtime interrupt vectors through here
   └──────────────────────────────┘
```

So the bootloader's table only matters during its brief post-reset window; once the
app is running, **all** interrupts go through the app's own table at `0x08004000`.
(That's the reason the app had to move above the bootloader: two tables can't both
own `0x08000000`.) The bootloader is therefore not self-updatable — see §9.

---

## 3. The update, end to end

```
  Settings -> Firmware -> Upgrade OS          (os_update.c, app, modal)
    │  find Firmware/Console.bin on the SD card
    ▼
  osFlasherStage()                            (os_flasher.c, app)
    │  erase staging (sectors 6-7)
    │  stream SD -> staging image area, computing a CRC of the bytes read
    │  read the staged image back, confirm its CRC == the read CRC   ← flash-write integrity
    │  return the read CRC + size
    ▼
  compare read CRC vs the CRC recorded at download time (downloaded.csv)
    │  mismatch -> warn, let the user flash-anyway or cancel          ← SD-read integrity
    ▼
  osFlasherCommitAndReboot()                  (os_flasher.c, app)
    │  write the staging header: size, image_crc32, header_crc32, then magic LAST
    │  gameConsoleReboot()  ───────────────────────────────────────────────┐
    ▼                                                                       │
  ── the running OS was never touched up to here; only staging was ──       │
                                                                            │
  RESET ─> BOOTLOADER (Bootloader/Src/main.c) ◀───────────────────────────┘
    │  staging header valid & committed?  (magic == OSUP, header_crc ok, size in range)
    │     no  -> validate the app and jump to it
    │     yes -> apply:
    │             erase the app region (sectors 1-5)
    │             copy staging image -> app region (word by word)
    │             crc32 the app region by READBACK
    │             == image_crc32 ?  yes -> clear magic (program to 0), reset -> run new app
    │                               no  -> reset -> re-apply next boot (staging still intact)
    ▼
  new OS runs
```

The two CRC checks cover the two ways the image can be wrong: a **bad SD read** (caught by comparing the read CRC to the recorded download CRC) and a **bad flash write** (caught by reading staging back, and again by the bootloader reading the app back).

---

## 4. Why interruption can't brick it

Whatever step power is lost in, the next boot lands somewhere safe:

```
  power lost during…                  next boot sees…              outcome
  ──────────────────────────────────────────────────────────────────────────────────
  SD stream → staging              no committed header          old OS boots (staging is scratch)
  the commit (magic write, last)   magic absent / hdr CRC bad   old OS boots (treated as not-pending)
  erasing the app region           pending set, app CRC bad     re-apply from intact staging
  copying staging → app            pending set, app CRC bad     re-apply from intact staging
  clearing the magic (consume)     app already verified         boots app (or re-verify → re-clear)
  ──────────────────────────────────────────────────────────────────────────────────
  unrecoverable only if sector 0 (the bootloader) is corrupted — which self-flash never writes
```

Every dangerous step is either reversible or idempotent:

- **During the SD transfer** only staging is written. The running OS is intact, so a yanked card or dead battery just means "no committed update" — the old OS boots normally.
- **The commit is one magic write, done last.** The `header_crc32` and `image_crc32` are written first; the `magic` word is written last. A torn write leaves `magic` absent (or the header CRC failing), which the bootloader reads as "not pending" → the old OS boots. There is no half-committed state.
- **The apply is idempotent.** It reads from the staging copy, which is reliable internal flash, not the SD card. If power dies mid-apply, the next boot still sees the committed header *and* an app whose readback CRC doesn't match — so it simply re-applies. It only clears the pending flag (and thus boots the new app) once the readback CRC matches. It repeats across as many brownouts as it takes.
- **The bootloader is never rewritten by an update** (the self-flash only ever erases sectors 1-7), so it cannot be corrupted by one. It is flashed once over SWD and left alone.

The only unrecoverable failure is corrupting sector 0 itself — which requires an SWD write, not a self-update.

---

## 5. Tracing (SWO)

The bootloader emits the same severity-tagged SWO log lines as the console (`[tick][L][CHAN] msg`) on the **`LOGGER_BOOT`** channel — it reuses the console's `logger.c` verbatim, with a small `trace.c` providing the three pieces it needs standalone: `swoInit` (prescaler computed for the bootloader's 16 MHz HSI clock, still targeting the host's 2 MHz SWO), `_write` → `ITM_SendChar`, and a DWT-cycle-counter `getSysTime` (there is no SysTick). It traces every decision and apply step: the boot banner and partition addresses, whether a pending update was found (and why not), each sector erased, copy progress every 32 KB, and the readback-verify result with both CRCs. The app-side flasher is just as verbose on `LOGGER_FLASHER` (per-sector erase, per-chunk byte count, verify, commit).

Because the bootloader runs only briefly after each reset, you see its logs by having the SWO session already attached (e.g. `make flashswo`) and then triggering an update from the menu: the app reboots, the trace session stays up across the reset, and the bootloader's apply is traced live. (`make flashswo` re-runs `tpiu config` for the app's 168 MHz; the host decodes both at the fixed 2 MHz because each stage sets its own prescaler.)

## 6. The low-level driver (`flash_ll.c`)

Shared by the app (writes staging) and the bootloader (writes the app). Erase and program are `.RamFunc` — copied to SRAM at boot — so they keep executing while the flash array is stalled mid-operation, and they run with interrupts masked (`PRIMASK`) so no ISR is fetched from the stalled array. The *callers* orchestrate from flash and read flash back normally between operations; the invariant is simply **never erase or program the sector you are executing from** (the app touches only sectors 6-7; the bootloader touches only sectors 1-5, never its own sector 0).

```
  caller in FLASH                  flashLlProgramWord()  (RAM, IRQ masked)
  ───────────────                  ─────────────────────────────────────
  …loop body…                      FLASH->CR |= PG
     │  bl ───────────────────▶    *addr = word            ┐ flash array now stalls
     │                             while (FLASH->SR & BSY)  │ EVERY flash access — but this
     │                             { }                      │ code runs from RAM, so it
     │                             FLASH->CR &= ~PG          ┘ keeps polling and completes
     ▼  ◀──────────────── bx lr    return
  …next fetch from flash OK…       (flash readable again between ops)
```

Because the op code lives in RAM, the stall costs only the program/erase time — not a fault. The orchestration (loops, `src[]` reads, CRC) stays in flash and only ever touches it *between* ops, when the array is idle.

---

## 7. Building & flashing

```
make -C Bootloader all     # the bootloader (sector 0)
make -C Console all         # the app (sectors 1-5, links at 0x08004000)
make flash                  # SWD-flash BOTH stages (bootloader then app)
make deploy                 # stage the app image as content/os/Console.bin for OTA
```

The bootloader is a minimal separate target: it reuses the console's `startup.s` (unused peripheral ISRs stay weak-aliased to the default handler) and shares `crc.c` and `flash_ll.c`, with its own `bootloader.ld`. Flash it once over SWD; thereafter the OS updates itself over WiFi + SD.

---

## 8. File map

| File | Responsibility |
| ---- | -------------- |
| `Console/Inc/Flasher/flash_map.h` | the partition map + `OsStagingHeader` (single source of truth) |
| `Console/Src/Flasher/flash_ll.c` | RAM-resident erase/program primitives (shared) |
| `Console/Src/Flasher/os_flasher.c` | app side: stage to flash, verify by readback, commit + reboot |
| `Console/Src/MainMenu/os_update.c` | the "Upgrade OS" modal (shares `flash_ui.c` with the ESP flow) |
| `Bootloader/Src/main.c` | apply (with readback verify) or boot the app; traces every step on `LOGGER_BOOT` |
| `Bootloader/Src/trace.c` | SWO/ITM glue so the bootloader reuses the console's `logger.c` (`swoInit`/`_write`/`getSysTime`) |
| `Bootloader/bootloader.ld` | sector-0 link map |
| `common.ld` | relinks the app above the bootloader |

---

## 9. Known limits & sharp edges

- **The bootloader is not self-updatable.** It lives in sector 0, which the self-flash never erases (so an interrupted update can't corrupt it). Changing the bootloader's code or vector table therefore needs an SWD reflash (`make -C Bootloader flash`).
- **The OS image must fit the 240 KB app region.** Staging is 256 KB, so the app region is the binding limit. `osFlasherStage` rejects an oversized image (`OS_FLASH_TOO_BIG`), and the app link itself fails (region overflow) if the firmware ever grows past 240 KB — at which point the partition in `flash_map.h` / `common.ld` must be rebalanced.
- **A persistently failing flash chip reset-loops.** Once `applyStaging` has erased the app region, there is no old app to fall back to; if the readback never verifies (genuine hardware failure), the bootloader keeps resetting and re-applying from the intact staging. There is no retry cap — the assumption is that the staging copy is good and a stable supply will eventually let the apply complete. A dead flash needs servicing regardless.
- **The last SWO log line before a bootloader reset can be truncated** (pending fix). `applyStaging` calls `NVIC_SystemReset()` while the SWO serializer may still be draining the final line — and the bootloader has no SysTick-based delay to cover the drain — so e.g. `…rebooting into new OS` can be cut off mid-byte before the next boot banner. Cosmetic only (the apply already completed and verified). A short drain before the reset (a busy-wait on the cycle counter, or polling the TPIU) would fix it; left out to keep the reset path minimal.
- **`getSysTime()` in the bootloader is a per-reset DWT-cycle timestamp** (there is no SysTick), so `[BOOT]` ticks restart at 0 on every boot/stage rather than being monotonic with the app.
- **The bootloader runs at HSI 16 MHz** (it never starts the PLL). Flash erase/program timing is independent of the CPU clock, so this only makes the CRC and copy loop a few× slower than the app would at 168 MHz — negligible for a one-shot apply.

---

See also: `docu/memory.md` (the full flash/SRAM map) and `docu/flasher.md` (the ESP-01 flasher, the sibling flow this shares its modal UI with).
