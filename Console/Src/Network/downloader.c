#include "downloader.h"

#include <stdlib.h>
#include <string.h>

#include "network.h"
#include "network_protocol.h"
#include "crc.h"
#include "sysclock.h"
#include "logger.h"
#include "ff.h"

/* Example address used for the auto-created server.txt and as the fallback.
 * Either "host:port" or a full "http://host:port" works (see makeUrl). */
#define DEFAULT_SERVER_ADDR "192.168.100.29:25568"

/* Written verbatim when server.txt is missing, so the format is self-evident.
 * Only the first line is parsed; the rest is a guiding comment. */
#define SERVER_CFG_TEMPLATE DEFAULT_SERVER_ADDR \
    "\n# Update server address. First line only; IP or hostname, optional http:// scheme:\n" \
    "#   192.168.1.50:25568   |   myserver:25568   |   http://myserver.lan:25568\n" \
    "# A hostname is resolved by the ESP via DNS (must be resolvable on your network).\n"

#define SERVER_CFG_PATH "server.txt"
#define MANIFEST_PATH "manifest.csv"

#define URL_MAX NP_URL_MAX
#define MANIFEST_BUF_MAX 4096U      /* whole manifest text (~50 entries)        */
#define CHUNK_MAX NP_MAX_PAYLOAD    /* per HTTP read                            */
#define SPEED_WINDOW_MS 500U        /* speed averaging window                   */

static char s_base_url[URL_MAX];
static char s_url[URL_MAX];
static uint8_t s_chunk[CHUNK_MAX];
static char s_manifest[MANIFEST_BUF_MAX + 1U];

const char *downloaderStatusString(DownloadStatus status)
{
    switch (status)
    {
    case DOWNLOAD_OK:
        return "ok";
    case DOWNLOAD_NO_SERVER:
        return "server unreachable";
    case DOWNLOAD_HTTP_ERR:
        return "transfer error";
    case DOWNLOAD_SD_ERR:
        return "SD write error";
    case DOWNLOAD_CRC_ERR:
        return "CRC mismatch";
    default:
        return "unknown";
    }
}

/* ---- URL helpers --------------------------------------------------- */

/* Create server.txt with the example template if it isn't on the card yet, so
 * the user sees the expected format and can edit it (also via Settings). */
static void ensureServerCfg(void)
{
    FILINFO info;
    if (f_stat(SERVER_CFG_PATH, &info) == FR_OK)
    {
        return; /* already present */
    }
    FIL f;
    if (f_open(&f, SERVER_CFG_PATH, FA_WRITE | FA_CREATE_NEW) == FR_OK)
    {
        UINT written = 0U;
        const char *t = SERVER_CFG_TEMPLATE;
        f_write(&f, t, (UINT)strlen(t), &written);
        f_close(&f);
        LOGGER_LOG_INFO(LOGGER_NETWORK, "created template %s", SERVER_CFG_PATH);
    }
}

/* Base address from 0:/server.txt (first line, trimmed) else the default. */
static const char *baseUrl(void)
{
    ensureServerCfg();

    FIL f;
    if (f_open(&f, SERVER_CFG_PATH, FA_READ) == FR_OK)
    {
        UINT n = 0U;
        const FRESULT res = f_read(&f, s_base_url, sizeof(s_base_url) - 1U, &n);
        f_close(&f);
        if (res == FR_OK && n > 0U)
        {
            s_base_url[n] = '\0';
            /* keep only the first line, trimmed at whitespace */
            for (char *p = s_base_url; *p != '\0'; p++)
            {
                if (*p == '\r' || *p == '\n' || *p == ' ' || *p == '\t')
                {
                    *p = '\0';
                    break;
                }
            }
            if (s_base_url[0] != '\0')
            {
                return s_base_url;
            }
        }
    }
    return DEFAULT_SERVER_ADDR;
}

/* Compose "http://" (if absent) + base + "/" + path into s_url. */
static const char *makeUrl(const char *path)
{
    const char *base = baseUrl();
    s_url[0] = '\0';
    if (strncmp(base, "http", 4) != 0)
    {
        strncat(s_url, "http://", sizeof(s_url) - 1U);
    }
    strncat(s_url, base, sizeof(s_url) - strlen(s_url) - 1U);
    if (s_url[strlen(s_url) - 1U] != '/')
    {
        strncat(s_url, "/", sizeof(s_url) - strlen(s_url) - 1U);
    }
    strncat(s_url, path, sizeof(s_url) - strlen(s_url) - 1U);
    return s_url;
}

void downloaderGetServerAddr(char *out, uint16_t out_size)
{
    if (out == NULL || out_size == 0U)
    {
        return;
    }
    strncpy(out, baseUrl(), out_size - 1U);
    out[out_size - 1U] = '\0';
}

