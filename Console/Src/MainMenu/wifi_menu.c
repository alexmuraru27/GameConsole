#include "wifi_menu.h"

#include <stdio.h>
#include <string.h>

#include "menu_common.h"
#include "renderer.h"
#include "buzzer.h"
#include "sysclock.h"
#include "fonts.h"
#include "logger.h"
#include "network.h"
#include "keyboard.h"
#include "download_ui.h"
#include "console_settings_storage.h"

#define WIFI_MAX_APS 16
#define WIFI_VISIBLE_ROWS 8

static NetworkAp s_aps[WIFI_MAX_APS];

static const uint16_t s_move_notes[] = {NOTE_A5, 24U};

/* ---- credential persistence (console settings) ---- */

static void loadSaved(char *ssid, char *pass, bool *valid)
{
    ConsoleSettings s;
    consoleSettingsLoad(&s);
    *valid = (s.wifi_valid != 0U);
    strncpy(ssid, s.wifi_ssid, CONSOLE_WIFI_SSID_SIZE - 1U);
    ssid[CONSOLE_WIFI_SSID_SIZE - 1U] = '\0';
    strncpy(pass, s.wifi_pass, CONSOLE_WIFI_PASS_SIZE - 1U);
    pass[CONSOLE_WIFI_PASS_SIZE - 1U] = '\0';
}

static void saveCreds(const char *ssid, const char *pass)
{
    ConsoleSettings s;
    consoleSettingsLoad(&s);
    s.wifi_valid = 1U;
    strncpy(s.wifi_ssid, ssid, CONSOLE_WIFI_SSID_SIZE - 1U);
    s.wifi_ssid[CONSOLE_WIFI_SSID_SIZE - 1U] = '\0';
    strncpy(s.wifi_pass, pass, CONSOLE_WIFI_PASS_SIZE - 1U);
    s.wifi_pass[CONSOLE_WIFI_PASS_SIZE - 1U] = '\0';
    consoleSettingsSave(&s);
    LOGGER_LOG_INFO(LOGGER_NETWORK, "saved wifi creds for '%s'", ssid);
}

/* ---- rendering ---- */

static const char *signalBars(int8_t rssi)
{
    if (rssi >= -55)
    {
        return "||||";
    }
    if (rssi >= -67)
    {
        return "|||";
    }
    if (rssi >= -78)
    {
        return "||";
    }
    return "|";
}

static void renderList(int count, int selected, int top, const char *saved_ssid, bool saved_valid)
{
    const bool cursor_on = ((getSysTime() / 450U) & 1U) == 0U;
    uint16_t n = 0U;

    rendererClear();
    n = menuDrawTitle(n, "WIFI NETWORKS");

    char status[48];
    if (saved_valid)
    {
        snprintf(status, sizeof(status), "saved: %s", saved_ssid);
    }
    else
    {
        snprintf(status, sizeof(status), "no saved network");
    }
    n = menuDrawText(n, &font5x5, 60, 70, g_menu_pal_footer, status);

    for (int i = 0; i < WIFI_VISIBLE_ROWS && (top + i) < count; i++)
    {
        const int idx = top + i;
        const NetworkAp *ap = &s_aps[idx];
        const bool sel = (idx == selected);
        const int16_t y = (int16_t)(MENU_LIST_TOP + i * MENU_ROW_H);
        const uint16_t *pal = sel ? g_menu_pal_item_sel : g_menu_pal_item;

        if (sel && cursor_on)
        {
            n = menuDrawText(n, &font8x8, 42, y, g_menu_pal_accent, ">");
        }
        n = menuDrawText(n, &font8x8, 60, y, pal, ap->ssid);

        char meta[12];
        snprintf(meta, sizeof(meta), "%s%s", signalBars(ap->rssi), (ap->enc != NP_ENC_OPEN) ? " #" : "");
        const int16_t mx = (int16_t)(rendererGetWidthPixels() - 60 - (int16_t)menuTextWidth(font8x8.size, meta));
        n = menuDrawText(n, &font8x8, mx, y, (ap->enc != NP_ENC_OPEN) ? g_menu_pal_footer : g_menu_pal_accent, meta);
    }

    n = menuDrawFooter(n, "UP/DOWN   A connect   B back");
    rendererSubmitLayer(LAYER_UI, g_menu_ui, n);
    rendererRender();
}

/* ---- flow ---- */

void wifiMenuRun(void)
{
    menuResetSurface();
    LOGGER_LOG_INFO(LOGGER_NETWORK, "wifi menu: scanning");

    /* Confirm the ESP link before the (slow) scan, so a dead/mismatched module
     * reports immediately instead of after the scan timeout. */
    if (!networkSync())
    {
        downloadUiWait("WIFI NETWORKS", "WiFi module not responding", g_menu_pal_alert);
        return;
    }

    downloadUiInfo("WIFI NETWORKS", "Scanning...", g_menu_pal_item_sel);

    const int count = networkScan(s_aps, WIFI_MAX_APS);
    if (count < 0)
    {
        downloadUiWait("WIFI NETWORKS", "WiFi module not responding", g_menu_pal_alert);
        return;
    }
    if (count == 0)
    {
        downloadUiWait("WIFI NETWORKS", "No networks found", g_menu_pal_alert);
        return;
    }

    char saved_ssid[CONSOLE_WIFI_SSID_SIZE];
    char saved_pass[CONSOLE_WIFI_PASS_SIZE];
    bool saved_valid;
    loadSaved(saved_ssid, saved_pass, &saved_valid);

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
        else if (nav.down && selected < count - 1)
        {
            selected++;
            buzzerPlay(0U, false, s_move_notes, 1U);
        }
        if (selected < top)
        {
            top = selected;
        }
        else if (selected >= top + WIFI_VISIBLE_ROWS)
        {
            top = selected - WIFI_VISIBLE_ROWS + 1;
        }

        if (nav.enter)
        {
            const NetworkAp *ap = &s_aps[selected];
            char pass[CONSOLE_WIFI_PASS_SIZE] = {0};
            bool have_pass = false;

            if (ap->enc == NP_ENC_OPEN)
            {
                have_pass = true; /* open network, no password */
            }
            else if (saved_valid && strcmp(saved_ssid, ap->ssid) == 0)
            {
                strncpy(pass, saved_pass, sizeof(pass) - 1U);
                have_pass = true; /* reuse the saved password */
            }
            else
            {
                have_pass = keyboardEnter("WIFI PASSWORD", pass, sizeof(pass), false);
                menuResetSurface();
            }

            if (have_pass)
            {
                char msg[64];
                snprintf(msg, sizeof(msg), "Connecting to %s...", ap->ssid);
                downloadUiInfo("WIFI NETWORKS", msg, g_menu_pal_item_sel);

                if (networkConnect(ap->ssid, pass))
                {
                    saveCreds(ap->ssid, pass);
                    strncpy(saved_ssid, ap->ssid, sizeof(saved_ssid) - 1U);
                    saved_ssid[sizeof(saved_ssid) - 1U] = '\0';
                    strncpy(saved_pass, pass, sizeof(saved_pass) - 1U);
                    saved_pass[sizeof(saved_pass) - 1U] = '\0';
                    saved_valid = true;
                    downloadUiWait("WIFI NETWORKS", "Connected!", g_menu_pal_accent);
                }
                else
                {
                    downloadUiWait("WIFI NETWORKS", "Connection failed", g_menu_pal_alert);
                }
            }
        }

        renderList(count, selected, top, saved_ssid, saved_valid);
    }
}
