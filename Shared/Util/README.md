# Shared/Util

Small, dependency-free helpers shared by the **console** and by **loaded games**.
Everything here is header-only (`static inline`) and depends only on `<stdint.h>` /
`<stdbool.h>`, so it links into either side with no `.c` file to compile.

Both build trees already put `Shared/` on the include path, so include either the
umbrella header or a single helper:

```c
#include "Util/utils.h"                 /* pulls in all of the helpers below */
/* or, if you only need one: */
#include "Util/util_debounce.h"
#include "Util/util_edge_detector.h"
```

Each helper is a tiny struct you declare **once** with its `*_DECLARE` macro (a
file/function-scope static that keeps its own state) and feed **one sample per poll**.

---

## Debouncer — `util_debounce.h`

A raw (bouncing) value is only accepted once it has held the same value for
`stable_ms`. `debounceUpdate()` takes the current time and raw value and returns the
debounced value. It is **generic over width** — the value is carried as `uint32_t`,
so a `bool` / `uint8_t` / `uint16_t` / `uint32_t` source promotes in and the result
casts back out.

| | |
|---|---|
| `DEBOUNCER_DECLARE(name, stable_ms)` | declare a static debouncer (settle time in ms) |
| `debounceUpdate(&d, now_ms, raw)` | feed one sample → debounced value (`uint32_t`) |
| `debounceGet(&d)` | read the current value without a new sample |

### Example — a button press read via `joystickGetState`

`InputState`'s digital buttons are **already debounced** by the driver (5 ms) and
carry `pressed` / `released` edges, so for a normal press you just use
`in.special1.pressed`. Reach for a Debouncer when you want a *different* settle — e.g.
require a button to be held a bit longer before it counts, to ignore an accidental
brush:

```c
#include "Util/util_debounce.h"
#include "joystick.h"   /* joystickGetState(InputState *) — games call inputGetState() */
#include "sysclock.h"   /* getSysTime() */

DEBOUNCER_DECLARE(s_confirm, 50);   /* special1 must hold 50 ms to count as pressed */

void poll(void)
{
    InputState in;
    joystickGetState(&in);
    bool confirmed = debounceUpdate(&s_confirm, getSysTime(), in.special1.held);
    if (confirmed) { /* ... */ }
}
```

For a source that *isn't* pre-debounced — thresholding an analog stick into a
"button" — it is the same call:
`debounceUpdate(&s_up, getSysTime(), in.left_y > 256)`.

A game uses it identically — `getSysTime()` is just a ConsoleAPI syscall there. And
the source need not be boolean; e.g. debounce a multi-position switch read as a byte:

```c
DEBOUNCER_DECLARE(s_mode, 20);
...
uint8_t mode = (uint8_t)debounceUpdate(&s_mode, getSysTime(), readModeSwitch());
```

Notes: the elapsed compare is wrap-safe (survives the `getSysTime()` rollover).
`stable_ms = 0` accepts a value on the next same-value sample. A `bool` result reads
straight out (`bool held = debounceUpdate(...)` — nonzero → true).

---

## EdgeDetector — `util_edge_detector.h`

Fires on the configured transition of an already-clean boolean. `edgeUpdate()` takes
the current level and returns `true` on the poll the edge occurs.

| | |
|---|---|
| `EDGE_DETECTOR_DECLARE(name, type)` | declare a static detector (`EDGE_RISING` / `EDGE_FALLING` / `EDGE_BOTH`) |
| `edgeUpdate(&e, level)` | feed one sample → `true` on the configured edge |

### Example — fire once when a button goes down

```c
#include "Util/util_edge_detector.h"

EDGE_DETECTOR_DECLARE(s_fire, EDGE_RISING);

void update(void)
{
    bool pressed = edgeUpdate(&s_fire, buttonIsDown());  /* true only on 0 -> 1 */
    if (pressed) { shoot(); }
}
```

> **Gotcha:** `edgeUpdate()` advances its state, so call it **exactly once per poll**
> and **not** behind a short-circuiting `||` / `&&` that might skip it. Compute it
> into a local first:
>
> ```c
> const bool edge = edgeUpdate(&s_fire, stick_up);   /* always runs */
> bool up = dpad_up_pressed || edge;                 /* safe */
> /* NOT: bool up = dpad_up_pressed || edgeUpdate(...);  // skipped when dpad true */
> ```

---

## Validating a click — debounce + edge

A clean single "click" is one fire when a source becomes pressed *after it settles*.
Feed the `Debouncer`'s output into a rising `EdgeDetector`: the debounce kills the
chatter (a *level*), the edge turns it into a one-shot *event*.

```c
#include "Util/utils.h"
#include "joystick.h"   /* joystickGetState(InputState *) — games call inputGetState() */
#include "sysclock.h"

DEBOUNCER_DECLARE(s_up_clean, 15);              /* settle the (bouncy) threshold  */
EDGE_DETECTOR_DECLARE(s_up_click, EDGE_RISING); /* fire once on the 0 -> 1        */

void poll(void)
{
    InputState in;
    joystickGetState(&in);
    bool up_stable = debounceUpdate(&s_up_clean, getSysTime(), in.left_y > 256);
    if (edgeUpdate(&s_up_click, up_stable)) { /* one click per real push */ }
}
```

For the *digital* pad buttons you don't need to build this yourself —
`in.special1.pressed` is already the debounced rising edge from the driver.

## In use

- The console menus use `EdgeDetector` for the analog-stick nav edges
  (`Console/Src/MainMenu/menu_common.c`, `menuPollNav()`).
