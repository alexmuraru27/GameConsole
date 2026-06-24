# ESP-NOW Multiplayer — ground up

Local, wireless, console-to-console multiplayer for up to **four** consoles, built
on **ESP-NOW** over the ESP-01S. No WiFi router, no internet, no pairing screens:
two or more consoles in the same room discover each other and play.

This document builds the stack from the wire up: why ESP-NOW, why the console
*polls* the ESP, the UART command set, the session protocol that lives on the
console (discovery, the join handshake, the heartbeat "ping-pong", the roster),
the game-facing kernel API, and how GameXO uses it.

> See also: `kernel.md` (the SVC syscall ABI these calls ride on), `flasher.md` /
> the Network section of `../CLAUDE.md` (the ESP-01 link this reuses), and
> `../Shared/Esp01s/network_protocol.h` (the wire contract).

---

## 1. Capabilities at a glance

| | |
|---|---|
| Players | up to 4 (one **host** + three **clients**) |
| Transport | ESP-NOW (connectionless 2.4 GHz, ESP-01S), fixed **channel 1** |
| Range | ESP-NOW line-of-sight (tens of metres); no AP needed |
| Discovery | host broadcasts a beacon; joiners list nearby hosts **of the same game** |
| Liveness | heartbeat ping-pong; a peer that goes silent is dropped in ≤ 2.5 s |
| App payload | up to **200 bytes** per message, unicast to a player or broadcast |
| Latency | one message exchanged per frame (~16 ms at 60 FPS) |
| Authority | up to the game; GameXO is **host-authoritative** |
| Security | v1 is unencrypted (LAN-toy threat model) — see §12 |

---

## 2. The big picture

Five layers, three of them new (in **bold**). The guiding principle: **the ESP is
a dumb byte mover; all session intelligence lives on the console.**

```
   ┌──────────────────────────────────────────────────────────────┐
   │  GAME  (e.g. GameXO, unprivileged)                            │
   │    owns the lobby UI + the game's netcode                     │
   │    mpHostStart / mpJoinStart / mpScanHosts / mpJoin           │
   │    mpSend / mpReceive / mpGetRole / mpIsConnected ...         │
   └───────────────┬──────────────────────────────────────────────┘
                   │  SVC syscalls (console_syscalls.c → syscall.c)
   ════════════════╪══════════════ trust boundary ═════════════════
                   ▼
   ┌──────────────────────────────────────────────────────────────┐
   │  CONSOLE / kernel (privileged)                                │
   │  ┌────────────────────────────────────────────────────────┐  │
   │  │ **mp_session.c**  — the brain                          │  │
   │  │   role · peer table · player indices · names           │  │
   │  │   discovery beacons · join handshake · roster          │  │
   │  │   heartbeat liveness · in/out app mailboxes            │  │
   │  └───────────────┬────────────────────────────────────────┘  │
   │   serviced once per frame from gameRuntimeService()           │
   │  ┌───────────────▼────────────────────────────────────────┐  │
   │  │ **espnow_link.c** — typed UART wrappers                │  │
   │  │   (reuses network.c's framing via networkTransact)     │  │
   │  └───────────────┬────────────────────────────────────────┘  │
   └──────────────────┼───────────────────────────────────────────┘
                      │  framed UART @ 923 kbaud (network_protocol.h)
                      │  NP_CMD_MP_BEGIN / MP_END / MP_SERVICE
                      ▼
   ┌──────────────────────────────────────────────────────────────┐
   │  ESP-01S firmware (**main.cpp** ESP-NOW handlers)             │
   │    init/deinit · auto-add peers · send batch · recv ring      │
   │    tags each inbound packet with its source MAC               │
   └──────────────────┬───────────────────────────────────────────┘
                      ▼
        ((  2.4 GHz ESP-NOW — broadcast + unicast, channel 1  ))
```

Why the split? The ESP8266 RF stack is the fragile part of the system (see the
channel-12/13 crash notes in `CLAUDE.md`). Keeping it to "move these bytes, tell me
who sent those bytes" means the protocol logic — which is where bugs and changes
live — sits in C on the STM32, recompiled and debugged with SWO, never reflashed
onto the ESP.

---

## 3. Why the console polls (and doesn't get pushed to)

