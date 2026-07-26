/*
 * mining_config.c — Provides tollgate_config_get() for the standalone build.
 *
 * Reads stratum pool + WiFi settings from Kconfig (menuconfig) and exposes
 * them through the tollgate_config_get() API that stratum_client.c expects.
 */
#include "mining_config.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "mine_cfg";

static tollgate_config_t s_config = {0};
static char s_wifi_ssid[33] = {0};
static char s_wifi_pass[65] = {0};
static bool s_initialized = false;

void mining_config_init(void)
{
    memset(&s_config, 0, sizeof(s_config));

    /* Pull values from menuconfig */
#ifdef CONFIG_MINE_STRATUM_HOST
    strncpy(s_config.stratum_host, CONFIG_MINE_STRATUM_HOST,
            sizeof(s_config.stratum_host) - 1);
#endif
    s_config.stratum_port = CONFIG_MINE_STRATUM_PORT;
#ifdef CONFIG_MINE_STRATUM_USER
    strncpy(s_config.stratum_user, CONFIG_MINE_STRATUM_USER,
            sizeof(s_config.stratum_user) - 1);
#endif
#ifdef CONFIG_MINE_STRATUM_PASS
    strncpy(s_config.stratum_pass, CONFIG_MINE_STRATUM_PASS,
            sizeof(s_config.stratum_pass) - 1);
#endif
#ifdef CONFIG_MINE_WIFI_SSID
    strncpy(s_wifi_ssid, CONFIG_MINE_WIFI_SSID, sizeof(s_wifi_ssid) - 1);
#endif
#ifdef CONFIG_MINE_WIFI_PASS
    strncpy(s_wifi_pass, CONFIG_MINE_WIFI_PASS, sizeof(s_wifi_pass) - 1);
#endif

    s_initialized = true;
    ESP_LOGI(TAG, "Mining config: pool=%s:%u user=%.20s...",
             s_config.stratum_host[0] ? s_config.stratum_host : "(none)",
             (unsigned)s_config.stratum_port,
             s_config.stratum_user);
}

void mining_config_set_wifi(const char *ssid, const char *pass)
{
    if (ssid) {
        strncpy(s_wifi_ssid, ssid, sizeof(s_wifi_ssid) - 1);
        s_wifi_ssid[sizeof(s_wifi_ssid) - 1] = '\0';
    }
    if (pass) {
        strncpy(s_wifi_pass, pass, sizeof(s_wifi_pass) - 1);
        s_wifi_pass[sizeof(s_wifi_pass) - 1] = '\0';
    }
}

const char *mining_config_get_wifi_ssid(void) { return s_wifi_ssid; }
const char *mining_config_get_wifi_pass(void) { return s_wifi_pass; }

const tollgate_config_t *tollgate_config_get(void)
{
    if (!s_initialized) {
        mining_config_init();
    }
    return &s_config;
}
