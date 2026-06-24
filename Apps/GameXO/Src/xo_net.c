#include "xo_net.h"
#include "game_console_api.h" /* mpSend / mpReceive, MP_BROADCAST_INDEX, MP_MSG_MAX */
#include <string.h>

/* The host always holds player index 0. */
#define XO_HOST_INDEX 0U

/* Wire layouts:
 *   MOVE  : [XO_MSG_MOVE, r, c]                              (3 bytes)
 *   STATE : [XO_MSG_STATE, board[9], turn, result]          (12 bytes) */
#define XO_MOVE_LEN 3U
#define XO_STATE_LEN 12U

void xoNetSendMove(uint8_t r, uint8_t c)
{
    const uint8_t buf[XO_MOVE_LEN] = {(uint8_t)XO_MSG_MOVE, r, c};
    mpSend(XO_HOST_INDEX, buf, sizeof(buf));
}

void xoNetBroadcastState(const uint8_t board[9], uint8_t turn, uint8_t result)
{
    uint8_t buf[XO_STATE_LEN];
    buf[0] = (uint8_t)XO_MSG_STATE;
    memcpy(&buf[1], board, 9U);
    buf[10] = turn;
    buf[11] = result;
    mpSend(MP_BROADCAST_INDEX, buf, sizeof(buf));
}

bool xoNetPoll(XoMsg *out, uint8_t *src_index)
{
    out->type = XO_MSG_NONE;

    uint8_t src = 0xFFU;
    uint8_t buf[MP_MSG_MAX];
    const int n = mpReceive(&src, buf, sizeof(buf));
    if (n <= 0)
    {
        return false;
    }
    if (src_index != NULL)
    {
        *src_index = src;
    }

    switch (buf[0])
    {
    case XO_MSG_MOVE:
        if (n >= (int)XO_MOVE_LEN)
        {
            out->type = XO_MSG_MOVE;
            out->r = buf[1];
            out->c = buf[2];
            return true;
        }
        break;
    case XO_MSG_STATE:
        if (n >= (int)XO_STATE_LEN)
        {
            out->type = XO_MSG_STATE;
            memcpy(out->board, &buf[1], 9U);
            out->turn = buf[10];
            out->result = buf[11];
            return true;
        }
        break;
    default:
        break;
    }
    return false;
}
