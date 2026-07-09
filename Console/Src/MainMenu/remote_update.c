#include "MainMenu/remote_update.h"

#include <stdio.h>
#include <string.h>

#include "MainMenu/menu_common.h"
#include "Renderer/renderer.h"
#include "Devices/buzzer.h"
#include "Peripherals/sysclock.h"
#include "Peripherals/systime.h"
#include "Fonts/fonts.h"
#include "Logger/logger.h"
#include "Network/network.h"
#include "Network/downloader.h"
#include "MainMenu/download_ui.h"
#include "MainMenu/keyboard.h"
#include "SettingsStorage/console_settings_storage.h"
#include "sd_layout.h"

#define RU_MAX_ENTRIES 32
#define RU_VISIBLE_ROWS 8
#define RU_DOWNLOAD_ATTEMPTS 3 /* re-download on ESP-link crash or corrupt write */

static RemoteEntry s_entries[RU_MAX_ENTRIES];
static char s_status[RU_MAX_ENTRIES][12];              /* "NEW" / "UPD" / "UpToDate" per display row */
static DownloadedEntry s_downloaded[RU_MAX_ENTRIES];   /* last-downloaded CRCs (0:/downloaded.csv) */
static int s_downloaded_count;

/* A display row is one logical item: a standalone file, or a game (its .bin plus
 * the paired .pak, shown and downloaded as a single unit). Indices into s_entries. */
typedef struct
{
    int bin; /* primary entry: a game's .bin, or any standalone file */
    int pak; /* the game's paired .pak, or -1                        */
} DisplayRow;
static DisplayRow s_rows[RU_MAX_ENTRIES];
static int s_row_count;


/* ---- helpers ---- */

/* Throttle the progress render to ~10 Hz (downloads call back every ~1 KB). */
typedef struct
{
    const char *title;
    const char *file;
    uint32_t last_render;
} ProgressCtx;

static void progressCb(uint32_t done, uint32_t total, uint32_t bps, void *vctx)
{
    ProgressCtx *ctx = (ProgressCtx *)vctx;
    const uint32_t now = getSysTime();
    if (done >= total || now - ctx->last_render >= 100U)
    {
        downloadUiProgress(ctx->title, ctx->file, done, total, bps);
        ctx->last_render = now;
    }
}

/* Ensure a WiFi link, connecting with saved credentials if needed. */
static bool ensureConnected(const char *title)
{
    if (networkIsConnected())
    {
        return true;
    }

    ConsoleSettings s;
    consoleSettingsLoad(&s);
    if (s.wifi_valid == 0U)
    {
        downloadUiWait(title, "Set up WiFi in Settings first", g_menu_pal_alert);
        return false;
    }

    char msg[64];
    snprintf(msg, sizeof(msg), "Connecting to %s...", s.wifi_ssid);
    downloadUiInfo(title, msg, g_menu_pal_item_sel);
    if (!networkConnect(s.wifi_ssid, s.wifi_pass))
    {
        downloadUiWait(title, "WiFi connect failed", g_menu_pal_alert);
        return false;
    }
    return true;
}

static bool nameEndsWith(const char *name, const char *suffix)
{
    const size_t nl = strlen(name);
    const size_t sl = strlen(suffix);
    return nl >= sl && strcasecmp(name + nl - sl, suffix) == 0;
}

/* Local SD directory for a manifest category: Games/ for games, Firmware/ for
 * firmware images (the console OS and the ESP-01, both served under the single
 * "firmware" category); unknown categories fall back to the card root. */
static const char *destDir(const char *category)
{
    if (strcasecmp(category, "games") == 0)
    {
        return SD_DIR_GAMES;
    }
    if (strcasecmp(category, "Firmware") == 0)
    {
        return SD_DIR_FIRMWARE;
    }
    return "";
}

/* On-card path a remote entry downloads to: "<dir>/<name>" (where the game
 * loader / flasher look for it). */
static void destPath(const RemoteEntry *e, char *out, size_t out_size)
{
    const char *dir = destDir(e->category);
    if (dir[0] == '\0')
    {
        snprintf(out, out_size, "%s", e->name);
    }
    else
    {
        snprintf(out, out_size, "%s/%s", dir, e->name);
    }
}

/* CRC recorded for this remote path in the local downloaded-manifest, if any. */
static uint32_t savedCrc(const char *path, bool *found)
{
    for (int i = 0; i < s_downloaded_count; i++)
    {
        if (strcmp(s_downloaded[i].path, path) == 0)
        {
            *found = true;
            return s_downloaded[i].crc32;
        }
    }
    *found = false;
    return 0U;
}

/* Index of the manifest entry named `name` (case-insensitive), or -1. */
static int findByName(int count, const char *name)
{
    for (int i = 0; i < count; i++)
    {
        if (strcasecmp(s_entries[i].name, name) == 0)
        {
            return i;
        }
    }
    return -1;
}

/* Index of the sibling entry that is `name` with its extension swapped to `ext`
 * (e.g. the .pak that pairs with a .bin), or -1. */
