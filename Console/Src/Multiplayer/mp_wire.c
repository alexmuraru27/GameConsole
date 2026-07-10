#include "Multiplayer/mp_wire.h"
#include <string.h>

/*
 * The ESP-NOW multiplayer wire codec (see mp_wire.h). Pure serialization and
 * bounds-checked parsing over caller buffers — the counterpart to the stateful
 * mp_session.c, kept separate so the byte format can be read and fuzzed alone.
 */

/* ------------------------------------------------------------------ MpReader */

void mpReaderInit(MpReader *const r, const uint8_t *const p, const uint8_t len)
{
    r->p = p;
    r->len = len;
    r->off = 0U;
}

uint8_t mpReaderRemaining(const MpReader *const r)
{
    return (uint8_t)(r->len - r->off);
}

bool mpReadU8(MpReader *const r, uint8_t *const out)
{
    if (r->off >= r->len)
    {
        return false;
    }
    *out = r->p[r->off++];
    return true;
}

bool mpReadBytes(MpReader *const r, uint8_t *const dst, const uint8_t n)
{
    if ((uint16_t)(r->off + n) > r->len)
    {
        return false;
    }
    memcpy(dst, &r->p[r->off], n);
    r->off = (uint8_t)(r->off + n);
    return true;
}

bool mpReadNameRef(MpReader *const r, const uint8_t **const name, uint8_t *const name_len)
{
    uint8_t declared;
    if (!mpReadU8(r, &declared))
    {
        return false;
    }
    const uint8_t avail = mpReaderRemaining(r);
    const uint8_t use = (declared > avail) ? avail : declared;
    *name = &r->p[r->off];
    *name_len = use;
    r->off = (uint8_t)(r->off + use);
    return true;
}

/* ------------------------------------------------------------------ builders */

uint8_t mpWireBuildSimple(uint8_t *const buf, const uint8_t sys_type)
{
    buf[0] = MP_CH_SYS;
    buf[1] = sys_type;
    return 2U;
}

uint8_t mpWireBuildJoinReject(uint8_t *const buf, const uint8_t reason)
{
    buf[0] = MP_CH_SYS;
    buf[1] = MP_SYS_JOIN_REJECT;
    buf[2] = reason;
    return 3U;
}

uint8_t mpWireBuildJoinReq(uint8_t *const buf, const char *const self_name)
{
    uint8_t w = 0U;
    buf[w++] = MP_CH_SYS;
    buf[w++] = MP_SYS_JOIN_REQ;
    const uint8_t nl = (uint8_t)strnlen(self_name, MP_NAME_MAX);
    buf[w++] = nl;
    memcpy(&buf[w], self_name, nl);
    w = (uint8_t)(w + nl);
    return w;
}

uint8_t mpWireBuildBeacon(uint8_t *const buf, const char *const game_id,
                          const uint8_t player_count, const char *const self_name)
{
    uint8_t w = 0U;
    buf[w++] = MP_CH_SYS;
    buf[w++] = MP_SYS_BEACON;
    const uint8_t gl = (uint8_t)strnlen(game_id, MP_GAME_ID_MAX);
    buf[w++] = gl;
    memcpy(&buf[w], game_id, gl);
    w = (uint8_t)(w + gl);
    buf[w++] = player_count;
    const uint8_t nl = (uint8_t)strnlen(self_name, MP_NAME_MAX);
    buf[w++] = nl;
    memcpy(&buf[w], self_name, nl);
    w = (uint8_t)(w + nl);
    return w;
}

uint8_t mpWireBuildRosterBody(uint8_t *const buf, uint8_t w, const MpWirePeer *const peers, const uint8_t count)
{
    buf[w++] = count;
    for (uint8_t i = 0U; i < count; i++)
    {
        buf[w++] = peers[i].index;
        memcpy(&buf[w], peers[i].mac, NP_MP_MAC_LEN);
        w = (uint8_t)(w + NP_MP_MAC_LEN);
        const uint8_t nl = (uint8_t)strnlen(peers[i].name, MP_NAME_MAX);
        buf[w++] = nl;
        memcpy(&buf[w], peers[i].name, nl);
        w = (uint8_t)(w + nl);
    }
    return w;
}

uint8_t mpWireBuildRoster(uint8_t *const buf, const MpWirePeer *const peers, const uint8_t count)
{
    buf[0] = MP_CH_SYS;
    buf[1] = MP_SYS_ROSTER;
    return mpWireBuildRosterBody(buf, 2U, peers, count);
}

uint8_t mpWireBuildJoinAccept(uint8_t *const buf, const uint8_t assigned_index,
                              const MpWirePeer *const peers, const uint8_t count)
{
    buf[0] = MP_CH_SYS;
    buf[1] = MP_SYS_JOIN_ACCEPT;
    buf[2] = assigned_index;
    return mpWireBuildRosterBody(buf, 3U, peers, count);
}

/* ------------------------------------------------------------------ beacon parse */

MpBeaconFields mpWireParseBeacon(const uint8_t *const body, const uint8_t blen)
{
    MpBeaconFields f = {false, NULL, 0U, 1U, NULL, 0U};
    if (blen == 0U)
    {
        return f;
    }
    uint8_t off = 0U;
    const uint8_t gl = body[off++];
    if ((uint16_t)(off + gl) > blen)
    {
        return f; /* game id runs past the packet — malformed */
    }
    f.game_id = &body[off];
    f.game_id_len = gl;
    off = (uint8_t)(off + gl);

    f.player_count = (off < blen) ? body[off++] : 1U;
    uint8_t nl = (off < blen) ? body[off++] : 0U;
    if ((uint16_t)(off + nl) > blen)
    {
        nl = (uint8_t)(blen - off);
    }
    f.name = &body[off];
    f.name_len = nl;
    f.ok = true;
    return f;
}
