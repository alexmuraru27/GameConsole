#ifndef __XO_NET_H
#define __XO_NET_H

#include <stdint.h>
#include <stdbool.h>

/*
 * GameXO multiplayer netcode — a thin codec over the console mp* API.
 *
 * Authority model: HOST-AUTHORITATIVE. The host owns the board and broadcasts a
 * full STATE snapshot on every change (and periodically, so a dropped packet
 * self-heals); the client never mutates the board directly — it sends a MOVE
 * intent and adopts whatever STATE the host sends back. With only tiny, idempotent
 * snapshots this needs no acks or sequence numbers.
 */

typedef enum
{
    XO_MSG_NONE = 0,
    XO_MSG_MOVE = 1,  /* client -> host: "I want to play (r,c)" */
    XO_MSG_STATE = 2, /* host -> all: the authoritative snapshot */
} XoMsgType;

typedef struct
{
    XoMsgType type;
    uint8_t r, c;     /* MOVE: requested cell                          */
    uint8_t board[9]; /* STATE: row-major cells (tic_tac_toe encoding) */
    uint8_t turn;     /* STATE: player index whose turn it is          */
    uint8_t result;   /* STATE: TIC_TAC_TOE_GAME_STATE_*               */
} XoMsg;

/* Client -> host (index 0): request to place at (r,c). */
void xoNetSendMove(uint8_t r, uint8_t c);

/* Host -> everyone: the authoritative board snapshot. */
void xoNetBroadcastState(const uint8_t board[9], uint8_t turn, uint8_t result);

/* Drain one inbound message into *out; returns true if one was decoded. The
 * sender's player index (if any) is stored in *src_index. */
bool xoNetPoll(XoMsg *out, uint8_t *src_index);

#endif /* __XO_NET_H */