The existing ESP-01 link is a strict **master/slave** protocol: the console sends
one command and reads exactly one response (`network.c`). That invariant is what
makes the high-baud polled framing safe — there is never an unsolicited byte on the
line to desync on.

ESP-NOW packets, however, arrive at the ESP **asynchronously** (a peer transmits
whenever it likes). To keep the master/slave invariant, we do **not** let the ESP
push frames to the console. Instead:

- The ESP's receive callback drops each inbound packet into a small ring.
- Once per frame the console issues **one** `MP_SERVICE` command that *both* hands
  the ESP a batch of outbound packets *and* drains the ESP's inbound ring in the
  response.

So there is still exactly one command → one response. The 2 KB DMA RX ring in
`usart.c` comfortably covers the gap between polls, and at 60 FPS the inbound
latency is ≤ 16 ms — far below what a turn-based game notices.

---

## 4. The transport: three UART commands

`NETWORK_PROTOCOL_VERSION` was bumped **2 → 3** for these. Framing is unchanged:
`0xA5 0x5A | type | len:u16 | payload | crc16` (see `network_protocol.h`).

| Command | Payload | Response | Effect |
|---|---|---|---|
| `NP_CMD_MP_BEGIN` | `channel:u8` | `NP_RSP_MP_BEGIN { self_mac[6] }` | `esp_now_init`, pin the channel, register the recv callback, add the broadcast peer. Returns the ESP's own MAC — this console's network identity. |
| `NP_CMD_MP_END` | — | `NP_RSP_OK` | `esp_now_deinit`; back to idle station mode. |
| `NP_CMD_MP_SERVICE` | `n:u8, n × {dst_mac[6], len:u8, bytes}` | `NP_RSP_MP_SERVICE { n:u8, n × {src_mac[6], len:u8, bytes} }` | Send each outbound packet (auto-adding unknown MACs as peers; `FF×6` = broadcast), then return the queued inbound packets. |

ESP-NOW mode and the HTTP/update path never run at the same time — you either poll
updates or play. `MP_BEGIN`/`MP_END` bracket the mode.

### The per-frame exchange

```
  console (mp_session)                         ESP-01
        │   MP_SERVICE { out: beacon,heartbeat,app… }
        ├───────────────────────────────────────────►│  for each out pkt:
        │                                             │    esp_now_send(dst, bytes)
        │                                             │  drain recv ring → response
        │   MP_SERVICE { in: src_mac+bytes … }        │
        │◄───────────────────────────────────────────┤
        │  process inbound, update liveness, sweep    │
        ▼  (next frame)                               ▼
                                  ▲
            ESP-NOW recv callback ┘ fires asynchronously between
            polls and enqueues peer packets into the ring
```

---

## 5. The session protocol (console-side)

Everything below lives in `mp_session.c`; the ESP never parses any of it.

Every ESP-NOW packet begins with a **1-byte channel tag**:

| Tag | Meaning |
|---|---|
| `MP_CH_SYS` (0) | session-internal control (this section) |
| `MP_CH_APP` (1) | opaque game payload → delivered to the inbound mailbox |

A `MP_CH_SYS` packet's second byte is the SYS type:

| SYS type | Direction | Body |
|---|---|---|
| `BEACON` | host → broadcast (~200 ms) | `game_id, player_count, name` |
| `JOIN_REQ` | client → host | `name` |
| `JOIN_ACCEPT` | host → client | `assigned_index, <roster>` |
| `JOIN_REJECT` | host → client | `reason` (e.g. roster full) |
| `ROSTER` | host → all (on change) | `<roster>` |
| `HEARTBEAT` | any → broadcast (~500 ms) | — |
| `BYE` | any → broadcast (on leave) | — |

`<roster> = count, count × { index, mac[6], name }`. A client adopts a roster
wholesale, so every console agrees on who is in the game, at which index, with what
name — and any console can address any other by **index** (the OS maps index → MAC).

The **peer table** (`s_peers[MP_MAX_PLAYERS]`) is each console's local view: per
slot, `used / mac / name / index / alive / last_seen_ms`. Your own slot is always
present and alive; the host owns slot 0.

---

## 6. Discovery — the beacon

A host advertises the **running game** (its `.bin` basename, lower-cased) so a
joiner only ever sees hosts it can actually play. The joiner filters beacons by
matching that identity against its own running game.