bool downloaderSetServerAddr(const char *addr)
{
    if (addr == NULL)
    {
        return false;
    }
    FIL f;
    if (f_open(&f, SERVER_CFG_PATH, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
    {
        return false;
    }
    UINT written = 0U;
    const FRESULT r1 = f_write(&f, addr, (UINT)strlen(addr), &written);
    const FRESULT r2 = f_write(&f, "\n", 1U, &written);
    f_close(&f);
    LOGGER_LOG_INFO(LOGGER_NETWORK, "server address set to '%s'", addr);
    return r1 == FR_OK && r2 == FR_OK;
}

/* ---- Manifest ------------------------------------------------------ */

static void copyField(char *dst, size_t dst_size, const char *src)
{
    strncpy(dst, src, dst_size - 1U);
    dst[dst_size - 1U] = '\0';
}

/* Parse one CSV line (mutated in place) into an entry. */
static bool parseLine(char *line, RemoteEntry *e)
{
    char *fields[6];
    int nf = 0;
    fields[nf++] = line;
    for (char *p = line; *p != '\0' && nf < 6; p++)
    {
        if (*p == ',')
        {
            *p = '\0';
            fields[nf++] = p + 1;
        }
    }
    if (nf < 6)
    {
        return false;
    }
    copyField(e->category, sizeof(e->category), fields[0]);
    copyField(e->name, sizeof(e->name), fields[1]);
    copyField(e->path, sizeof(e->path), fields[2]);
    e->size = (uint32_t)strtoul(fields[3], NULL, 10);
    e->crc32 = (uint32_t)strtoul(fields[4], NULL, 16);
    e->version = (uint32_t)strtoul(fields[5], NULL, 10);
    return true;
}

int downloaderFetchManifest(RemoteEntry *out, int max)
{
    if (out == NULL || max <= 0)
    {
        return -1;
    }

    uint32_t length = 0U;
    uint16_t status = 0U;
    if (!networkHttpOpen(makeUrl(MANIFEST_PATH), &length, &status))
    {
        LOGGER_LOG_WARN(LOGGER_NETWORK, "manifest fetch failed (http %u)", (unsigned)status);
        networkHttpClose();
        return -1;
    }

    uint32_t total = 0U;
    int n;
    while (total < MANIFEST_BUF_MAX &&
           (n = networkHttpRead((uint8_t *)&s_manifest[total], (uint16_t)(MANIFEST_BUF_MAX - total))) > 0)
    {
        total += (uint32_t)n;
    }
    networkHttpClose();
    s_manifest[total] = '\0';

    /* Split into lines; skip the header row; parse the rest. */
    int count = 0;
    bool header = true;
    char *save = NULL;
    for (char *line = strtok_r(s_manifest, "\r\n", &save); line != NULL && count < max;
         line = strtok_r(NULL, "\r\n", &save))
    {
        if (header)
        {
            header = false;
            continue;
        }
        if (parseLine(line, &out[count]))
        {
            count++;
        }
    }
    LOGGER_LOG_INFO(LOGGER_NETWORK, "manifest: %d entr(ies)", count);
    return count;
}

/* ---- File download ------------------------------------------------- */

DownloadStatus downloaderFetchFile(const RemoteEntry *entry, const char *sd_path,
                                   DownloadProgressCb cb, void *ctx)
{
    if (entry == NULL || sd_path == NULL)
    {
        return DOWNLOAD_SD_ERR;
    }

    uint32_t length = 0U;
    uint16_t status = 0U;
    if (!networkHttpOpen(makeUrl(entry->path), &length, &status))
    {
        networkHttpClose();
        return DOWNLOAD_NO_SERVER;
    }

    FIL f;
    if (f_open(&f, sd_path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
    {
        networkHttpClose();
        return DOWNLOAD_SD_ERR;
    }

    const uint32_t total = entry->size;
    uint32_t done = 0U;
    uint32_t crc = CRC32_INIT;
    uint32_t window_start = getSysTime();
    uint32_t window_bytes = 0U;
    uint32_t bps = 0U;
    DownloadStatus result = DOWNLOAD_OK;

    for (;;)
    {
        const int n = networkHttpRead(s_chunk, CHUNK_MAX);
        if (n < 0)
        {
            result = DOWNLOAD_HTTP_ERR;
            break;
        }
        if (n == 0)
        {
            break; /* EOF */
        }

        UINT written = 0U;
        if (f_write(&f, s_chunk, (UINT)n, &written) != FR_OK || written != (UINT)n)
        {
            result = DOWNLOAD_SD_ERR;
            break;
        }
        crc = crc32_update(crc, s_chunk, (uint32_t)n);
        done += (uint32_t)n;

        window_bytes += (uint32_t)n;
        const uint32_t elapsed = getSysTime() - window_start;
        if (elapsed >= SPEED_WINDOW_MS)
        {
            bps = (window_bytes * 1000U) / elapsed;
            window_start = getSysTime();
            window_bytes = 0U;
        }
        if (cb != NULL)
        {
            cb(done, total, bps, ctx);
        }
    }

    networkHttpClose();
    f_close(&f);

    if (result == DOWNLOAD_OK && crc32_final(crc) != entry->crc32)
    {
        LOGGER_LOG_WARN(LOGGER_NETWORK, "crc mismatch on '%s'", sd_path);
        result = DOWNLOAD_CRC_ERR;
    }
    if (result != DOWNLOAD_OK)
    {
        f_unlink(sd_path); /* don't leave a corrupt/partial file behind */
    }
    else
    {
        LOGGER_LOG_INFO(LOGGER_NETWORK, "downloaded '%s' (%lu bytes, crc ok)", sd_path, (unsigned long)done);
    }
    return result;
}

uint32_t downloaderLocalCrc(const char *sd_path, bool *exists)
{
    FIL f;
    if (f_open(&f, sd_path, FA_READ) != FR_OK)
    {
        if (exists != NULL)
        {
            *exists = false;
        }
        return 0U;
    }
    if (exists != NULL)
    {
        *exists = true;
    }

    uint32_t crc = CRC32_INIT;
    UINT n = 0U;
    while (f_read(&f, s_chunk, sizeof(s_chunk), &n) == FR_OK && n > 0U)
    {
        crc = crc32_update(crc, s_chunk, (uint32_t)n);
    }
    f_close(&f);
    return crc32_final(crc);
}
