#include "downloader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "network.h"
#include "network_protocol.h"
#include "server_config.h"
#include "crc.h"
#include "sysclock.h"
#include "watchdog.h"
#include "logger.h"
#include "sd_layout.h"
#include "ff.h"

#define MANIFEST_PATH "manifest.csv"                         /* remote URL path (GET) */
#define MANIFEST_SAVE_PATH SD_DIR_MANIFESTS "/manifest.csv"  /* saved copy of the fetched manifest */
#define DOWNLOADED_PATH SD_DIR_MANIFESTS "/downloaded.csv"   /* local record of what we've fetched */
#define DOWNLOADED_MAX 48                                    /* cap for the read-modify-write buffer */

#define MANIFEST_BUF_MAX 4096U      /* whole manifest text (~50 entries)        */
#define CHUNK_MAX NP_MAX_PAYLOAD    /* per HTTP read                            */
#define SPEED_WINDOW_MS 500U        /* speed averaging window                   */

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
    if (!networkHttpOpen(downloaderMakeUrl(MANIFEST_PATH), &length, &status))
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

    /* Keep a local copy of the fetched manifest (before strtok rewrites it). */
    FIL mf;
    if (f_open(&mf, MANIFEST_SAVE_PATH, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK)
    {
        UINT mw = 0U;
        f_write(&mf, s_manifest, (UINT)total, &mw);
        f_close(&mf);
    }

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
    if (!networkHttpOpen(downloaderMakeUrl(entry->path), &length, &status))
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
        watchdogKick(); /* long transfer: feed the watchdog each chunk */
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
        LOGGER_LOG_WARN(LOGGER_NETWORK, "crc mismatch on '%s' (received bytes)", sd_path);
        result = DOWNLOAD_CRC_ERR;
    }
    /* Read the file back off the card and re-CRC it. The streaming CRC above only
     * proves the *received* bytes were correct, not that the SD actually stored
     * them — a flaky card can ACK a write yet hold wrong/incomplete data. This
     * read-back closes that gap so a silently-corrupt file is never kept (and so
     * never flashed to the ESP, whose MD5 only checks flash == SD-file). */
    if (result == DOWNLOAD_OK)
    {
        bool exists = false;
        const uint32_t disk_crc = downloaderLocalCrc(sd_path, &exists);
        if (!exists || disk_crc != entry->crc32)
        {
            LOGGER_LOG_WARN(LOGGER_NETWORK, "sd verify failed on '%s' (card corrupted the write)", sd_path);
            result = DOWNLOAD_CRC_ERR;
        }
    }
    if (result != DOWNLOAD_OK)
    {
        f_unlink(sd_path); /* don't leave a corrupt/partial file behind */
    }
    else
    {
        LOGGER_LOG_INFO(LOGGER_NETWORK, "downloaded '%s' (%lu bytes, crc + sd-readback ok)", sd_path, (unsigned long)done);
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
        watchdogKick(); /* hashing a multi-hundred-KB image: feed the watchdog each chunk */
        crc = crc32_update(crc, s_chunk, (uint32_t)n);
    }
    f_close(&f);
    return crc32_final(crc);
}

/* ---- Local downloaded-manifest (0:/downloaded.csv: "path,crc32hex") ---- */

int downloaderLoadDownloaded(DownloadedEntry *out, int max)
{
    if (out == NULL || max <= 0)
    {
        return -1;
    }

    FIL f;
    if (f_open(&f, DOWNLOADED_PATH, FA_READ) != FR_OK)
    {
        return 0; /* no file yet => nothing downloaded */
    }
    UINT n = 0U;
    const FRESULT res = f_read(&f, s_manifest, MANIFEST_BUF_MAX, &n);
    f_close(&f);
    if (res != FR_OK)
    {
        return -1;
    }
    s_manifest[n] = '\0';

    int count = 0;
    char *save = NULL;
    for (char *line = strtok_r(s_manifest, "\r\n", &save); line != NULL && count < max;
         line = strtok_r(NULL, "\r\n", &save))
    {
        char *comma = strchr(line, ',');
        if (comma == NULL)
        {
            continue;
        }
        *comma = '\0';
        copyField(out[count].path, sizeof(out[count].path), line);
        out[count].crc32 = (uint32_t)strtoul(comma + 1, NULL, 16);
        count++;
    }
    return count;
}

bool downloaderRecordDownload(const char *path, uint32_t crc)
{
    if (path == NULL)
    {
        return false;
    }

    static DownloadedEntry entries[DOWNLOADED_MAX];
    int count = downloaderLoadDownloaded(entries, DOWNLOADED_MAX);
    if (count < 0)
    {
        count = 0;
    }

    /* Update in place if the path is already recorded, else append. */
    int idx = -1;
    for (int i = 0; i < count; i++)
    {
        if (strcmp(entries[i].path, path) == 0)
        {
            idx = i;
            break;
        }
    }
    if (idx < 0)
    {
        if (count >= DOWNLOADED_MAX)
        {
            return false; /* table full; nothing auto-evicted */
        }
        idx = count++;
        copyField(entries[idx].path, sizeof(entries[idx].path), path);
    }
    entries[idx].crc32 = crc;

    FIL f;
    if (f_open(&f, DOWNLOADED_PATH, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
    {
        return false;
    }
    char line[DOWNLOADER_PATH_MAX + 16];
    bool ok = true;
    for (int i = 0; i < count; i++)
    {
        const int len = snprintf(line, sizeof(line), "%s,%08lx\n",
                                 entries[i].path, (unsigned long)entries[i].crc32);
        UINT written = 0U;
        if (len <= 0 || f_write(&f, line, (UINT)len, &written) != FR_OK || written != (UINT)len)
        {
            ok = false;
            break;
        }
    }
    f_close(&f);
    if (ok)
    {
        LOGGER_LOG_INFO(LOGGER_NETWORK, "recorded download '%s' crc %08lx", path, (unsigned long)crc);
    }
    return ok;
}
