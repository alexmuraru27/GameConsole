#include "Network/server_config.h"
#include "Network/downloader.h" /* downloaderGetServerAddr/SetServerAddr prototypes */

#include <string.h>

#include "network_protocol.h" /* NP_URL_MAX */
#include "Logger/logger.h"
#include "sd_layout.h"
#include "ff.h"

/* Example address used for the auto-created server.txt and as the fallback.
 * Either "host:port" or a full "http://host:port" works (see downloaderMakeUrl). */
#define DEFAULT_SERVER_ADDR "192.168.100.29:25568"

/* Written verbatim when server.txt is missing, so the format is self-evident.
 * Only the first line is parsed; the rest is a guiding comment. */
#define SERVER_CFG_TEMPLATE DEFAULT_SERVER_ADDR \
    "\n# Update server address. First line only; IP or hostname, optional http:// scheme:\n" \
    "#   192.168.1.50:25568   |   myserver:25568   |   http://myserver.lan:25568\n" \
    "# A hostname is resolved by the ESP via DNS (must be resolvable on your network).\n"

#define SERVER_CFG_PATH SD_DIR_SETTINGS "/server.txt"
#define URL_MAX NP_URL_MAX

static char s_base_url[URL_MAX];
static char s_url[URL_MAX];

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

const char *downloaderMakeUrl(const char *path)
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
