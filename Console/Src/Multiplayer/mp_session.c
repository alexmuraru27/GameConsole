#include "Multiplayer/mp_session.h"
#include "Multiplayer/mp_wire.h"
#include "Network/espnow_link.h"
#include "network_protocol.h"
#include "Loader/loader.h"
#include "ff.h"
#include "SettingsStorage/console_settings_storage.h"
#include "Peripherals/sysclock.h"
#include "Peripherals/systime.h"
#include "Logger/logger.h"

#include <string.h>
#include <stdio.h>
#include <ctype.h>

/*
 * OS-level multiplayer session manager. See mp_session.h / docu/espnow.md.
 *
 * Layering recap: the ESP (espnow_link.c) just moves bytes and tags each inbound
 * packet with its source MAC. This module turns that flat packet stream into a
 * session — players, indices, names, liveness — entirely on the console. A game
 * never sees a MAC; it addresses peers by index and discovers hosts by handle.
 *
 * Every ESP-NOW packet starts with a 1-byte CHANNEL tag:
 *   MP_CH_SYS  session protocol (beacon / join / roster / heartbeat / bye)
 *   MP_CH_APP  opaque game payload, delivered to the inbound mailbox
 * A SYS packet's second byte is the SYS message type below.
 */

/* Fixed ESP-NOW radio channel (must be within the ESP firmware's FCC 1-11). Both
 * peers must share it; there is no AP to negotiate one. */
#define MP_CHANNEL 1U

#define MP_MAX_HOSTS 8U /* discovered-host list capacity in join/browse mode (MP_GAME_ID_MAX is in mp_wire.h) */

/* Session cadence + timeouts (ms). */
#define MP_BEACON_MS 200U          /* host advertises this often                    */
#define MP_HEARTBEAT_MS 500U       /* ping-pong: every peer beats this often        */
#define MP_TIMEOUT_MS 2500U        /* declare a peer dead after this silence (5 beats) */
#define MP_HOST_STALE_MS 1500U     /* drop a host from the browse list after this   */
#define MP_JOINREQ_RESEND_MS 300U  /* re-send a pending join request this often     */
#define MP_JOIN_TIMEOUT_MS 5000U   /* give up a pending join after this             */

/* Mailbox / staging depths (kept lean — CONSOLE_RAM is dominated by the renderer). */
#define MP_SYSOUT_SLOTS 6U
#define MP_SYSOUT_DATA_MAX 110U /* largest SYS message (a 4-player roster ~99 B)    */
#define MP_INBOX_SLOTS 4U
#define MP_OUTBOX_SLOTS 4U
#define MP_SVC 5U /* packets staged per service exchange (one buffer, out then in) */

#define STM32_UID_BASE 0x1FFF7A10U /* 96-bit unique device id (for the default name) */

/* The CHANNEL tags, SYS message types and the roster/beacon byte format live in
 * mp_wire.h — this module is their sole user on the console side. */

