# Creating a Multiplayer Game

How to build a **local wireless multiplayer** game for the console — the design
philosophy, the lobby-to-gameplay flow, the exact call sequence, and how to weave
the network exchange into your `update()` loop without ever stalling a frame.

This is the **game author's how-to**. Two companion docs sit underneath it:

- [`README.md`](README.md) — creating a game in general (the three callbacks, the
  binary header, the full single-player API). Read it first.
- [`../espnow.md`](../espnow.md) — the **stack internals**: the ESP-NOW transport,
  the UART command set, the session protocol (discovery, join handshake, heartbeat),
  the peer table. You do **not** need it to write a game, but it explains *why* the
  rules below are what they are.

The reference implementation is **GameXO** (`Apps/GameXO/`) — a host-authoritative
tic-tac-toe whose entire netcode is one small file, `Src/xo_net.c`, driven from the
phase machine in `Src/game_state_manager.c`.

---

## 1. Philosophy — five rules to internalize

Multiplayer here is deliberately small and opinionated. Five rules cover almost
everything:

**1. The console owns the session; you own the lobby and the netcode.** The OS
handles discovery, the peer table, player-index assignment, the join handshake, and
heartbeat liveness. You handle the lobby UI (who's hosting, who joined, "start") and
the *meaning* of the bytes you exchange. You never see a MAC address, a channel, or
a wire frame — peers are small **player indices** (`0` = host, `1..3` = joiners).

**2. The radio is a per-frame mailbox, not a socket.** `mpSend()` and `mpReceive()`
only touch OS mailboxes and return instantly — they **never block**. The actual
ESP-NOW exchange happens *once per frame*, pipelined across the inter-frame seam by
the OS (it overlaps `render()`). The practical consequence: **inbound data is one
frame old** (~16 ms at 60 FPS), and a message you `mpSend()` this frame leaves the
console at the end of this frame and is seen by the peer a frame *after that*. Design
for that latency; never assume a send is reflected back instantly.

**3. The link is best-effort — there are no acks.** ESP-NOW is connectionless and
the OS does **not** retransmit app messages at the application layer. A packet can
drop. So do **not** build a protocol of deltas that must all arrive ("move 5 came
but I missed move 4"). Instead send **compact, idempotent state** and **re-broadcast
it periodically** so a lost packet self-heals on the next one. This single decision
removes acks, sequence numbers, and reconnection logic from your game.

**4. Pick an authority model — host-authoritative is the default.** Decide who owns
the truth so two consoles can't disagree:
   - **Host-authoritative** (recommended, what GameXO does): the host owns all game
     state. Clients send *intents* ("I want to play cell (r,c)"); the host validates,
     applies, and broadcasts the new state. A client never mutates shared state — it
     renders whatever the host sends. Cannot desync, needs no acks. Cost: a client's
     action shows up after one host round-trip (~2 frames). Perfect for turn-based and
     most casual real-time games.
   - **Lockstep** (advanced): every peer runs the same deterministic simulation on the
     same inputs. Tiny bandwidth, but one dropped/late input stalls everyone and any
     nondeterminism desyncs. Only worth it for fast games with many entities.
   - Start host-authoritative. Reach for anything else only when you've measured a
     reason to.

**5. Discovery is per-game and by name.** A host advertises the **running `.bin`**
(its basename), so a joiner only ever sees hosts it can actually play. Both consoles
must run the **same game** to find each other. Players are identified by a console-wide
**display name** (Settings → *Player Name*), which you read with `mpGetName()`.

---

## 2. Console capabilities & limits

The hard numbers — these are enforced in code, not advice:

| Capability | Limit | Where |
|---|---|---|
| Players per session | **4** (1 host + 3 clients) | `MP_MAX_PLAYERS` |
| Bytes per message | **≤ 200** (`mpSend` rejects larger) | `MP_MSG_MAX` |
| Outbound queue depth | **4** unsent messages (`mpSend` returns `false` when full) | `MP_OUTBOX_SLOTS` |
| Inbound queue depth | **4** buffered messages — **drain every frame or new ones are dropped** | `MP_INBOX_SLOTS` |
| Packets per frame, each way | **~5**, shared with OS beacon/heartbeat/roster traffic | `MP_SVC` |
| Whole outbound batch | **≤ 896 B/frame** | `MP_CMD_MAX` |
| Inbound latency | **one frame** (~16 ms @ 60 FPS) | inter-frame seam |
| Delivery | **best-effort**, no app-layer acks/retransmit | ESP-NOW |
| Display name | **≤ 16 chars** | `MP_NAME_MAX` |
| Transport | ESP-NOW, 2.4 GHz, **channel 1**, no AP/router | — |
| Security | **unencrypted** (v1, LAN-toy threat model) | — |

**Max transfer rate — read this honestly.** This is a *control channel for game
state*, not a bulk pipe. The ceiling is structural: a handful of small packets per
frame, capped at ~896 B of total outbound per frame and 200 B per message, and the
OS's own beacon/heartbeat packets share the per-frame budget. At 60 FPS that works
out to a few hundred bytes to low single-digit KB of useful app payload per frame —
on the order of **tens of KB/s aggregate, best-effort**. That is plenty for board
state, player positions, inputs, scores. It is *not* enough (and not meant) for
streaming audio, textures, or level data — ship those in the `.pak` on the SD card
instead, and send only the small, changing game state over the radio.

A corollary of the depth limits: **drain your inbox every frame** (a `while
(mpReceive(...))` loop), and don't try to push more than a couple of messages per
frame. If `mpSend()` returns `false`, the outbox is full — drop the message and rely
on your next periodic state broadcast rather than retrying in a tight loop.

---

## 3. The game flow

A multiplayer game is a small state machine layered on top of your normal gameplay.
GameXO's phases (`game_state_manager.c`) are a good template:

```
                         ┌──────────────────────┐
                         │   MODE SELECT        │   Single / Host / Join
                         │  (your menu)         │
                         └───┬───────┬──────┬───┘
              Single player  │       │ Host │ Join
            ┌────────────────┘       │      └────────────────┐
            ▼                        ▼                        ▼
   ┌─────────────────┐     ┌───────────────────┐   ┌────────────────────┐
   │  normal SP game │     │  HOST LOBBY       │   │  JOIN BROWSE        │
   │  (no mp* calls) │     │  mpHostStart()    │   │  mpJoinStart()      │
   └─────────────────┘     │  wait for a peer  │   │  mpScanHosts() each │
                           │  (count >= 2)     │   │  frame; pick one;   │
                           │  press A → start  │   │  mpJoin(handle)     │
                           └─────────┬─────────┘   │  wait role==CLIENT  │
                                     │             └──────────┬─────────┘
                                     └──────────┬─────────────┘
                                                ▼
                                     ┌────────────────────┐
                                     │   PLAYING          │   exchange state
                                     │  collect → logic   │   each frame
                                     │  → send each frame │
                                     │  watch mpIsConnected│
                                     └──────────┬─────────┘
                                                ▼
                                     ┌────────────────────┐
                                     │   END / TEARDOWN   │   mpStop()
                                     └────────────────────┘
```

The whole session lives inside one launch of your game: the OS force-stops any
session when you `gameExit()` or crash, so a session can never leak into the next
game — but call `mpStop()` yourself when leaving a match so peers drop you cleanly
(it sends a goodbye) instead of waiting out the 2.5 s heartbeat timeout.

---

## 4. Driving the session — the call sequence

All of these are SVC syscalls; include `game_console_api.h` and call them directly.

### Hosting

```c
/* In your "Host" menu choice: */
if (mpHostStart() == MP_OK)
{
    s_phase = PHASE_HOST_LOBBY;   /* now advertising THIS game, you are index 0 */
}

/* Each frame in the lobby: wait for someone to join, then let the host start. */
static void updateHostLobby(void)
{
    if (mpGetPlayerCount() >= 2U && joystickGetSpecialBtn1())
    {
        startMatch(/* as_host = */ true);
    }
}
```

### Joining

`mpJoin()` is asynchronous — it returns `MP_PENDING` while the handshake settles.
Don't block on it; poll `mpGetRole()` until it flips to `MP_ROLE_CLIENT`.

```c
/* In your "Join" menu choice: */
if (mpJoinStart() == MP_OK)
{
    s_phase = PHASE_JOIN_BROWSE;
}

/* Each frame in the browse screen: refresh the discovered-host list. */
static void updateJoinBrowse(void)
{
    MpHostInfo hosts[8];
    int n = mpScanHosts(hosts, 8);        /* same-game hosts only; refreshes live */
    if (n < 0) { n = 0; }

    /* ... move a cursor over hosts[0..n-1], showing hosts[i].name ... */

    if (selected && joystickGetSpecialBtn1())
    {
        MpStatus st = mpJoin(hosts[cursor].handle);
        if (st == MP_PENDING || st == MP_OK)
        {
            s_joining = true;             /* request in flight; now poll the role */
        }
        /* st == MP_FULL → that lobby is full; pick another host */
    }

    if (s_joining && mpGetRole() == MP_ROLE_CLIENT)
    {
        startMatch(/* as_host = */ false);  /* accepted — we are in */
    }
}
```

### Starting the match and reading the roster

Once connected (host pressed start, or a client's role became `CLIENT`), capture who
you are and who you're playing:

```c
static void startMatch(bool as_host)
{
    uint8_t self = mpGetSelfIndex();      /* 0 if host */
    uint8_t opponent = findOpponent();    /* first connected index that isn't self */

    char self_name[MP_NAME_MAX + 1];
    char opp_name[MP_NAME_MAX + 1];
    mpGetSelfName(self_name, sizeof(self_name));
    mpGetName(opponent, opp_name, sizeof(opp_name));

    /* host owns the initial state and broadcasts it; client waits for the first
       snapshot (see §5/§8). */
    s_phase = PHASE_PLAYING;
}

static uint8_t findOpponent(void)
{
    uint8_t self = mpGetSelfIndex();
    for (uint8_t i = 0U; i < MP_MAX_PLAYERS; i++)
    {
        if (i != self && mpIsConnected(i))
        {
            return i;
        }
    }
    return self;
}
```

### Tearing down

```c
mpStop();        /* ends the session, releases the ESP-NOW link, says goodbye */
```

Call it when the player backs out of a match or the game ends. It is safe to call
even when no session is active.

---

## 5. Interleaving the network with your game logic

Everything network-related happens inside **`update()`** — keep `render()` pure
(read game state, submit sprites, nothing else). The OS performs the actual radio
exchange *between* your `update()` and the next frame, so all you do in `update()`
is read the inbox and fill the outbox.

Use this exact order every frame while in the `PLAYING` phase:

```c
static void updatePlaying(void)
{
    /* 1. DRAIN the inbox first — empty it fully so the 4-slot queue never overflows.
          Apply each message according to your authority model. */
    uint8_t src;
    uint8_t buf[MP_MSG_MAX];
    int n;
    while ((n = mpReceive(&src, buf, sizeof(buf))) > 0)
    {
        handleMessage(src, buf, n);       /* host: apply intents; client: adopt state */
    }

    /* 2. LIVENESS — react to a peer dropping before doing anything turn-related. */
    if (!mpIsConnected(s_opp_index))
    {
        s_opp_left = true;
        goToEnd();
        return;
    }

    /* 3. LOCAL INPUT → produce an action. What you do with it depends on authority:
          - host: apply locally, recompute state, broadcast the new snapshot
          - client: send an INTENT and wait for the host's snapshot (don't apply) */
    handleLocalInput();

    /* 4. PERIODIC RE-BROADCAST (host only): re-send the authoritative state every
          ~500 ms so a dropped snapshot self-heals. This is what makes the link
          best-effort-safe with no acks. */
    if (s_is_host && (getSysTime() - s_last_state_ms) > 500U)
    {
        broadcastState();
    }
}
```

Why this order: draining first means you act on the freshest inbound state; the
liveness check short-circuits before you waste work on a dead peer; sending last
means your outbound reflects the input you just processed. Because inbound is one
frame old and your own send goes out at frame end, **never** read back your own
message to confirm it — trust your local model (host) or wait for the authority
(client).

---

## 6. The network flow — calls, waits, and timing

You call `mpSend`/`mpReceive`; the OS turns that into exactly **one** `MP_SERVICE`
round-trip with the ESP per frame, **pipelined across the frame** so the radio
overlaps `render()` and your loop never busy-waits on it. Here is what actually
happens around your two callbacks each frame.

### Per frame: the four seams

```
 A console in a session — ONE frame. The OS owns the loop; you write the two
 middle callbacks (update / render); the OS runs a seam on either side of them.

   gameRuntimeCollect()    your update()      gameRuntimeFlush()     your render()
  ┌───────────────────┐  ┌───────────────┐  ┌──────────────────┐  ┌──────────────┐
  │ mpSessionCollect() │  │ mpReceive() ×N│  │ mpSessionFlush() │  │ draw the     │
  │  • read LAST       │  │ mpSend()    ×M│  │  • due beacon /  │  │ world        │
  │    frame's reply   │  │               │  │    heartbeat     │  │              │
  │    from RX DMA ring│  │ both are      │  │  • pack outbox   │  │  ⟵ the ESP   │
  │  • run SYS proto   │  │ INSTANT       │  │  • ARM TX (async │  │   round-trip │
  │  • app payloads →  │  │ mailbox ops — │  │    DMA) then     │  │   runs and   │
  │    your inbox      │  │ never block   │  │    RETURN now    │  │   COMPLETES  │
  └─────────┬─────────┘  └───────────────┘  └────────┬─────────┘  │   here       │
            │                                         │            └──────┬───────┘
            │ reads the request armed a frame ago     │ arms THIS frame's │
            │ (ring already full → no CPU wait)       │ request, returns  │
            └───────────────────► time ───────────────┴───────────────────┘
                                            the reply streams into the RX DMA
                                            ring while render() draws — for free
```

### Where are the waits?

Almost nowhere on the CPU. The TX is fired over DMA and returns immediately; the
reply lands in the 2 KB RX DMA ring on its own. The **only** point that can block is
the next frame's `mpSessionCollect()`, and only if `render()` finished *before* the
reply fully arrived (a 250 ms guard timeout bounds even that). For a normal frame the
reply is already in the ring, so `collect` just reads it. Net effect: **inbound is
one frame old (~16 ms at 60 FPS), and that one frame is the entire cost** — there is
no synchronous radio call anywhere in your code path. Exactly one request is
outstanding at a time (armed at `flush`, consumed at the next `collect`), which keeps
the ESP's one-command/one-reply invariant.

### End to end: when does a peer see what you sent?

```
 Console A                        ESP-A    ((2.4 GHz))  ESP-B            Console B
  frame N                          │                      │
   update(): mpSend(X) ─► outbox   │                      │
   flush(): arm MP_SERVICE ───────►│ esp_now_send(X) ───────►│ recv → ESP ring
   render() (overlaps)             │      ~1-2 ms         │   (held until B polls)
 ─────────────────────────────────────────────────────────────────────────────────
                                   │                      │ frame M  (B's next frame)
                                   │                      │  flush(): MP_SERVICE poll
                                   │                      │◄─ drains ring into reply
                                   │                      │ frame M+1
                                   │                      │  collect(): X → B's inbox
                                   │                      │  update(): mpReceive()==X
```

A message is visible to a peer **1–2 of its frames** after you send it (~16–33 ms one
way at 60 FPS), set by frame-phase alignment, not by the radio (air time is ~1–2 ms).
For host-authoritative play, a client's action → host applies → client sees the
result is **two hops (~2–4 frames, ~33–66 ms)**. Budget for it: it is imperceptible
turn-based, and hideable in real-time games with prediction/interpolation (§9).

---

## 7. A message codec — the type-tag pattern

Wrap `mpSend`/`mpReceive` in a tiny codec so the rest of your game speaks in typed
messages, not raw bytes. The convention: **first byte is a message-type tag**, the
rest is that type's payload. This is all of `xo_net.c`:

```c
typedef enum { MSG_MOVE = 1, MSG_STATE = 2 } MsgType;

/* client -> host (index 0): "I want to play (r,c)"  — 3 bytes */
void netSendMove(uint8_t r, uint8_t c)
{
    const uint8_t buf[3] = { (uint8_t)MSG_MOVE, r, c };
    mpSend(0U, buf, sizeof(buf));                 /* unicast to the host */
}

/* host -> everyone: the authoritative snapshot — 12 bytes */
void netBroadcastState(const uint8_t board[9], uint8_t turn, uint8_t result)
{
    uint8_t buf[12];
    buf[0] = (uint8_t)MSG_STATE;
    memcpy(&buf[1], board, 9U);
    buf[10] = turn;
    buf[11] = result;
    mpSend(MP_BROADCAST_INDEX, buf, sizeof(buf));  /* 0xFF = all peers */
}

/* decode one inbound message; validate the length for the tag before trusting it */
bool netPoll(Msg *out, uint8_t *src_index)
{
    uint8_t buf[MP_MSG_MAX];
    int n = mpReceive(src_index, buf, sizeof(buf));
    if (n <= 0) { return false; }
    switch (buf[0])
    {
    case MSG_MOVE:  if (n >= 3)  { /* fill out from buf[1],buf[2] */  return true; } break;
    case MSG_STATE: if (n >= 12) { /* fill out from buf[1..11]   */   return true; } break;
    default: break;
    }
    return false;   /* unknown tag or short frame — ignore it, never trust length */
}
```

Always bounds-check the received length against the tag before reading the payload —
a peer (or a corrupted frame) can hand you anything.

---

## 8. Worked example — host-authoritative turn-based (GameXO)

GameXO's full model, in words and one diagram:

- The **host** owns the board. On its own turn it applies its move locally. On the
  client's turn it accepts a `MOVE` intent **only if** it really is the client's turn
  and the cell is legal. After *any* change it recomputes the result and broadcasts a
  `STATE{board, turn, result}` snapshot — and re-broadcasts it every ~500 ms.
- The **client** never touches the board. On its turn it sends a `MOVE` intent and
  then renders whatever `STATE` the host sends back. Before the first snapshot
  arrives it shows "waiting".

```
  CLIENT (its turn)                          HOST (authoritative)
     │  netSendMove(r,c) ──► mpSend(0,…)        │
     ├──────────────────────────────────────────►│  legal & client's turn?
     │                                            │    apply, recompute result,
     │   STATE{board, turn=host, result}          │    flip turn
     │◄────────────────────────────────────────────┤  netBroadcastState(…)
     │  adopt board/turn/result, render            │
     ▼                                            ▼
   both screens now show the same board; no acks, no sequence numbers
```

Notice what's *absent*: no acks, no retransmit, no sequence numbers, no
reconnection. The snapshot is tiny and idempotent and is re-sent periodically, so a
dropped packet simply corrects itself on the next broadcast. That is the whole payoff
of rules 3 and 4. The host-side and client-side per-frame loops are
`updatePlayingHost()` / `updatePlayingClient()` in `game_state_manager.c`; the codec
is `xo_net.c`. Read those two together — under ~250 lines total — as your starting
point.

---

## 9. Real-time games — continuous state streaming

Turn-based games (§8) broadcast *on change*. An action game — a Bomberman, a
top-down racer, a twin-stick shooter — has state changing **every frame for every
player**, so the cadence is different. The four rules from §1 are unchanged
(host-authoritative, idempotent state, best-effort, address-by-index); what you add
is **three techniques** that turn a sparse, one-frame-late, lossy link into smooth
motion: a network tick decoupled from the frame rate, tight state packing, and
client-side interpolation/prediction.

### 9.1 Decouple the network tick from the frame rate

You render at ~60 FPS but you must **not** send 60 snapshots/sec — the per-frame
budget (§2: ~5 packets/frame, ≤ 896 B) won't sustain a full broadcast to three peers
every frame, and you don't need it to. Pick a **network tick** of **15–30 Hz** and
gate all sends on it with `getSysTime()`; rendering stays at 60 FPS and interpolation
(§9.4) fills the gaps:

```c
#define NET_TICK_MS 50U   /* 20 Hz network update; render still runs at 60 FPS */

if (getSysTime() - s_last_tick_ms >= NET_TICK_MS)
{
    s_last_tick_ms += NET_TICK_MS;
    netTick();   /* host: broadcast a snapshot. client: send this player's input. */
}
```

### 9.2 Send inputs, not moves — the host simulates

A real-time client doesn't send discrete moves; it sends its **current input** (a
button/axis bitmask) every tick. The host runs the simulation for *everyone* from the
latest input it holds per player, then broadcasts the authoritative world. A missing
input tick just means the host reuses that player's last input (dead-reckoning) —
exactly the graceful degradation you want.

```c
/* client -> host, every net tick (tiny) */
INPUT  = { tag=1, seq:u8, buttons:u8 }

/* host -> everyone, every net tick (the whole world, idempotent) */
STATE  = { tag=2, tick:u16, players[N]×{x,y,vx,vy,flags}, world... }
```

### 9.3 Pack the snapshot tight — stay under 200 bytes

Always send the **full** state (idempotent: a dropped packet self-heals on the next
tick — never send deltas that must all arrive), and quantize hard so it fits one
`mpSend`:

- **Bomberman (13×11 grid):** tiles `143 × 2 bits ≈ 36 B` + up to 8 bombs `× {x,y,timer}=3 B` `= 24 B` + 4 players `× {x,y,flags}=3 B` `= 12 B` → **~72 B**.
- **Racer:** 4 cars `× {x:u16, y:u16, heading:u8, flags:u8} = 6 B` → **24 B**, plus a lap/checkpoint byte each.

Quantize positions to bytes (grid cells) or fixed-point `u16` (sub-pixel). If a full
snapshot genuinely can't fit 200 B, split it across a couple of messages within the
~5-packets/frame budget — but shrink first; you almost never need to.

### 9.4 Interpolate remote entities — smooth motion from sparse updates

At 20 Hz, a remote entity drawn raw would jump 20 times/sec. Instead, buffer the last
**two** snapshots and render each remote entity at a time **one tick in the past**,
linearly interpolating between them. You trade ~1 tick (50 ms) of remote latency for
fluid 60 FPS motion — invisible in practice, and far better than stutter:

```c
/* remote entity: lerp between the previous and latest snapshot */
float t = (float)(getSysTime() - latest.recv_ms) / (float)NET_TICK_MS;  /* 0..1 */
if (t > 1.0f) { t = 1.0f; }                 /* clamp if the next tick is late */
draw_x = prev.x + (latest.x - prev.x) * t;
draw_y = prev.y + (latest.y - prev.y) * t;
```

Render the **local** player with prediction (§9.5); render **remote** players with
this interpolation.

### 9.5 Predict the local player — hide your own round-trip (advanced)

Your own avatar still has the ~2–4-frame host round-trip (§6), which feels laggy on a
fast game. Fix it with **client-side prediction**: apply your input locally the
instant you read it (run the same movement code the host runs), then **reconcile**
when the authoritative `STATE` arrives — if the host's position for you differs from
your prediction, snap or ease to it. Keep a small ring of your recent inputs so you
can re-simulate forward from the last host-confirmed position. This is the standard
"host-authoritative + client prediction" model. It's **optional**: a grid Bomberman
usually feels fine with interpolation alone; a twitchy racer wants prediction.

### 9.6 Don't reach for lockstep

It's tempting for action games, but this transport is best-effort and one frame late:
a single dropped or late input stalls *every* console, and any nondeterminism desyncs
with no recovery path. Host-authoritative + interpolation degrades gracefully — a
drop is one skipped tick, healed by the next snapshot. Lockstep does not. Skip it.

### 9.7 The real-time loop, end to end

```
 REAL-TIME host-authoritative loop (Bomberman / racer), per NET_TICK (~20 Hz)

  CLIENTS (each tick)                        HOST (owns the whole simulation)
     │  INPUT{buttons} ─► mpSend(host) ───────►│ collect newest INPUT per player
     │                                         │ step physics/game for ALL players
     │                                         │   (missing input → reuse last)
     │  STATE{tick, players[], world}          │ pack tight (<200 B), broadcast
     │◄─────────────────────────────────────────┤ mpSend(MP_BROADCAST_INDEX, …)
     │  buffer last 2 STATEs                     │
     │  render LOCAL  = predict + reconcile      │
     │  render REMOTE = interpolate (1 tick back) ▼
     ▼
  60 FPS-smooth even though state arrives at 20 Hz, and a dropped packet is
  just one skipped tick — the next STATE re-syncs the whole world
```

**Budget check.** At 20 Hz with 4 players: the host broadcasts 1 packet/tick and
receives 3 input packets/tick; each is well under 200 B. Spread over three 60 FPS
frames per tick, that's ≪ the ~5-packets/frame ceiling — even with the OS's own
sparse beacon (~200 ms) and heartbeat (~500 ms) sharing the budget. Real-time play
fits comfortably; the limit you'll actually feel is latency, which §9.4–9.5 hide, not
bandwidth.

---

## 10. Checklist & gotchas

- [ ] **Drain the inbox every frame** with a `while (mpReceive(...) > 0)` loop — the
      queue is only 4 deep and drops new messages when full.
- [ ] **Keep each message ≤ 200 bytes.** Need more? Send compact state, not bulk; put
      big static data in the `.pak`. To send something genuinely large, chunk it
      across frames yourself.
- [ ] **Don't rely on any single packet arriving.** Make shared state idempotent and
      re-broadcast it periodically (~500 ms) so drops self-heal.
- [ ] **Handle peer drop** every frame with `mpIsConnected(index)` — show an
      "opponent left" state instead of hanging.
- [ ] **Do all networking in `update()`**, never in `render()`. Both `mpSend` and
      `mpReceive` are non-blocking, so this never costs you a frame.
- [ ] **Poll, don't block, on `mpJoin()`** — handle `MP_PENDING` by watching
      `mpGetRole()` flip to `MP_ROLE_CLIENT`; handle `MP_FULL`.
- [ ] **Tag every message** with a leading type byte and **bounds-check the length**
      for that tag before reading the payload.
- [ ] **`mpStop()` when leaving a match** so peers drop you immediately. (The OS also
      force-stops on `gameExit()`/crash, but be explicit.)
- [ ] **Both consoles must run the same `.bin`** to discover each other, and each
      player should set a distinct **Player Name** (Settings → Player Name).
- [ ] **Test with two real consoles** — multiplayer can't be exercised on one. Watch
      the `LOGGER_MP` SWO channel for `peer joined` / roster size.
- [ ] **Real-time game?** Decouple a 15–30 Hz network tick from the 60 FPS render
      (§9.1), send full quantized snapshots under 200 B (§9.3), and **interpolate
      remote entities** (+ optionally **predict** the local one) to hide the
      one-frame latency and dropped packets (§9.4–9.5).

---

## 11. Reference

- [Creating a Game + Console API](README.md) — the general game guide and full API
- [ESP-NOW multiplayer (stack internals)](../espnow.md) — transport, protocol,
  discovery, join handshake, heartbeat, peer table, and the sequence diagrams
- [Kernel / game isolation](../kernel.md) — the SVC syscall ABI these calls ride on
- [GameXO source](../../Apps/GameXO/) — the reference host-authoritative game
  (`Src/xo_net.c` + the multiplayer phases in `Src/game_state_manager.c`)