```
  HOST                              JOINER (browsing)
   │  BEACON{game="gamexo",          │
   │        players=1, name="Ann"}   │   mpJoinStart()
   ├───────── broadcast ────────────►│   collect beacons whose game == mine
   │  (every ~200 ms)                │     → s_hosts[]: {Ann, players=1}
   │                                 │
   │                                 │   mpScanHosts() → game shows the list
   │                                 ▼
                       a host that stops beaconing for >1.5 s
                       drops off the list automatically
```

---

## 7. The join handshake

The joiner picks a host and calls `mpJoin(handle)`. The handshake is request/accept
with idempotent retries — a dropped packet just re-sends on the next service.

```
  JOINER (Bob)                                 HOST (Ann)
    │  mpJoin(handle)                              │
    │  JOIN_REQ{name="Bob"}                         │
    ├──────────── unicast ────────────────────────►│  assign free slot → index 1
    │                                               │  s_peers[1] = {Bob, mac, alive}
    │  JOIN_ACCEPT{ index=1, roster=[Ann@0,Bob@1] } │
    │◄──────────────────────────────────────────────┤  + broadcast ROSTER
    │  role → CLIENT, self_index = 1                 │
    │  adopt roster (everyone, indices, names)       │
    │  start heartbeating                            │
    ▼                                                ▼
  mpGetRole()==CLIENT, mpGetPlayerCount()==2 on both sides
```

If the roster is full the host replies `JOIN_REJECT`, and the joiner's `mpJoin`
result settles back so the game can pick another host. A request is re-sent every
~300 ms until accepted or a 5 s timeout.

---

## 8. Heartbeat — the ping-pong

Liveness is a simple, symmetric ping-pong. Every console broadcasts a `HEARTBEAT`
about every **500 ms**. *Any* packet from a peer (heartbeat, roster, or app data)
refreshes that peer's `last_seen`. A peer not heard from for **2.5 s** (≈ 5 missed
beats) is declared dead.

```
   Ann ──HEARTBEAT──►  …  ──HEARTBEAT──►  …        (every ~500 ms)
   Bob ──HEARTBEAT──►  …  ──HEARTBEAT──►  …

   each side: on any packet from peer  →  last_seen = now, alive = true
              every frame (sweep)      →  if now - last_seen > 2500 ms → DEAD


   ── Bob loses power ──────────────────────────────────────────────►
   Ann hears nothing from Bob …  (≤2.5 s) … sweep marks Bob DEAD
        host: drop slot 1, broadcast a fresh ROSTER
   Game sees mpIsConnected(1) == false  →  "Opponent left"
```

The host turns a dead peer into a roster change (it removes the slot and
re-broadcasts `ROSTER`); a client that loses the host simply sees
`mpIsConnected(0)` go false. A graceful `mpStop()` also sends a `BYE` so peers drop
you immediately instead of waiting out the timeout.

Tuning lives in `mp_session.c`: `MP_BEACON_MS`, `MP_HEARTBEAT_MS`, `MP_TIMEOUT_MS`.

---

## 9. The game-facing kernel API

Games never see MACs or the wire protocol. They drive the whole session through 13
SVC syscalls (`console_syscalls.h`; dispatched and pointer-validated in
`syscall.c`). The console ABI was bumped **3 → 4** for these.

```c
MpRole   mpGetRole(void);                 // NONE / HOST / CLIENT
MpStatus mpHostStart(void);               // advertise THIS game, become index 0
MpStatus mpJoinStart(void);               // listen for hosts of THIS game
int      mpScanHosts(MpHostInfo *o, int); // discovered hosts (name, handle, count)
MpStatus mpJoin(uint8_t handle);          // connect to a scanned host
void     mpStop(void);                    // end the session / release the link

uint8_t  mpGetSelfIndex(void);            // 0 = host
uint8_t  mpGetPlayerCount(void);          // 1..4
bool     mpIsConnected(uint8_t index);    // heartbeat liveness
int      mpGetName(uint8_t index, char*, int);
int      mpGetSelfName(char*, int);

bool     mpSend(uint8_t dst, const void*, uint16_t);  // dst 0xFF = broadcast
int      mpReceive(uint8_t *src, void*, uint16_t);    // 0 = mailbox empty
```