static const uint8_t MP_BROADCAST_MAC[NP_MP_MAC_LEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

typedef struct
{
    bool used;
    bool alive;
    uint8_t index;
    uint8_t mac[NP_MP_MAC_LEN];
    char name[MP_NAME_MAX + 1U];
    uint32_t last_seen_ms;
} MpPeer;

typedef struct
{
    bool used;
    uint8_t mac[NP_MP_MAC_LEN];
    char name[MP_NAME_MAX + 1U];
    uint8_t player_count;
    uint32_t last_seen_ms;
} MpDiscovered;

typedef struct
{
    uint8_t mac[NP_MP_MAC_LEN];
    uint8_t len;
    uint8_t data[MP_SYSOUT_DATA_MAX];
} MpSysOut;

typedef struct
{
    uint8_t peer_index;
    uint8_t len;
    uint8_t data[MP_MSG_MAX];
} MpAppMsg;

/* ---- session state ---- */
static bool s_active;
static MpRole s_role;
static uint8_t s_self_index;
static uint8_t s_self_mac[NP_MP_MAC_LEN];
static char s_self_name[MP_NAME_MAX + 1U];
static char s_game_id[MP_GAME_ID_MAX + 1U];

static MpPeer s_peers[MP_MAX_PLAYERS];
static MpDiscovered s_hosts[MP_MAX_HOSTS];

/* pending join (client, before MP_SYS_JOIN_ACCEPT lands) */
static bool s_join_pending;
static uint8_t s_pending_host_mac[NP_MP_MAC_LEN];
static uint32_t s_join_started_ms;
static uint32_t s_last_joinreq_ms;

/* periodic-send timers */
static uint32_t s_last_beacon_ms;
static uint32_t s_last_heartbeat_ms;

/* SYS outbox (replies generated while processing inbound; flushed next service) */
static MpSysOut s_sysout[MP_SYSOUT_SLOTS];
static uint8_t s_sysout_head, s_sysout_tail;

/* app mailboxes (game <-> network) */
static MpAppMsg s_inbox[MP_INBOX_SLOTS];
static uint8_t s_inbox_head, s_inbox_tail;
static MpAppMsg s_outbox[MP_OUTBOX_SLOTS];
static uint8_t s_outbox_head, s_outbox_tail;

/* Per-service packet staging — ONE buffer for both directions. The outbound batch
 * is fully serialized (mpSessionFlush) before any inbound is written into it
 * (mpSessionCollect of the next frame), so the sent and received batches share
 * storage (see espnow_link.h). */
static EspNowPacket s_svc[MP_SVC];

/* True between an mpSessionFlush (request armed over the async TX) and the
 * mpSessionCollect that consumes its reply. Guards the first frame (nothing sent
 * yet) and keeps the one-command/one-reply invariant across the split. */
static bool s_request_pending;

/* ---- small helpers ---- */

static bool macEqual(const uint8_t *a, const uint8_t *b)
{
    return memcmp(a, b, NP_MP_MAC_LEN) == 0;
}

static void macCopy(uint8_t *dst, const uint8_t *src)
{
    memcpy(dst, src, NP_MP_MAC_LEN);
}

static void copyName(char *dst, const uint8_t *src, uint8_t len)
{
    if (len > MP_NAME_MAX)
    {
        len = MP_NAME_MAX;
    }
    memcpy(dst, src, len);
    dst[len] = '\0';
}

static int findPeerByMac(const uint8_t *mac)
{
    for (uint8_t i = 0U; i < MP_MAX_PLAYERS; i++)
    {
        if (s_peers[i].used && macEqual(s_peers[i].mac, mac))
        {
            return (int)i;
        }
    }
    return -1;
}

static int freePlayerSlot(void)
{
    /* Joiners take indices 1..MP_MAX_PLAYERS-1 (the host is 0). */
    for (uint8_t i = 1U; i < MP_MAX_PLAYERS; i++)
    {
        if (!s_peers[i].used)
        {
            return (int)i;
        }
    }
    return -1;
}

static uint8_t playerCount(void)
{
    uint8_t n = 0U;
    for (uint8_t i = 0U; i < MP_MAX_PLAYERS; i++)
    {
        if (s_peers[i].used)
        {
            n++;
        }
    }
    return n;
}

static void deriveGameId(const char *fname, char *out)
{
    const char *dot = strrchr(fname, '.');
    size_t base_len = dot ? (size_t)(dot - fname) : strlen(fname);
    if (base_len > MP_GAME_ID_MAX)
    {
        base_len = MP_GAME_ID_MAX;
    }
    for (size_t i = 0U; i < base_len; i++)
    {
        out[i] = (char)tolower((unsigned char)fname[i]);
    }
    out[base_len] = '\0';
}

/* Resolve this console's display name: the persisted Player Name, or a UID-derived
 * "Console-XXXX" fallback when it has never been set. */
static void resolveSelfName(void)
{
    ConsoleSettings cs;
    consoleSettingsLoad(&cs); /* fills defaults on miss/corrupt */
    if (cs.player_name[0] != '\0')
    {
        copyName(s_self_name, (const uint8_t *)cs.player_name, (uint8_t)strnlen(cs.player_name, MP_NAME_MAX));
    }
    else
    {
        const uint16_t uid = (uint16_t)(*(volatile uint32_t *)STM32_UID_BASE & 0xFFFFU);
        snprintf(s_self_name, sizeof(s_self_name), "Console-%04X", (unsigned)uid);
    }
}

static void setSelfSlot(uint32_t now)
{
    if (s_self_index >= MP_MAX_PLAYERS)
    {
        return;
    }
    MpPeer *p = &s_peers[s_self_index];
    p->used = true;
    p->alive = true;
    p->index = s_self_index;
    macCopy(p->mac, s_self_mac);
    copyName(p->name, (const uint8_t *)s_self_name, (uint8_t)strnlen(s_self_name, MP_NAME_MAX));
    p->last_seen_ms = now;
}

static void clearSessionTables(void)
{
    memset(s_peers, 0, sizeof(s_peers));
    memset(s_hosts, 0, sizeof(s_hosts));
    s_sysout_head = s_sysout_tail = 0U;
    s_inbox_head = s_inbox_tail = 0U;
    s_outbox_head = s_outbox_tail = 0U;
    s_join_pending = false;
    s_last_beacon_ms = 0U;
    s_last_heartbeat_ms = 0U;
    s_request_pending = false;
}

/* ---- ring pushes ---- */

static void sysOutPush(const uint8_t *mac, const uint8_t *data, uint8_t len)
{
    const uint8_t next = (uint8_t)((s_sysout_head + 1U) % MP_SYSOUT_SLOTS);
    if (next == s_sysout_tail || len > MP_SYSOUT_DATA_MAX)
    {
        return; /* full (periodic SYS messages will be re-sent) or too big */
    }
    macCopy(s_sysout[s_sysout_head].mac, mac);
    s_sysout[s_sysout_head].len = len;
    memcpy(s_sysout[s_sysout_head].data, data, len);
    s_sysout_head = next;
}

static void inboxPush(uint8_t peer_index, const uint8_t *data, uint8_t len)
{
    if (len > MP_MSG_MAX)
    {
        len = MP_MSG_MAX;
    }
    const uint8_t next = (uint8_t)((s_inbox_head + 1U) % MP_INBOX_SLOTS);
    if (next == s_inbox_tail)
    {
        return; /* mailbox full — drop (game isn't draining; it will re-sync) */
    }
    s_inbox[s_inbox_head].peer_index = peer_index;
    s_inbox[s_inbox_head].len = len;
    memcpy(s_inbox[s_inbox_head].data, data, len);
    s_inbox_head = next;
}

static bool svcOutAdd(uint8_t *n_out, const uint8_t *mac, const uint8_t *data, uint8_t len)
{
    if (*n_out >= MP_SVC || len > MP_WIRE_DATA_MAX)
    {
        return false;
    }
    macCopy(s_svc[*n_out].mac, mac);
    s_svc[*n_out].len = len;
    memcpy(s_svc[*n_out].data, data, len);
    (*n_out)++;
    return true;
}

/* ---- wire builders ---- *
 * The byte layout lives in mp_wire.c; these thin wrappers just feed it this
 * module's live session state (the peer table, our name / game id), so the
 * handlers and the per-frame pump below stay unchanged. */

/* Snapshot the used peers into the wire's roster view, in ascending index order
 * (so the emitted bytes match the old in-place s_peers walk exactly). */
static uint8_t rosterView(MpWirePeer out[MP_MAX_PLAYERS])
{
    uint8_t n = 0U;
    for (uint8_t i = 0U; i < MP_MAX_PLAYERS; i++)
    {
        if (s_peers[i].used)
        {
            out[n].index = i;
            out[n].mac = s_peers[i].mac;
            out[n].name = s_peers[i].name;
            n++;
        }
    }
    return n;
}

static uint8_t buildBeacon(uint8_t *buf)
{
    return mpWireBuildBeacon(buf, s_game_id, playerCount(), s_self_name);
}

static uint8_t buildRoster(uint8_t *buf)
{
    MpWirePeer view[MP_MAX_PLAYERS];
    const uint8_t n = rosterView(view);
    return mpWireBuildRoster(buf, view, n);
}

static uint8_t buildJoinAccept(uint8_t *buf, uint8_t assigned_index)
{
    MpWirePeer view[MP_MAX_PLAYERS];
    const uint8_t n = rosterView(view);
    return mpWireBuildJoinAccept(buf, assigned_index, view, n);
}

static uint8_t buildJoinReject(uint8_t *buf, uint8_t reason)
{
    return mpWireBuildJoinReject(buf, reason);
}

static uint8_t buildJoinReq(uint8_t *buf)
{
    return mpWireBuildJoinReq(buf, s_self_name);
}

static uint8_t buildSimple(uint8_t *buf, uint8_t sys_type)
{
    return mpWireBuildSimple(buf, sys_type);
}

/* ---- inbound handlers ---- */

static int findHostByMac(const uint8_t *mac)
{
    for (uint8_t i = 0U; i < MP_MAX_HOSTS; i++)
    {
        if (s_hosts[i].used && macEqual(s_hosts[i].mac, mac))
        {
            return (int)i;
        }
    }
    return -1;
}

static void handleBeacon(const uint8_t *mac, const uint8_t *body, uint8_t blen, uint32_t now)
{
    if (s_role != MP_ROLE_NONE)
    {
        return; /* only collect hosts while browsing (pre-join) */
    }
    const MpBeaconFields b = mpWireParseBeacon(body, blen);
    if (!b.ok)
    {
        return;
    }
    /* Only surface hosts of the SAME game (both sides lower-case the .bin name). */
    if (b.game_id_len != strnlen(s_game_id, MP_GAME_ID_MAX) ||
        memcmp(b.game_id, s_game_id, b.game_id_len) != 0)
    {
        return;
    }

    int slot = findHostByMac(mac);
    if (slot < 0)
    {
        for (uint8_t i = 0U; i < MP_MAX_HOSTS; i++)
        {
            if (!s_hosts[i].used)
            {
                slot = (int)i;
                break;
            }
        }
    }
    if (slot < 0)
    {
        return; /* list full */
    }
    s_hosts[slot].used = true;
    macCopy(s_hosts[slot].mac, mac);
    copyName(s_hosts[slot].name, b.name, b.name_len);
    s_hosts[slot].player_count = b.player_count;
    s_hosts[slot].last_seen_ms = now;
}

static void handleJoinReq(const uint8_t *mac, const uint8_t *body, uint8_t blen, uint32_t now)
{
    if (s_role != MP_ROLE_HOST)
    {
        return;
    }
    const uint8_t nl = (blen >= 1U) ? body[0] : 0U;
    const uint8_t *name = &body[1];
    const uint8_t name_len = ((uint16_t)(1U + nl) <= blen) ? nl : 0U;

    uint8_t buf[MP_SYSOUT_DATA_MAX];
    int pi = findPeerByMac(mac);
    bool roster_changed = false;
    if (pi < 0)
    {
        const int slot = freePlayerSlot();
        if (slot < 0)
        {
            const uint8_t l = buildJoinReject(buf, (uint8_t)MP_FULL);
            sysOutPush(mac, buf, l);
            LOGGER_LOG_WARN(LOGGER_MP, "join rejected: roster full");
            return;
        }
        pi = slot;
        MpPeer *p = &s_peers[pi];
        p->used = true;
        p->alive = true;
        p->index = (uint8_t)pi;
        macCopy(p->mac, mac);
        copyName(p->name, name, name_len);
        p->last_seen_ms = now;
        roster_changed = true;
        LOGGER_LOG_INFO(LOGGER_MP, "peer joined: index %d '%s'", pi, p->name);
    }
    /* (Re)send the accept — idempotent, so a lost accept self-heals on the next req. */
    const uint8_t accept_len = buildJoinAccept(buf, (uint8_t)pi);
    sysOutPush(mac, buf, accept_len);
    if (roster_changed)
    {
        const uint8_t rl = buildRoster(buf);
        sysOutPush(MP_BROADCAST_MAC, buf, rl);
    }
}

/* Rebuild the peer table from a roster body {count, count x {idx,mac,namelen,name}},
 * preserving liveness for peers already known (matched by MAC). */
static void adoptRoster(const uint8_t *p, uint8_t len, uint32_t now)
{
    MpPeer old[MP_MAX_PLAYERS];
    memcpy(old, s_peers, sizeof(old));
    memset(s_peers, 0, sizeof(s_peers));

    MpReader r;
    mpReaderInit(&r, p, len);
    uint8_t count;
    if (!mpReadU8(&r, &count))
    {
        return; /* no count byte: leave the table cleared, self slot unset (as before) */
    }
    for (uint8_t e = 0U; e < count; e++)
    {
        uint8_t idx;
        if (!mpReadU8(&r, &idx))
        {
            break;
        }
        uint8_t mac[NP_MP_MAC_LEN];
        /* Need the 6-byte MAC plus the following name-length byte. */
        if (mpReaderRemaining(&r) < NP_MP_MAC_LEN + 1U || !mpReadBytes(&r, mac, NP_MP_MAC_LEN))
        {
            break;
        }
        const uint8_t *name;
        uint8_t nl;
        if (!mpReadNameRef(&r, &name, &nl))
        {
            break;
        }
        if (idx < MP_MAX_PLAYERS)
        {
            MpPeer *peer = &s_peers[idx];
            peer->used = true;
            peer->index = idx;
            macCopy(peer->mac, mac);
            copyName(peer->name, name, nl);
            int oi = -1;
            for (uint8_t k = 0U; k < MP_MAX_PLAYERS; k++)
            {
                if (old[k].used && macEqual(old[k].mac, mac))
                {
                    oi = (int)k;
                    break;
                }
            }
            if (oi >= 0)
            {
                peer->alive = old[oi].alive;
                peer->last_seen_ms = old[oi].last_seen_ms;
            }
            else
            {
                peer->alive = true;
                peer->last_seen_ms = now;
            }
        }
    }
    setSelfSlot(now); /* our own slot is always present and alive */
}

static void handleJoinAccept(const uint8_t *body, uint8_t blen, uint32_t now)
{
    if (s_role == MP_ROLE_HOST || blen < 1U)
    {
        return;
    }
    s_self_index = body[0];
    s_role = MP_ROLE_CLIENT;
    s_join_pending = false;
    s_last_heartbeat_ms = 0U; /* start beating promptly */
    adoptRoster(&body[1], (uint8_t)(blen - 1U), now);
    LOGGER_LOG_INFO(LOGGER_MP, "joined as player %u (%u players)",
                    (unsigned)s_self_index, (unsigned)playerCount());
}

static void handleBye(const uint8_t *mac, uint32_t now)
{
    const int pi = findPeerByMac(mac);
    if (pi < 0)
    {
        return;
    }
    s_peers[pi].alive = false;
    if (s_role == MP_ROLE_HOST && (uint8_t)pi != s_self_index)
    {
        LOGGER_LOG_INFO(LOGGER_MP, "peer %d left (bye)", pi);
        s_peers[pi].used = false;
        uint8_t buf[MP_SYSOUT_DATA_MAX];
        const uint8_t rl = buildRoster(buf);
        sysOutPush(MP_BROADCAST_MAC, buf, rl);
    }
    (void)now;
}

static void processInbound(const uint8_t *mac, const uint8_t *data, uint8_t len, uint32_t now)
{
    if (len < 1U || macEqual(mac, s_self_mac))
    {
        return; /* ignore empty packets and any loopback of our own broadcast */
    }

    /* Any traffic from a known peer refreshes its heartbeat window. */
    const int pi = findPeerByMac(mac);
    if (pi >= 0)
    {
        s_peers[pi].last_seen_ms = now;
        s_peers[pi].alive = true;
    }

    const uint8_t channel = data[0];
    if (channel == MP_CH_APP)
    {
        if (pi >= 0)
        {
            inboxPush((uint8_t)pi, &data[1], (uint8_t)(len - 1U));
        }
        return;
    }

    if (len < 2U)
    {
        return;
    }
    const uint8_t sys = data[1];
    const uint8_t *body = &data[2];
    const uint8_t blen = (uint8_t)(len - 2U);
    switch (sys)
    {
    case MP_SYS_BEACON:
        handleBeacon(mac, body, blen, now);
        break;
    case MP_SYS_JOIN_REQ:
        handleJoinReq(mac, body, blen, now);
        break;
    case MP_SYS_JOIN_ACCEPT:
        handleJoinAccept(body, blen, now);
        break;
    case MP_SYS_JOIN_REJECT:
        s_join_pending = false; /* let the game pick another host / retry */
        LOGGER_LOG_WARN(LOGGER_MP, "join rejected by host");
        break;
    case MP_SYS_ROSTER:
        if (s_role == MP_ROLE_CLIENT)
        {
            adoptRoster(body, blen, now);
        }
        break;
    case MP_SYS_HEARTBEAT:
        break; /* liveness already refreshed above */
    case MP_SYS_BYE:
        handleBye(mac, now);
        break;
    default:
        break;
    }
}

/* ---- liveness sweep ---- */

static void sweep(uint32_t now)
{
    bool roster_changed = false;
    for (uint8_t i = 0U; i < MP_MAX_PLAYERS; i++)
    {
        if (!s_peers[i].used || i == s_self_index)
        {
            if (i == s_self_index && s_peers[i].used)
            {
                s_peers[i].alive = true;
            }
            continue;
        }
        if ((now - s_peers[i].last_seen_ms) > MP_TIMEOUT_MS)
        {
            if (s_peers[i].alive)
            {
                LOGGER_LOG_INFO(LOGGER_MP, "peer %u timed out", (unsigned)i);
            }
            s_peers[i].alive = false;
            if (s_role == MP_ROLE_HOST)
            {
                s_peers[i].used = false;
                roster_changed = true;
            }
        }
    }
    if (roster_changed)
    {
        uint8_t buf[MP_SYSOUT_DATA_MAX];
        const uint8_t rl = buildRoster(buf);
        sysOutPush(MP_BROADCAST_MAC, buf, rl);
    }

    /* Forget stale hosts in the browse list. */
    for (uint8_t i = 0U; i < MP_MAX_HOSTS; i++)
    {
        if (s_hosts[i].used && (now - s_hosts[i].last_seen_ms) > MP_HOST_STALE_MS)
        {
            s_hosts[i].used = false;
        }
    }
}

/* ---- per-frame service (split: collect at frame start, send at frame end) ---- *
 * The exchange is pipelined across one frame so the UART round-trip overlaps the
 * game's render() instead of busy-waiting the CPU. mpSessionCollect() (before
 * update) ingests the reply to the PREVIOUS frame's request — already streamed
 * into the RX ring during the last render(), so it rarely blocks. update() then
 * reads the inbox and queues outbound. mpSessionFlush() (after update) emits this
 * frame's batch over the async TX and returns; render() runs while the reply comes
 * back. Inbound is therefore one frame old (~16 ms @ 60 FPS) — fine for the
 * host-authoritative netcode. */

void mpSessionCollect(uint32_t now_ms)
{
    if (!s_active || !s_request_pending)
    {
        return; /* nothing in flight (first frame, or session just started) */
    }
    s_request_pending = false;

    /* Ingest the reply to last frame's request: peer liveness refresh, SYS protocol
     * (which may queue replies into the outbox, sent in this same frame's send),
     * and app payloads into the inbox the game drains via mpReceive. */
    const uint8_t n_in = espnowLinkCollect(s_svc, MP_SVC);
    for (uint8_t i = 0U; i < n_in; i++)
    {
        processInbound(s_svc[i].mac, s_svc[i].data, s_svc[i].len, now_ms);
    }
}

void mpSessionFlush(uint32_t now_ms)
{
    if (!s_active)
    {
        return;
    }

    uint8_t n_out = 0U;
    uint8_t buf[MP_SYSOUT_DATA_MAX];

    /* 1. flush SYS replies queued while processing the previous frame */
    while (s_sysout_tail != s_sysout_head && n_out < MP_SVC)
    {
        const MpSysOut *m = &s_sysout[s_sysout_tail];
        if (!svcOutAdd(&n_out, m->mac, m->data, m->len))
        {
            break;
        }
        s_sysout_tail = (uint8_t)((s_sysout_tail + 1U) % MP_SYSOUT_SLOTS);
    }

    /* 2. host discovery beacon */
    if (s_role == MP_ROLE_HOST && (now_ms - s_last_beacon_ms) >= MP_BEACON_MS)
    {
        const uint8_t l = buildBeacon(buf);
        if (svcOutAdd(&n_out, MP_BROADCAST_MAC, buf, l))
        {
            s_last_beacon_ms = now_ms;
        }
    }

    /* 3. heartbeat (ping-pong) once we are part of a session */
    if (s_role != MP_ROLE_NONE && (now_ms - s_last_heartbeat_ms) >= MP_HEARTBEAT_MS)
    {
        const uint8_t l = buildSimple(buf, MP_SYS_HEARTBEAT);
        if (svcOutAdd(&n_out, MP_BROADCAST_MAC, buf, l))
        {
            s_last_heartbeat_ms = now_ms;
        }
    }

    /* 4. resend a pending join request (client), or give up on timeout */
    if (s_join_pending)
    {
        if ((now_ms - s_join_started_ms) > MP_JOIN_TIMEOUT_MS)
        {
            s_join_pending = false;
            LOGGER_LOG_WARN(LOGGER_MP, "join timed out");
        }
        else if ((now_ms - s_last_joinreq_ms) >= MP_JOINREQ_RESEND_MS)
        {
            const uint8_t l = buildJoinReq(buf);
            if (svcOutAdd(&n_out, s_pending_host_mac, buf, l))
            {
                s_last_joinreq_ms = now_ms;
            }
        }
    }

    /* 5. drain the game's outbound mailbox */
    uint8_t pkt[1U + MP_MSG_MAX];
    while (s_outbox_tail != s_outbox_head && n_out < MP_SVC)
    {
        const MpAppMsg *m = &s_outbox[s_outbox_tail];
        pkt[0] = MP_CH_APP;
        memcpy(&pkt[1], m->data, m->len);
        const uint8_t plen = (uint8_t)(m->len + 1U);
        bool ok = true;
        if (m->peer_index == MP_BROADCAST_INDEX)
        {
            ok = svcOutAdd(&n_out, MP_BROADCAST_MAC, pkt, plen);
        }
        else if (m->peer_index < MP_MAX_PLAYERS && s_peers[m->peer_index].used)
        {
            ok = svcOutAdd(&n_out, s_peers[m->peer_index].mac, pkt, plen);
        }
        /* a message to a vanished peer is simply dropped */
        if (!ok)
        {
            break; /* batch full — leave the rest for next frame */
        }
        s_outbox_tail = (uint8_t)((s_outbox_tail + 1U) % MP_OUTBOX_SLOTS);
    }

    /* 6. arm the batch over the async TX and return — the reply lands in the RX
     * ring during render() and is picked up by next frame's mpSessionCollect().
     * Always sent (even with n_out == 0) so we keep polling inbound every frame. */
    if (espnowLinkSend(s_svc, n_out))
    {
        s_request_pending = true;
    }

    /* 7. drop silent peers / stale hosts */
    sweep(now_ms);
}

/* ---- public backends (mp* syscalls) ---- */

bool mpSessionActive(void) { return s_active; }
MpRole mpSessionGetRole(void) { return s_role; }

static MpStatus mpBeginCommon(MpRole role)
{
    const FILINFO *fi = loaderGetFileInfo();
    if (fi == NULL)
    {
        LOGGER_LOG_WARN(LOGGER_MP, "no running game to advertise");
        return MP_ERR;
    }

    /* Restart cleanly if a previous session was up. */
    if (s_active)
    {
        espnowLinkEnd();
        s_active = false;
    }

    deriveGameId(fi->fname, s_game_id);
    resolveSelfName();

    if (!espnowLinkBegin(MP_CHANNEL, s_self_mac))
    {
        LOGGER_LOG_ERROR(LOGGER_MP, "esp-now link begin failed");
        return MP_ERR;
    }

    clearSessionTables();
    s_role = role;
    s_active = true;
    return MP_OK;
}

MpStatus mpSessionHostStart(void)
{
    const MpStatus st = mpBeginCommon(MP_ROLE_HOST);
    if (st != MP_OK)
    {
        return st;
    }
    s_self_index = 0U;
    setSelfSlot(getSysTime());
    LOGGER_LOG_INFO(LOGGER_MP, "hosting '%s' as '%s'", s_game_id, s_self_name);
    return MP_OK;
}

MpStatus mpSessionJoinStart(void)
{
    const MpStatus st = mpBeginCommon(MP_ROLE_NONE); /* NONE until a host accepts */
    if (st != MP_OK)
    {
        return st;
    }
    LOGGER_LOG_INFO(LOGGER_MP, "browsing for '%s' hosts", s_game_id);
    return MP_OK;
}

int mpSessionScan(MpHostInfo *out, int max)
{
    if (out == NULL || max <= 0 || !s_active)
    {
        return -1;
    }
    int n = 0;
    for (uint8_t i = 0U; i < MP_MAX_HOSTS && n < max; i++)
    {
        if (!s_hosts[i].used)
        {
            continue;
        }
        out[n].handle = i;
        out[n].player_count = s_hosts[i].player_count;
        copyName(out[n].name, (const uint8_t *)s_hosts[i].name, (uint8_t)strnlen(s_hosts[i].name, MP_NAME_MAX));
        n++;
    }
    return n;
}

MpStatus mpSessionJoin(uint8_t host_handle)
{
    if (!s_active || s_role != MP_ROLE_NONE)
    {
        return MP_ERR;
    }
    if (host_handle >= MP_MAX_HOSTS || !s_hosts[host_handle].used)
    {
        return MP_ERR;
    }
    macCopy(s_pending_host_mac, s_hosts[host_handle].mac);
    s_join_pending = true;
    s_join_started_ms = getSysTime();
    s_last_joinreq_ms = s_join_started_ms - MP_JOINREQ_RESEND_MS; /* send on the next service */
    LOGGER_LOG_INFO(LOGGER_MP, "joining '%s'", s_hosts[host_handle].name);
    return MP_PENDING;
}

void mpSessionStop(void)
{
    if (!s_active)
    {
        return;
    }
    /* Best-effort graceful leave so peers drop us immediately instead of waiting
     * out the heartbeat timeout. */
    if (s_role != MP_ROLE_NONE)
    {
        uint8_t buf[2];
        const uint8_t l = buildSimple(buf, MP_SYS_BYE);
        macCopy(s_svc[0].mac, MP_BROADCAST_MAC);
        s_svc[0].len = l;
        memcpy(s_svc[0].data, buf, l);
        (void)espnowLinkService(s_svc, 1U, s_svc, MP_SVC);
    }
    espnowLinkEnd();
    clearSessionTables();
    s_role = MP_ROLE_NONE;
    s_self_index = 0U;
    s_active = false;
    LOGGER_LOG_INFO(LOGGER_MP, "session stopped");
}

uint8_t mpSessionGetSelfIndex(void) { return s_self_index; }
uint8_t mpSessionGetPlayerCount(void) { return playerCount(); }

bool mpSessionIsConnected(uint8_t index)
{
    return index < MP_MAX_PLAYERS && s_peers[index].used && s_peers[index].alive;
}

int mpSessionGetName(uint8_t index, char *buf, int max)
{
    if (buf == NULL || max <= 0)
    {
        return 0;
    }
    if (index >= MP_MAX_PLAYERS || !s_peers[index].used)
    {
        buf[0] = '\0';
        return 0;
    }
    const int n = (int)strnlen(s_peers[index].name, MP_NAME_MAX);
    const int cp = (n < max - 1) ? n : (max - 1);
    memcpy(buf, s_peers[index].name, (size_t)cp);
    buf[cp] = '\0';
    return cp;
}

int mpSessionGetSelfName(char *buf, int max)
{
    if (buf == NULL || max <= 0)
    {
        return 0;
    }
    if (s_self_name[0] == '\0')
    {
        resolveSelfName(); /* let a game show its name before any session starts */
    }
    const int n = (int)strnlen(s_self_name, MP_NAME_MAX);
    const int cp = (n < max - 1) ? n : (max - 1);
    memcpy(buf, s_self_name, (size_t)cp);
    buf[cp] = '\0';
    return cp;
}

bool mpSessionSend(uint8_t dst_index, const uint8_t *data, uint16_t len)
{
    if (!s_active || s_role == MP_ROLE_NONE || data == NULL || len == 0U || len > MP_MSG_MAX)
    {
        return false;
    }
    const uint8_t next = (uint8_t)((s_outbox_head + 1U) % MP_OUTBOX_SLOTS);
    if (next == s_outbox_tail)
    {
        return false; /* outbox full */
    }
    s_outbox[s_outbox_head].peer_index = dst_index;
    s_outbox[s_outbox_head].len = (uint8_t)len;
    memcpy(s_outbox[s_outbox_head].data, data, len);
    s_outbox_head = next;
    return true;
}

int mpSessionReceive(uint8_t *src_index_out, uint8_t *data, uint16_t max)
{
    if (data == NULL || s_inbox_tail == s_inbox_head)
    {
        return 0;
    }
    const MpAppMsg *m = &s_inbox[s_inbox_tail];
    uint16_t n = m->len;
    if (n > max)
    {
        n = max;
    }
    if (src_index_out != NULL)
    {
        *src_index_out = m->peer_index;
    }
    memcpy(data, m->data, n);
    s_inbox_tail = (uint8_t)((s_inbox_tail + 1U) % MP_INBOX_SLOTS);
    return (int)n;
}
