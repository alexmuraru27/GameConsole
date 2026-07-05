# The Kernel — game isolation, ground up

This console runs untrusted code. A game is a separate `.bin` loaded from the SD card at runtime; it may be buggy or hostile. The console's job is to run it **without letting it corrupt the OS, read random memory, or brick the device** — and to recover cleanly when it misbehaves.

The mechanism is a small **microkernel**: the console is the kernel (privileged), the game is a user process (unprivileged), the **MPU** is the wall between them, and **SVC** is the only door through the wall. A crash on the game's side is caught, reported, and recovered from; a crash on the kernel's side is fatal.

All of this lives in `Console/Src/Kernel/` (`syscall.c`, `scheduler.c`, `mpu.c`, `faults.c`) and `Shared/Syscall/` (the ABI both sides agree on).

---

## 1. The mental model

Two questions decide everything on a Cortex-M4: **what privilege** is the code running at, and **which stack** is it using.

```
                 Thread mode                        Handler mode
              (your code runs)                    (exceptions run)
          ┌────────────────────────┐        ┌────────────────────────────┐
priv-     │  Console / menu loop   │        │  SVC · PendSV · faults ·    │
ileged    │  stack: MSP            │        │  timer + peripheral ISRs    │
          │                        │        │  stack: MSP                 │
          └────────────────────────┘        └────────────────────────────┘
          ┌────────────────────────┐
un-       │  Loaded game (GameXO)  │   ◀── confined by the MPU to GAME_RAM
priv.     │  stack: PSP            │        + the CCM asset arena. Nothing else.
          └────────────────────────┘
```

- **The console** runs privileged, in Thread mode, on the **MSP**. It owns all hardware and the full memory map.
- **A game** runs unprivileged, in Thread mode, on the **PSP**. The MPU lets it touch only its own RAM.
- **Every exception** (SVC, PendSV, faults, ISRs) runs privileged, in Handler mode, on the **MSP**.

The console thread and the exception handlers therefore *share the MSP*. That is not an accident — it is the trick that lets a syscall or a crash hand control back to the exact point where the console launched the game (see §4).

Two consequences fall out immediately:

- A game **cannot execute console code**: it has no way to raise its own privilege except by trapping (SVC), and any direct branch into console flash + touch of console RAM would fault.
- A syscall automatically **runs on the kernel stack (MSP)**, not the game's stack — the hardware switches to MSP on exception entry. The game can neither see nor corrupt it.

---

## 2. The syscall ABI — the only door

A game never links against console code and never calls a function pointer into the console. Instead it links a tiny **stub library** (`Shared/Syscall/console_syscalls.c`) of strongly-typed wrappers. Each wrapper marshals its arguments and executes one `svc #0`.

```
 Shared/Syscall/
   syscall_numbers.h   enum SyscallId + CONSOLE_ABI_VERSION   (the single source of truth)
   console_syscalls.h  typed prototypes the game calls        (rendererSubmitLayer, …)
   console_syscalls.c  the SVC stubs                           (compiled into every game)
```

The calling convention is deliberately simple, chosen so the four argument registers the AAPCS already filled don't need to move:

```
   r12  =  syscall id        (IP — caller-saved, not an argument register)
   r0..r3 = up to 4 args
   svc #0
   r0   =  return value      (written back by the kernel into the stacked frame)
```

Calls with more than four register arguments (`buzzerPlayWithFlag`) bundle them into a struct in the game's own RAM and pass one pointer. `gameLog()` is formatted **game-side** into a bounded buffer and handed over as raw bytes + length — so no game-controlled format string ever reaches the kernel's `printf` (no `%s`/`%n` confused-deputy).

### A syscall round-trip