`mpSend` / `mpReceive` are **non-blocking** — they only touch the OS mailboxes. The
actual UART exchange happens once per frame at the inter-frame seam, so a game's
update loop never blocks on the radio.

The display **name** is OS-owned: Settings → *Player Name* (persisted in
`ConsoleSettings`, edited with the on-screen keyboard), defaulting to a
STM32-UID-derived `Console-XXXX` when unset.

---

## 10. Inter-frame servicing

The OS owns the game loop (`scheduler.c`); between a game's `update` and the next
frame it runs `gameRuntimeService()` (`game_loader.c`). While a session is active
this calls `mpSessionService(getSysTime())`, which performs exactly one
`MP_SERVICE` exchange: emit any due beacon/heartbeat, flush queued outbound + SYS
replies, drain inbound, then sweep liveness.

```
  for each frame:
     gameRuntimeService()  ──►  if (mpSessionActive()) mpSessionService(now)
     game update()         ──►  mpSend()/mpReceive() are instant mailbox ops
     game render()
```

When the game exits or crashes, `game_loader.c` calls `mpSessionStop()`
unconditionally, so a session can never leak into the next game.

---

## 11. How GameXO uses it (host-authoritative)

GameXO adds a mode menu (Single / Host / Join) and two lobby screens, then plays
**host-authoritative** — the simplest model that can't desync:

- The **host** owns the board. It applies its own move on its turn, and applies the
  client's *move intent* only when it's the client's turn and the cell is legal.
  After any change it broadcasts a full **STATE snapshot** `{board, turn, result}`,
  and re-broadcasts every ~500 ms so a dropped snapshot self-heals.
- The **client** never mutates the board. On its turn it sends a `MOVE{r,c}` intent
  and renders whatever STATE the host sends back.

```
  CLIENT (O, its turn)                     HOST (X, authoritative)
     │  MOVE{r,c}  (mpSend → host)            │
     ├───────────────────────────────────────►│  legal & client's turn?
     │                                         │    place O, recompute result
     │  STATE{board, turn=host, result}        │    flip turn
     │◄─────────────────────────────────────────┤  broadcast snapshot
     │  adopt board/turn/result                │
     ▼                                         ▼
```

Because the snapshot is tiny and idempotent, there are no acks or sequence numbers.
The netcode is isolated in `Apps/GameXO/Src/xo_net.c`; the rest of GameXO just reads
the synced board (no AI in multiplayer — the opponent is the remote human).

---

## 12. Memory, timing, and limits

- **Memory.** The session manager + transport cost ~5 KB of `CONSOLE_RAM`
  (peer/host tables, app mailboxes, a single shared service-staging buffer). The
  outbound batch is fully serialized before the inbound is parsed, so one buffer
  serves both directions.
- **Per-frame cost.** `mpSessionService` blocks the frame for one UART round-trip
  (~1 ms at 923 kbaud for small batches). Negligible for turn-based games; a future
  fast-action game would want this profiled.
- **Channel.** Both peers must share **channel 1** (pinned in `MP_BEGIN`, inside the
  ESP firmware's FCC 1–11 range). There is no AP to negotiate one.
- **Security.** v1 is **unencrypted** ESP-NOW on an open channel — the same LAN-toy
  threat model as the plaintext WiFi credentials. ESP-NOW PMK/LMK encryption is the
  obvious next step.
- **Not yet done.** In-game rematch (v1 is one match per connection), >2-player
  games on top of the 4-player framework, and reconnection of a dropped peer.

---

## 13. Bringing it up (needs two consoles)

Multiplayer can only be exercised with **two consoles + two ESP-01s**.

1. `make esp` then `make deploy`; flash the new ESP firmware to **both** units via
   Settings → *Upgrade WiFi module*. `make flash` both consoles.
2. On `make flashswo` you should see the ESP boot `proto v3` and the console log
   `ESP synced (proto v3)`.
3. Set a distinct **Player Name** on each (Settings → *Player Name*).
4. Launch GameXO on both: one picks **Host Game**, the other **Join Game**. The
   joiner's list should show the host by name (filtered to GameXO). On the
   `LOGGER_MP` SWO channel you'll see `peer joined`, roster size 2.
5. Play a full game — moves propagate and the result agrees on both screens. Pull
   power on one mid-game; the other shows **OPPONENT LEFT** within ~2.5 s (the
   heartbeat drop).