static int findSibling(int count, const char *name, const char *ext)
{
    char sib[DOWNLOADER_NAME_MAX];
    strncpy(sib, name, sizeof(sib) - 1U);
    sib[sizeof(sib) - 1U] = '\0';
    char *dot = strrchr(sib, '.');
    if (dot == NULL)
    {
        return -1;
    }
    strncpy(dot, ext, (size_t)(&sib[sizeof(sib) - 1U] - dot));
    return findByName(count, sib);
}

/* True if `e` is missing from the saved manifest or its CRC differs from the
 * server's; *exists reports whether it was recorded at all. */
static bool entryMismatch(const RemoteEntry *e, bool *exists)
{
    bool found = false;
    const uint32_t saved = savedCrc(e->path, &found);
    *exists = found;
    return !found || saved != e->crc32;
}

/* Diff a display row (a game's .bin + .pak, or a standalone file) against the
 * last-downloaded manifest: NEW (never fetched) / UPD (changed) / OK. A game is
 * "OK" only when both its .bin and .pak match. */
static void rowStatus(const DisplayRow *r, char out[4])
{
    bool bin_exists = false;
    bool mismatch = entryMismatch(&s_entries[r->bin], &bin_exists);
    if (!bin_exists)
    {
        strcpy(out, "NEW");
        return;
    }
    if (r->pak >= 0)
    {
        bool pak_exists = false;
        if (entryMismatch(&s_entries[r->pak], &pak_exists))
        {
            mismatch = true;
        }
    }
    strcpy(out, mismatch ? "UPD" : "UpToDate");
}

/* ---- Poll Updates ---- */

static void renderList(int count, int selected, int top)
{
    const bool cursor_on = menuCursorVisible();
    uint16_t n = 0U;

    rendererClear();
    n = menuDrawTitle(n, "UPDATES");

    if (count == 0)
    {
        menuDrawText(&font8x8, 60, 110, g_menu_pal_item, "Nothing on server");
    }
    for (int i = 0; i < RU_VISIBLE_ROWS && (top + i) < count; i++)
    {
        const int row = top + i;
        const RemoteEntry *e = &s_entries[s_rows[row].bin];
        const bool sel = (row == selected);
        const bool mismatch = (strcmp(s_status[row], "UpToDate") != 0); /* NEW or UPD */
        const int16_t y = (int16_t)(MENU_LIST_TOP + i * MENU_ROW_H);

        if (sel && cursor_on)
        {
            menuDrawText(&font8x8, 42, y, g_menu_pal_accent, ">");
        }
        /* Highlight a CRC mismatch (vs the last download) in the accent colour. */
        const uint16_t *label_pal = sel ? g_menu_pal_item_sel
                                        : (mismatch ? g_menu_pal_accent : g_menu_pal_item);
        menuDrawText(&font8x8, 60, y, label_pal, e->name);

        const char *tag = s_status[row];
        const uint16_t *tag_pal = mismatch ? g_menu_pal_accent : g_menu_pal_footer;
        const int16_t tx = (int16_t)(rendererGetWidthPixels() - 60 - (int16_t)menuTextWidth(font8x8.size, tag));
        menuDrawText(&font8x8, tx, y, tag_pal, tag);
    }

    menuDrawFooter("UP/DOWN   A download   B back");
    rendererSubmitLayer(LAYER_UI, g_menu_ui, n);
    rendererRender();
}

/* Download one file, retrying once after recovering the link if the ESP's HTTP
 * stack crashed (an intermittent lwIP fault over a flaky link reboots the ESP,
 * dropping the connection). Only transport failures retry — a CRC/SD error is
 * not a link crash. */
static DownloadStatus fetchWithRetry(const RemoteEntry *e, const char *path, ProgressCtx *ctx)
{
    DownloadStatus st = DOWNLOAD_HTTP_ERR;
    for (int attempt = 1; attempt <= RU_DOWNLOAD_ATTEMPTS; attempt++)
    {
        st = downloaderFetchFile(e, path, progressCb, ctx);
        if (st == DOWNLOAD_OK || attempt >= RU_DOWNLOAD_ATTEMPTS)
        {
            break;
        }
        ctx->last_render = 0U;
        if (st == DOWNLOAD_NO_SERVER || st == DOWNLOAD_HTTP_ERR)
        {
            /* The ESP HTTP stack likely crashed + rebooted (dropping WiFi); bring
             * the link back before retrying. */
            downloadUiInfo("UPDATES", "Link reset, retrying...", g_menu_pal_item_sel);
            networkRebootEsp();
            if (!ensureConnected("UPDATES"))
            {
                break;
            }
        }
        else if (st == DOWNLOAD_CRC_ERR)
        {
            /* Corrupt download — the read-back caught a flaky SD write (or rarely a
             * bad transfer). The link is fine, so just re-fetch; a fresh write may
             * land clean. (If the card keeps corrupting, this fails out and the
             * card needs replacing.) */
            downloadUiInfo("UPDATES", "Bad data, retrying...", g_menu_pal_item_sel);
        }
        else
        {
            break; /* DOWNLOAD_SD_ERR (hard write failure): don't spin on it */
        }
    }
    return st;
}