```
  GAME  (unprivileged, PSP)                   KERNEL  (privileged, MSP)
  ─────────────────────────                   ─────────────────────────
  rendererSubmitLayer(BG, spr, n)
      │  stub: r12=SYS_RENDERER_SUBMIT_LAYER
      │        r0=BG  r1=spr  r2=n
      ▼
    svc #0  ──────────────────────────────▶   SVC_Handler        (scheduler.c, naked)
                                                 │ EXC_RETURN bit2 → frame is on PSP
                                                 ▼
                                              svcHandlerMain(frame, exc_return)
                                                 │ id = frame[4]   (the stacked r12)
                                                 ▼
                                              svcDispatch(id, frame)     (syscall.c)
                                                 │ validate every pointer arg
                                                 │ rendererSubmitLayer(BG, spr, n)
                                                 │ frame[0] = result      (→ caller r0)
                                                 ▼
    r0 = result  ◀──────────────────────────   bx  EXC_RETURN (0xFFFFFFFD → Thread/PSP)
      │
      ▼
   …game continues…
```

`SVC_Handler` is a naked trampoline: it figures out which stack holds the caller's exception frame (PSP for a game syscall or a callback's `SYS_FRAME_DONE`, MSP for the `SYS_INVOKE` trap — see §4), then calls `svcHandlerMain`, which either does the context-switch lifecycle calls (`SYS_INVOKE`/`SYS_FRAME_DONE`/`SYS_EXIT`) or forwards everything else to `svcDispatch`. The dispatcher is a deliberately **explicit `switch`**, not a generated table — it is the trust boundary, so every argument cast and every pointer check is meant to be read in one place.

Because the SVC handler runs at the **lowest** exception priority (§7), a long syscall such as a full-frame render stays preemptible by the audio and tick interrupts while it runs on the kernel stack.

---

## 3. Memory protection — the MPU wall

The MPU (`mpu.c`) is enabled at boot with **`PRIVDEFENA`**: privileged code (the console) keeps the full default memory map, while unprivileged code (a game) gets access *only* through regions the kernel explicitly opens. When a game is running it has **three** regions; everything else faults.

```
   addr                         region / access
 ───────────────────────────────────────────────────────────────────────
 0x10000000 ┌────────────────┐  MPU region 1  ── game: RW, execute-never
            │ CCM asset arena│                   (64 KB) — .pak blobs land here
 0x10010000 └────────────────┘
            ┊                ┊
 0x08000000 ┌────────────────┐  background map ── game: no direct access.
            │ console flash  │                   (readable by the *kernel*; font
            │ 512 KB         │                    glyph pointers are allowed through
 0x08080000 └────────────────┘                    pointer-validation, see §5)
            ┊                ┊
 0x20000000 ┌────────────────┐  background map ── game: TRAPS  (MemManage)
            │ CONSOLE_RAM    │                   kernel .data/.bss, MSP stack,
            │ 96 KB          │                   renderer buffers, DMA, FatFs…
 0x20018000 ├────────────────┤  MPU region 0  ── game: RWX
            │ GAME_RAM 32 KB │                   game code + rodata + data + bss
            │  .bss          │                   + stack (one power-of-two region)
            │ ┌────────────┐ │  MPU region 2  ── game: NO ACCESS (256 B guard)
            │ │stack guard │ │                   overlaps region 0, higher number
            │ └────────────┘ │                   wins → overflow traps here
            │  stack ↓ ↓ ↓   │
 0x20020000 └────────────────┘
            ┊                ┊
 0x40000000 ┌────────────────┐  background map ── game: TRAPS  (MemManage)
            │ peripherals    │                   GPIO, timers, I2C, SDIO, FSMC…
            └────────────────┘
```

| Region | Base | Size | Game access | Notes |
| ------ | ---- | ---- | ----------- | ----- |
| 0 — `GAME_RAM` | `0x20018000` | 32 KB | **RWX** | code + data + stack share one region (single RWX region by design; W^X not enforced) |
| 1 — CCM arena | `0x10000000` | 64 KB | **RW, XN** | data only — CCM can't execute on the STM32F4 anyway |
| 2 — stack guard | *(per-game)* | 256 B | **none** (`AP_PRIV`) | no-access band just below the PSP; base from the game header (`stack_guard`). Overlaps region 0 and wins as the higher-numbered region, so a stack overflow faults on the offending instruction instead of silently corrupting `.bss`. Privileged-accessible so the console is never faulted by it. |
| *(everything else)* | — | — | **none** | console RAM, peripherals, flash → recoverable MemManage fault |

