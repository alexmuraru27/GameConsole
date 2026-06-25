/*
 * HTTP GET subsystem — see http.h. Holds the single open-GET session (client,
 * stream, bytes remaining) and relays the body to the console in chunks.
 */
#include "http.h"

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <string.h>

#include "network_protocol.h"
#include "protocol.h"

/* Scratch for building an HTTP_DATA chunk response. */
static uint8_t s_buf[NP_MAX_PAYLOAD];

/* Open HTTP GET session state. */
static WiFiClient s_client;
static HTTPClient s_http;
static WiFiClient *s_stream = nullptr;
static long s_remaining = 0; /* bytes left when Content-Length is known, else -1 */
static bool s_http_active = false;

void httpHandleOpen(uint16_t len)
{
    /* static, not stack: the URL must stay alive across the whole GET() call
     * chain (DNS + connect + header parse), and keeping 257 bytes off the 4 KB
     * cont-stack avoids overflowing it deep inside HTTPClient. */
    static char url[NP_URL_MAX + 1];
    uint16_t n = (len > NP_URL_MAX) ? NP_URL_MAX : len;
    memcpy(url, np::payload(), n);
    url[n] = '\0';

    if (s_http_active)
    {
        s_http.end();
        s_http_active = false;
    }

    uint16_t http_status = 0u;
    uint32_t content_length = NP_LENGTH_UNKNOWN;
    s_remaining = -1;
    s_stream = nullptr;

    np::logf(NP_LOG_INFO, "GET %s (heap %u)", url, (unsigned)ESP.getFreeHeap());
    if (WiFi.status() != WL_CONNECTED)
    {
        np::logf(NP_LOG_WARN, "GET aborted: WiFi not connected");
    }
    else if (!s_http.begin(s_client, url))
    {
        np::logf(NP_LOG_WARN, "GET begin() failed (bad URL?)");
    }
    else
    {
        /* Match the simple update server: HTTP/1.0, no keep-alive, no redirects.
         * This keeps HTTPClient off its chunked/reuse code paths and frees the
         * socket promptly between files. */
        s_http.useHTTP10(true);
        s_http.setReuse(false);
        s_http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
        s_http.setTimeout(8000);
        /* Boundary marker: if this prints but "http N" / "GET error" never does,
         * the crash is inside GET() (the TCP receive path), not begin()/connect.
         * The heap here flags any allocation pressure right before the transfer. */
        np::logf(NP_LOG_DEBUG, "begin ok, GET (heap %u)", (unsigned)ESP.getFreeHeap());
        const int code = s_http.GET();
        s_http_active = true;
        if (code > 0)
        {
            http_status = (uint16_t)code;
            const int sz = s_http.getSize();
            if (sz >= 0)
            {
                content_length = (uint32_t)sz;
                s_remaining = sz;
            }
            s_stream = s_http.getStreamPtr();
            np::logf(NP_LOG_INFO, "http %d, len %d", code, sz);
        }
        else
        {
            np::logf(NP_LOG_WARN, "http GET error %d", code);
        }
    }

    uint8_t out[6];
    out[0] = (uint8_t)(http_status & 0xFFu);
    out[1] = (uint8_t)(http_status >> 8u);
    out[2] = (uint8_t)(content_length & 0xFFu);
    out[3] = (uint8_t)((content_length >> 8u) & 0xFFu);
    out[4] = (uint8_t)((content_length >> 16u) & 0xFFu);
    out[5] = (uint8_t)((content_length >> 24u) & 0xFFu);
    np::sendFrame(NP_RSP_HTTP_OPEN, out, sizeof(out));
}

void httpHandleRead(uint16_t len)
{
    uint16_t want = NP_MAX_PAYLOAD;
    if (len >= 2u)
    {
        const uint8_t *p = np::payload();
        want = (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8u));
    }
    if (want > NP_MAX_PAYLOAD)
    {
        want = NP_MAX_PAYLOAD;
    }
    if (s_remaining == 0 || s_stream == nullptr)
    {
        np::sendFrame(NP_RSP_HTTP_DATA, nullptr, 0u); /* EOF */
        return;
    }
    if (s_remaining > 0 && (long)want > s_remaining)
    {
        want = (uint16_t)s_remaining;
    }

    /* Gather up to `want` bytes; stop on idle timeout or a closed connection. */
    uint16_t got = 0u;
    uint32_t idle_deadline = millis() + 4000u;
    while (got < want)
    {
        const int avail = s_stream->available();
        if (avail > 0)
        {
            int chunk = s_stream->read(&s_buf[got], want - got);
            if (chunk > 0)
            {
                got += (uint16_t)chunk;
                idle_deadline = millis() + 4000u;
            }
        }
        else if (!s_http.connected() && s_stream->available() == 0)
        {
            break; /* server closed and buffer drained */
        }
        else if ((int32_t)(idle_deadline - millis()) <= 0)
        {
            break; /* stalled */
        }
        else
        {
            delay(1);
        }
    }

    if (s_remaining > 0)
    {
        s_remaining -= got;
    }
    np::sendFrame(NP_RSP_HTTP_DATA, s_buf, got);
}

void httpHandleClose(void)
{
    if (s_http_active)
    {
        s_http.end();
        s_http_active = false;
    }
    s_stream = nullptr;
    s_remaining = 0;
    np::logf(NP_LOG_DEBUG, "http closed");
    np::sendFrame(NP_RSP_OK, nullptr, 0u);
}