static void downloadRow(int row)
{
    const DisplayRow *r = &s_rows[row];
    const RemoteEntry *bin = &s_entries[r->bin];
    ProgressCtx ctx = {"UPDATES", bin->name, 0U};
    char path[DOWNLOADER_PATH_MAX];

    /* Download the primary file into its category dir, then its paired .pak (a
     * game is one unit). Each is recorded in the local manifest (keyed by remote
     * path) so the diff shows "UpToDate" afterwards. */
    destPath(bin, path, sizeof(path));
    DownloadStatus st = fetchWithRetry(bin, path, &ctx);
    if (st == DOWNLOAD_OK)
    {
        downloaderRecordDownload(bin->path, bin->crc32);
        if (r->pak >= 0)
        {
            const RemoteEntry *pak = &s_entries[r->pak];
            ctx.file = pak->name;
            ctx.last_render = 0U;
            destPath(pak, path, sizeof(path));
            st = fetchWithRetry(pak, path, &ctx);
            if (st == DOWNLOAD_OK)
            {
                downloaderRecordDownload(pak->path, pak->crc32);
            }
        }
    }

    if (st == DOWNLOAD_OK)
    {
        strcpy(s_status[row], "UpToDate");
        menuBeepMove();
        downloadUiWait("UPDATES", "Download complete!", g_menu_pal_accent);
    }
    else
    {
        char m[48];
        snprintf(m, sizeof(m), "Failed: %s", downloaderStatusString(st));
        downloadUiWait("UPDATES", m, g_menu_pal_alert);
    }
}

void remoteGamesRun(void)
{
    menuResetSurface();
    if (!ensureConnected("UPDATES"))
    {
        return;
    }

    downloadUiInfo("UPDATES", "Fetching list...", g_menu_pal_item_sel);
    int count = downloaderFetchManifest(s_entries, RU_MAX_ENTRIES);
    if (count <= 0)
    {
        /* The ESP can crash + reboot inside its HTTP/DNS stack on a flaky link
         * (the GET fails silently and the module drops WiFi). Power-cycle it back
         * to a clean state, reconnect, and try the fetch once more. */
        downloadUiInfo("UPDATES", "Link reset, retrying...", g_menu_pal_item_sel);
        networkRebootEsp();
        if (ensureConnected("UPDATES"))
        {
            count = downloaderFetchManifest(s_entries, RU_MAX_ENTRIES);
        }
    }
    if (count <= 0)
    {
        downloadUiWait("UPDATES", "No manifest / server error", g_menu_pal_alert);
        return;
    }

    /* Load the last-downloaded record, then collapse the manifest into display
     * rows: a game's .bin absorbs its paired .pak (one row, downloaded together);
     * every other file is its own row. Diff each row: NEW / UPD / OK. */
    s_downloaded_count = downloaderLoadDownloaded(s_downloaded, RU_MAX_ENTRIES);
    if (s_downloaded_count < 0)
    {
        s_downloaded_count = 0;
    }
    s_row_count = 0;
    for (int i = 0; i < count && s_row_count < RU_MAX_ENTRIES; i++)
    {
        /* A .pak that belongs to a .bin in the manifest rides with that game. */
        if (nameEndsWith(s_entries[i].name, ".pak") && findSibling(count, s_entries[i].name, ".bin") >= 0)
        {
            continue;
        }
        s_rows[s_row_count].bin = i;
        s_rows[s_row_count].pak = nameEndsWith(s_entries[i].name, ".bin")
                                      ? findSibling(count, s_entries[i].name, ".pak")
                                      : -1;
        rowStatus(&s_rows[s_row_count], s_status[s_row_count]);
        s_row_count++;
    }

    MenuListState list = {0, 0};
    for (;;)
    {
        const MenuNav nav = menuPollNav();
        if (nav.back)
        {
            return;
        }
        if (menuListStep(&list, nav, s_row_count, RU_VISIBLE_ROWS))
        {
            menuBeepMove();
        }
        const int selected = list.selected;
        const int top = list.top;

        if (nav.enter && s_row_count > 0)
        {
            downloadRow(selected);
        }

        renderList(s_row_count, selected, top);
    }
}

/* ---- Edit server address (Settings) ---- */

void remoteServerAddrRun(void)
{
    menuResetSurface();

    char addr[80];
    downloaderGetServerAddr(addr, sizeof(addr)); /* current value, pre-fills the keyboard */

    if (keyboardModal("SERVER ADDRESS", addr, sizeof(addr)))
    {
        menuResetSurface();
        if (downloaderSetServerAddr(addr))
        {
            downloadUiWait("SERVER ADDRESS", "Saved", g_menu_pal_accent);
        }
        else
        {
            downloadUiWait("SERVER ADDRESS", "Save failed (no SD?)", g_menu_pal_alert);
        }
    }
}