`GAME_RAM` is sized and aligned to a power of two (32 KB at `0x20018000`) specifically so it is a *single, clean* MPU region — no sub-region tricks. The **stack guard** is a 256-byte no-access band the game's linker places (the `.stack_guard` section in `app.ld`, 256-aligned) between `.bss` and the descending stack; its base rides in the game header, and `mpuConfigureForGame()` validates it sits inside `GAME_RAM` before arming region 2 (a bad/zero base just leaves the guard off — the game still runs, only unguarded). A deep-recursing or runaway game trips it and returns to the menu as "crashed" with an exact fault PC, rather than silently clobbering its own data. The regions are programmed in `mpuConfigureForGame()` right before a game is entered and torn down in `mpuReleaseGame()` the moment it exits or crashes, so no unprivileged access ever outlives the game.

The renderer, the buzzer ISR, and the asset loader all read game RAM **from the kernel side** (privileged) — which the background map permits — so the game submitting sprite pointers or note data costs nothing extra.

---

## 4. The context switch — the OS-driven loop

The **OS owns the game loop**. A game is not a `main()` that runs forever — the console calls it as three callbacks (`init` once, then `update`/`render` each frame) and does its own work (pacing the frame, servicing the WiFi link) in between. `kernelRunGame()` (scheduler.c) runs as a normal C call chain on the MSP and, for each callback, must *become* the game briefly and come right back. It does that by turning each call into an exception.

**Narrative:** `SYS_INVOKE` takes us **into** one callback and parks the console; `SYS_FRAME_DONE` brings us **back out** when the callback returns — leaving the MPU and the game session up so the next callback can run. Only `gameExit()` or a crash ends the session, and those route through PendSV (which tears the session down). So per frame there are two cheap excursions (update, render); the MPU is programmed once per session, not once per call.

```
  kernelRunGame(header, collect, send):
    mpuConfigureForGame()                 ← wall goes up once
    invoke(entry_point)   ── bootstrap: zero .bss, run ctors
    invoke(init)          ── game one-time setup
    while (game active):
        collect()         ── console: ingest inbound before the game steps (privileged)
        invoke(update)    ── game logic        ┐ two excursions
        send()            ── console: emit outbound; an async round-trip overlaps render (privileged; no pacing)
        invoke(render)    ── game draw          ┘ per frame
    (session ended by gameExit/crash → PendSV released the wall)
```

### Invoke one callback (`SYS_INVOKE`)

`kernelInvokeGame(fn)`:

