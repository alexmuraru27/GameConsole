#ifndef __DOWNLOADER_H
#define __DOWNLOADER_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Download engine: pulls the update server's manifest and files through the ESP
 * WiFi link (network.c) onto the SD card, verifying each file's CRC-32 against
 * the manifest. The server base URL comes from 0:/server.txt (one line) or a
 * compile-time default. Used by Poll Remote Games and Download WiFi firmware.
 */

#define DOWNLOADER_CAT_MAX 8U
#define DOWNLOADER_NAME_MAX 48U
#define DOWNLOADER_PATH_MAX 64U

/* One manifest row: "category,name,path,size,crc32,version". */
typedef struct
{
    char category[DOWNLOADER_CAT_MAX];
    char name[DOWNLOADER_NAME_MAX];
    char path[DOWNLOADER_PATH_MAX];
    uint32_t size;
    uint32_t crc32;
    uint32_t version;
} RemoteEntry;

typedef enum
{
    DOWNLOAD_OK = 0,
    DOWNLOAD_NO_SERVER, /* HTTP open / connection failed */
    DOWNLOAD_HTTP_ERR,  /* transport error mid-transfer  */
    DOWNLOAD_SD_ERR,    /* SD open/write failed          */
    DOWNLOAD_CRC_ERR,   /* CRC mismatch after download   */
} DownloadStatus;

/* Per-block progress: bytes done / total, and current speed (bytes/sec). */
typedef void (*DownloadProgressCb)(uint32_t done, uint32_t total, uint32_t bytes_per_sec, void *ctx);

/* GET /manifest.csv and parse it into `out` (up to `max`). Returns count or -1. */
int downloaderFetchManifest(RemoteEntry *out, int max);

/* Download `entry` to `sd_path` (e.g. "GameXO.bin"), CRC-verifying against the
 * manifest. `cb` may be NULL. On CRC/SD failure the partial file is removed. */
DownloadStatus downloaderFetchFile(const RemoteEntry *entry, const char *sd_path,
                                   DownloadProgressCb cb, void *ctx);

/* CRC-32 of a local SD file (chunked). Sets *exists=false if it isn't there. */
uint32_t downloaderLocalCrc(const char *sd_path, bool *exists);

const char *downloaderStatusString(DownloadStatus status);

/* Current server address (from 0:/server.txt, auto-creating a template if it's
 * missing) and a setter that rewrites server.txt. Used by the Settings editor. */
void downloaderGetServerAddr(char *out, uint16_t out_size);
bool downloaderSetServerAddr(const char *addr);

#endif /* __DOWNLOADER_H */
