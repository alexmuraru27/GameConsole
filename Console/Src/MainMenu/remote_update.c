#include "remote_update.h"

#include <stdio.h>
#include <string.h>

#include "menu_common.h"
#include "renderer.h"
#include "buzzer.h"
#include "sysclock.h"
#include "fonts.h"
#include "logger.h"
#include "network.h"
#include "downloader.h"
#include "download_ui.h"
#include "keyboard.h"
#include "console_settings_storage.h"

#define RU_MAX_ENTRIES 32
#define RU_MAX_GAMES 16
#define RU_VISIBLE_ROWS 8

static RemoteEntry s_entries[RU_MAX_ENTRIES];
static int s_game_rows[RU_MAX_GAMES]; /* indices into s_entries for game .bins */
static char s_status[RU_MAX_GAMES][4]; /* "NEW" / "UPD" / "OK"                 */

static const uint16_t s_move_notes[] = {NOTE_A5, 24U};

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

/* Find the .pak entry that pairs with a .bin (same base name), or NULL. */
static const RemoteEntry *findPak(int count, const char *bin_name)
{
    char pak[DOWNLOADER_NAME_MAX];
    strncpy(pak, bin_name, sizeof(pak) - 1U);
    pak[sizeof(pak) - 1U] = '\0';
    char *dot = strrchr(pak, '.');
    if (dot == NULL)
    {
        return NULL;
    }
    strncpy(dot, ".pak", (size_t)(&pak[sizeof(pak) - 1U] - dot));

    for (int i = 0; i < count; i++)
    {
        if (strcasecmp(s_entries[i].name, pak) == 0)
        {
            return &s_entries[i];
        }
    }
    return NULL;
}

/* Classify a local file vs the manifest CRC: NEW / UPD(ated) / OK. */
static void computeStatus(const RemoteEntry *e, char out[4])
{
    bool exists = false;
    const uint32_t local = downloaderLocalCrc(e->name, &exists);
    if (!exists)
    {
        strcpy(out, "NEW");
    }
    else if (local == e->crc32)
    {
        strcpy(out, "OK");
    }
    else
    {
        strcpy(out, "UPD");
    }
}

/* ---- Poll Remote Games ---- */

static void renderGameList(int num, int selected, int top)
{
    const bool cursor_on = ((getSysTime() / 450U) & 1U) == 0U;
    uint16_t n = 0U;

    rendererClear();
    n = menuDrawTitle(n, "REMOTE GAMES");

    if (num == 0)
    {
        n = menuDrawText(n, &font8x8, 60, 110, g_menu_pal_item, "No games on server");
    }
    for (int i = 0; i < RU_VISIBLE_ROWS && (top + i) < num; i++)
    {
        const int row = top + i;
        const RemoteEntry *e = &s_entries[s_game_rows[row]];
        const bool sel = (row == selected);
        const int16_t y = (int16_t)(MENU_LIST_TOP + i * MENU_ROW_H);

        if (sel && cursor_on)
        {
            n = menuDrawText(n, &font8x8, 42, y, g_menu_pal_accent, ">");
        }
        n = menuDrawText(n, &font8x8, 60, y, sel ? g_menu_pal_item_sel : g_menu_pal_item, e->name);

        const char *tag = s_status[row];
        const uint16_t *tag_pal = (strcmp(tag, "OK") == 0) ? g_menu_pal_footer : g_menu_pal_accent;
        const int16_t tx = (int16_t)(rendererGetWidthPixels() - 60 - (int16_t)menuTextWidth(font8x8.size, tag));
        n = menuDrawText(n, &font8x8, tx, y, tag_pal, tag);
    }

    n = menuDrawFooter(n, "UP/DOWN   A download   B back");
    rendererSubmitLayer(LAYER_UI, g_menu_ui, n);
    rendererRender();
}