1. Builds a fresh **exception frame** by hand at the top of `GAME_RAM` — 8 words `{r0–r3=0, r12=0, LR=_game_return, PC=fn, xPSR=0x01000000}`. The LR is the game-side return trampoline (see below); the PSP is reset to the top of `GAME_RAM` every invoke, so each callback starts on a clean stack (state lives in the game's globals, which persist).
2. Calls `kernelTriggerInvoke()`, which preserves the console's callee-saved registers and traps:

```asm
   push {r4-r11, lr}     ; park the console's callee regs below the SVC frame
   mov  r12, #SYS_INVOKE
   svc  #0               ; ── run the callback; we resume *here* when it returns ──
   pop  {r4-r11, pc}     ; …returns to kernelInvokeGame's caller (the loop)
```

The `svc` stacks the console's exception frame onto the **MSP** and enters the handler. `svcHandlerMain` sees `SYS_INVOKE`, points the PSP at the hand-built frame, sets `CONTROL.nPRIV` (drop to unprivileged), and returns with `EXC_RETURN = 0xFFFFFFFD` (Thread mode, **PSP**). The hardware unstacks the *callback* frame from the PSP — so the callback runs — and the console's frame sits **parked on the MSP**, untouched.

```
        MSP  (kernel stack)                       PSP  (game stack)
   higher ┌───────────────────────┐         0x20020000 ┌──────────────┐
   addr   │ …kernelRunGame loop   │                    │ (callback    │
          │ → kernelInvokeGame    │                    │  stack, down)│
          │ {r4-r11, lr}  ◀── push│                    ├──────────────┤
          │ ┌───────────────────┐ │         0x2001FFE0 │ fresh 8-wd   │ ◀ PSP
   MSP ──▶│ │ parked console    │ │                    │ frame: pc=fn │
          │ │ exception frame   │ │                    │ lr=_game_ret │
   lower  │ └───────────────────┘ │                    └──────────────┘
   addr   └───────────────────────┘
            (frozen for this one                 the callback runs here,
             callback's duration)                unprivileged, MPU-confined
```

The `EXC_RETURN` the console was entered with is saved (`s_console_exc_return`) so the eventual return matches the exact stacked-frame layout — important if the console ever has live FPU state, because that changes the frame size.

### Callback returns (`SYS_FRAME_DONE`)

A callback is plain C — it just `return`s. The kernel set its LR to `_game_return`, a tiny game-side trampoline (in `console_syscalls.c`, so it runs unprivileged from `GAME_RAM`) that does `svc #0` with `SYS_FRAME_DONE`. The handler **re-privileges the thread** and returns with `s_console_exc_return` (Thread mode, **MSP**) — directly, no PendSV. That unstacks the **parked console frame** from the MSP → execution resumes at `pop {r4-r11, pc}` inside `kernelTriggerInvoke` → back in the loop. The MPU regions and `s_game_active` stay up, so the next `invoke()` runs against the same live session.

This is the symmetric counterpart of `SYS_INVOKE`: invoke unstacks the *game* frame from the PSP; frame-done unstacks the *parked console* frame from the MSP. Both are handled inline in the SVC handler — no PendSV on the hot per-frame path.

### Clean exit (`SYS_EXIT`)

To quit, a callback (by convention on Special Button 2) calls `gameExit()` — `svc #0` with `SYS_EXIT`. Unlike frame-done, this must end the **whole session**, so the handler does **not** switch directly; it **pends PendSV** and returns toward the game. Because PendSV outranks Thread mode, the processor never executes another game instruction — it tail-chains straight into PendSV, which:

1. releases the MPU regions,
2. re-privileges the thread (`CONTROL.nPRIV = 0`),
3. returns with `EXC_RETURN = 0xFFFFFFF9` (Thread mode, **MSP**).

That unstacks the parked console frame (the current invoke's) from the MSP → resumes in `kernelInvokeGame` → the loop sees `kernelGameActive()` is now false and stops → `kernelRunGame()` returns → the menu redraws.

### Crash (any fault from the game)

Identical session-ending exit path, reached from the fault handler instead of `SYS_EXIT` — see §6.

---

## 5. Pointer validation — the confused-deputy defense

The MPU stops the *game* from touching console memory. But a game also hands the kernel pointers (sprite arrays, save buffers, asset destinations) and asks the **privileged** kernel to read or write them. Without checking, a malicious game could pass `&console_ram` and make the kernel scribble there on its behalf — a "confused deputy."

So `svcDispatch` validates every pointer argument **before** the privileged side touches it (`syscall.c`):

```
 gameCanWrite(p, len) :  [p, p+len) lies wholly inside GAME_RAM  ∪ CCM arena
 gameCanRead (p, len) :  the above  ∪ console flash
```

- **Writes** must target memory the game owns (GAME_RAM or the CCM arena).
- **Reads** additionally allow console **flash**, because a sprite's `pixels`/`palette` can legitimately point at a built-in font glyph in flash (which the renderer reads). Flash is contiguous and side-effect-free, so a bounded over-read there can never fault the kernel or expose live state.

`rendererSubmitLayer` is the thorough case: the sprite **array** is range-checked, then *every* sprite's pixel and palette extent is checked, sized from its width/height and pixel format:

```
   spritesValid(sprites, count):
       gameCanRead(sprites, count * sizeof(Sprite))         ?
       for each s:  gameCanRead(s.pixels,  w*h*bpp/8)        ?
                    gameCanRead(s.palette, slots * 2)        ?
```

A failed check rejects that one syscall (logs a warning, skips the work) rather than killing the game — safe, and gentler than a fault.

---

## 6. Faults — correct attribution, then recovery

Out of reset, MemManage/BusFault/UsageFault are disabled and *escalate* to a single generic HardFault. `faultsInit()` (faults.c) **enables them** (`SHCSR`) and turns on divide-by-zero trapping, so each cause surfaces as its own, named fault.

The four fault vectors share one naked trampoline → `faultReport()`, which prints over SWO:

```
 === MEMMANAGE FAULT ===
 ctx =PSP/thread (EXC_RETURN=0x…)     ← PSP = the game, MSP = the kernel
 PC  =0x…  LR  =0x…  PSR =0x…          ← stacked from the faulting frame
 CFSR=0x…  HFSR=0x…
 flags: DACCVIOL MMARVALID             ← the CFSR sub-bits, decoded by name
 MMFAR=0x20000000                      ← the address that faulted
```

Then it decides:

```
        a fault happened
              │
       from PSP (a game)  and  a game is active?
         │  yes                         │  no
         ▼                              ▼
  recoverable:                    fatal (kernel bug):
   clear sticky CFSR/HFSR          halt for the debugger
   pend PendSV (request leave)     (while(1))
   return toward the game
         │
         │  (return is blocked: PendSV is pending and outranks Thread mode)
         ▼  tail-chain
   PendSV  ── release MPU, re-privilege, EXC_RETURN 0xFFFFFFF9 ──▶ console resumes
         ▼
   gameLoaderLoadGame() returns GAME_LOADER_RET_CRASHED
         ▼
   the game list shows  "<name> crashed - recovered"  for a few seconds
```

The crux of recoverability: the fault handler must **not** simply return — that would re-run the faulting instruction. Pending PendSV and letting it **tail-chain** means the processor switches to the console *before* the bad instruction is retried, so it never re-faults. The faulting unprivileged context is just abandoned (its PSP frame discarded); the parked console context on the MSP is what we resume.

A fault that originated in the kernel (Handler mode, or Thread/MSP) is a real bug and halts — there is nothing safe to recover to.

---

## 7. Interrupt priorities — why the switch lives at the bottom

The switch handlers and the syscall handler run at the **lowest** exception priority, and *every* peripheral and timer interrupt must be able to preempt them. This is the invariant that keeps a syscall that internally busy-waits (an EEPROM write's `delay()`, an SD read) making progress.

```
   priority   source                         rationale
   ── higher urgency ───────────────────────────────────────────────────
     0        DMA / SDIO / I2C (default)      short completion ISRs
     1        TIM6  (buzzer, 1 ms)            audio timing must stay tight
     2        SysTick (1 ms tick)             getSysTime()/delay() depend on it
    14        TIM7  (joystick, 50 ms)         least-urgent ISR, still preempts syscalls
    15        SVC / PendSV                    syscalls + context switch — strictly lowest
   ── lower urgency ────────────────────────────────────────────────────
```

The lesson is baked into this table: `SysTick_Config()` leaves the tick at priority 15, the same as SVC. A privileged syscall (running in the SVC handler) that called `delay()` would then busy-wait on a tick that could never preempt it — a deadlock. The fix is the rule above: **SVC and PendSV are strictly the lowest priority in the system, and nothing else shares their level** (`syscallInit()` raises SysTick to 2; `timer7Init` raises TIM7 to 14). The renderer's panel DMA wait polls a *hardware* flag, not an ISR, so it has no such dependency.

---

## 8. End-to-end lifecycle

```
  menu: player picks GameXO, presses A
    │
    ▼
  gameLoaderLoadGame(idx)                       (privileged, MSP, Thread)
    │  read 32-byte header → check magic + ABI version
    │  copy the whole .bin to GAME_RAM (flat image, sections land in place)
    │  bind .pak + settings slot
    ▼
  kernelRunGame(header, collect, send)
    │  mpuConfigureForGame(header->stack_guard)  ← wall goes up (once)
    │  ┌─ for each callback: kernelInvokeGame(fn) ─────────────────────────┐
    │  │   build fresh PSP frame at top of GAME_RAM (PC=fn, LR=_game_return)│
    │  │   kernelTriggerInvoke():  svc SYS_INVOKE                           │
    │  │ ▼ ──────────────────────── drop to unprivileged, switch to PSP     │
    │  │  callback  (unprivileged, PSP, Thread)                             │
    │  │   …console calls are svc #0 → SVC_Handler → svcDispatch…           │
    │  │   return → _game_return → svc SYS_FRAME_DONE                       │
    │  │ ▲ ── re-privilege, EXC_RETURN s_console_exc_return → MSP (no PendSV)│
    │  └────────────────────────────────────────────────────────────────────┘
    │  sequence:  entry_point (bss+ctors) · init · loop{ collect · update · send · render }
    │
    ├─ clean: update calls gameExit() → svc SYS_EXIT → pend PendSV
    └─ crash: illegal access → MemManage/Bus/Usage fault → decode → pend PendSV
              │
              ▼  PendSV tail-chains   (session end only — not per frame)
  PendSV_Handler                                (privileged, MSP, Handler)
    │  mpuReleaseGame()                          ← wall comes down
    │  re-privilege thread, clear s_game_active
    │  EXC_RETURN 0xFFFFFFF9 → unstack parked console frame
    ▼ ─────────────────────────────────────────
  kernelRunGame() loop sees !active, returns to gameLoaderLoadGame()
    │  unbind settings · close .pak
    │  return OK or GAME_LOADER_RET_CRASHED
    ▼
  menu: rebuild surface; on crash, show the "recovered" banner
```

---

## 9. File map

| File | Responsibility |
| ---- | -------------- |
| `Shared/Syscall/syscall_numbers.h` | the ABI: `SyscallId` enum + `CONSOLE_ABI_VERSION` (both sides include it) |
| `Shared/Syscall/console_syscalls.h` | typed game-facing prototypes |
| `Shared/Syscall/console_syscalls.c` | the SVC stubs, compiled into every game |
| `Console/Src/Kernel/syscall.c` | `svcDispatch` (the switch), pointer validators, `syscallInit` (priorities) |
| `Console/Src/Kernel/scheduler.c` | `SVC_Handler`, `PendSV_Handler`, `kernelRunGame` (the OS-driven loop), the invoke/frame-done/leave context switch |
| `Console/Src/Kernel/mpu.c` | enable + per-game region setup/teardown |
| `Console/Src/Kernel/faults.c` | enable + decode the configurable faults; route game crashes to recovery |
| `Console/Src/Loader/game_loader.c` | validate header, copy the flat image, hand off to `kernelRunGame` |

---

## 10. Known limits & sharp edges

- **FPU callee registers (s16–s31) are not preserved across a callback invoke.** Only s0–s15 ride the exception frame. The console holds no live FP state across an invoke today, so it's fine; if that changes, add `vpush/vpop {s16-s31}` in `kernelTriggerInvoke`.
- **Sprite pixel pointers may aim into flash.** `gameCanRead` allows it (font glyphs live there). A game could therefore render arbitrary flash bytes — a *bounded* information leak, with no corruption and no kernel fault. Accepted by design.
- **`GAME_RAM` is one RWX region** — W^X is not enforced. Hardening would split game `.text` into its own read-only, executable sub-region.
- **ABI versioning:** any change to syscall ids or argument marshalling must bump `CONSOLE_ABI_VERSION`; the loader refuses a `.bin` built against a different version.

See also: `docu/memory.md` (the authoritative memory map and game binary layout) and `docu/renderer.md` (the renderer the syscall path drives).