static void downloadGame(int count, int game_row)
{
    const RemoteEntry *bin = &s_entries[s_game_rows[game_row]];
    ProgressCtx ctx = {"REMOTE GAMES", bin->name, 0U};

    DownloadStatus st = downloaderFetchFile(bin, bin->name, progressCb, &ctx);
    if (st == DOWNLOAD_OK)
    {
        const RemoteEntry *pak = findPak(count, bin->name);
        if (pak != NULL)
        {
            ctx.file = pak->name;
            ctx.last_render = 0U;
            st = downloaderFetchFile(pak, pak->name, progressCb, &ctx);
        }
    }

    if (st == DOWNLOAD_OK)
    {
        buzzerPlay(0U, false, s_move_notes, 1U);
        computeStatus(bin, s_status[game_row]); /* now "OK" */
        downloadUiWait("REMOTE GAMES", "Download complete!", g_menu_pal_accent);
    }
    else
    {
        char m[48];
        snprintf(m, sizeof(m), "Failed: %s", downloaderStatusString(st));
        downloadUiWait("REMOTE GAMES", m, g_menu_pal_alert);
    }
}

void remoteGamesRun(void)
{
    menuResetSurface();
    if (!ensureConnected("REMOTE GAMES"))
    {
        return;
    }

    downloadUiInfo("REMOTE GAMES", "Fetching list...", g_menu_pal_item_sel);
    const int count = downloaderFetchManifest(s_entries, RU_MAX_ENTRIES);
    if (count <= 0)
    {
        downloadUiWait("REMOTE GAMES", "No manifest / server error", g_menu_pal_alert);
        return;
    }

    /* Collect the game .bin entries and classify each against the local copy. */
    int num = 0;
    for (int i = 0; i < count && num < RU_MAX_GAMES; i++)
    {
        if (strcasecmp(s_entries[i].category, "games") == 0 && nameEndsWith(s_entries[i].name, ".bin"))
        {
            s_game_rows[num] = i;
            computeStatus(&s_entries[i], s_status[num]);
            num++;
        }
    }

    int selected = 0;
    int top = 0;
    for (;;)
    {
        const MenuNav nav = menuPollNav();
        if (nav.back)
        {
            return;
        }
        if (nav.up && selected > 0)
        {
            selected--;
            buzzerPlay(0U, false, s_move_notes, 1U);
        }
        else if (nav.down && selected < num - 1)
        {
            selected++;
            buzzerPlay(0U, false, s_move_notes, 1U);
        }
        if (selected < top)
        {
            top = selected;
        }
        else if (selected >= top + RU_VISIBLE_ROWS)
        {
            top = selected - RU_VISIBLE_ROWS + 1;
        }

        if (nav.enter && num > 0)
        {
            downloadGame(count, selected);
        }

        renderGameList(num, selected, top);
    }
}

/* ---- Edit server address (Settings) ---- */

void remoteServerAddrRun(void)
{
    menuResetSurface();

    char addr[80];
    downloaderGetServerAddr(addr, sizeof(addr)); /* current value, pre-fills the keyboard */

    if (keyboardEnter("SERVER ADDRESS", addr, sizeof(addr), false))
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

/* ---- Download WiFi firmware ---- */

void remoteWifiFirmwareRun(void)
{
    menuResetSurface();
    if (!ensureConnected("WIFI FIRMWARE"))
    {
        return;
    }

    downloadUiInfo("WIFI FIRMWARE", "Fetching list...", g_menu_pal_item_sel);
    const int count = downloaderFetchManifest(s_entries, RU_MAX_ENTRIES);
    if (count <= 0)
    {
        downloadUiWait("WIFI FIRMWARE", "No manifest / server error", g_menu_pal_alert);
        return;
    }

    const RemoteEntry *fw = NULL;
    for (int i = 0; i < count; i++)
    {
        if (strcasecmp(s_entries[i].category, "wifi") == 0 && nameEndsWith(s_entries[i].name, ".bin"))
        {
            fw = &s_entries[i];
            break;
        }
    }
    if (fw == NULL)
    {
        downloadUiWait("WIFI FIRMWARE", "No firmware on server", g_menu_pal_alert);
        return;
    }

    ProgressCtx ctx = {"WIFI FIRMWARE", fw->name, 0U};
    const DownloadStatus st = downloaderFetchFile(fw, fw->name, progressCb, &ctx);
    if (st == DOWNLOAD_OK)
    {
        downloadUiWait("WIFI FIRMWARE", "Done - use Upgrade WiFi module", g_menu_pal_accent);
    }
    else
    {
        char m[48];
        snprintf(m, sizeof(m), "Failed: %s", downloaderStatusString(st));
        downloadUiWait("WIFI FIRMWARE", m, g_menu_pal_alert);
    }
}
