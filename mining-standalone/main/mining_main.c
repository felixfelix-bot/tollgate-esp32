/*
 * mining_main.c — Standalone ESP-IDF mining entry point.
 *
 * Phase 1: sw_miner + stratum_client extracted from the tollgate firmware.
 *
 * Boot sequence:
 *   nvs_flash_init() → mining_config_init() → netif/event-loop →
 *   WiFi STA start → (on got-IP) stratum_client_start() + sw_miner_start() →
 *   periodic hashrate logger.
 *
 * All tollgate-specific dependencies (identity, wallet, Nostr, config.json on
 * SPIFFS) are stubbed out; pool + WiFi settings come from Kconfig
 * (CONFIG_MINE_*).
 */
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include <string.h>

#include "mining_config.h"
#include "stratum_proxy.h"
#include "stratum_client.h"
#include "sw_miner.h"

static const char *TAG = "mine_main";

#define BIT_STA_CONNECTED  (1 << 0)

static EventGroupHandle_t s_wifi_events;
static esp_event_handler_instance_t s_wifi_evt_instance;
static esp_event_handler_instance_t s_ip_evt_instance;
static bool s_services_started = false;

static void wifi_event_handler(void *arg, esp_event_base_t base,
                                int32_t id, void *data)
{
    (void)arg;
    (void)data;

    if (base != WIFI_EVENT) return;

    switch (id) {
    case WIFI_EVENT_STA_START:
        ESP_LOGI(TAG, "WiFi STA started, connecting...");
        esp_wifi_connect();
        break;
    case WIFI_EVENT_STA_DISCONNECTED: {
        xEventGroupClearBits(s_wifi_events, BIT_STA_CONNECTED);
        ESP_LOGW(TAG, "WiFi disconnected, retrying in 1s");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_wifi_connect();
        break;
    }
    default:
        break;
    }
}

static void ip_event_handler(void *arg, esp_event_base_t base,
                              int32_t id, void *data)
{
    (void)arg;

    if (base != IP_EVENT || id != IP_EVENT_STA_GOT_IP) return;

    const ip_event_got_ip_t *evt = (const ip_event_got_ip_t *)data;
    ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&evt->ip_info.ip));
    xEventGroupSetBits(s_wifi_events, BIT_STA_CONNECTED);

    /* Start stratum + miner once (idempotent guards inside each). */
    if (!s_services_started) {
        s_services_started = true;
        ESP_LOGI(TAG, "Starting stratum client + sw_miner");

        stratum_proxy_init(0, false);
        stratum_client_init();

        esp_err_t e1 = stratum_client_start();
        esp_err_t e2 = sw_miner_start();
        ESP_LOGI(TAG, "stratum_client_start=%s sw_miner_start=%s",
                 esp_err_to_name(e1), esp_err_to_name(e2));
    }
}

static void wifi_init_sta(void)
{
    s_wifi_events = xEventGroupCreate();

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL,
        &s_wifi_evt_instance));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler, NULL,
        &s_ip_evt_instance));

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    const char *ssid = mining_config_get_wifi_ssid();
    const char *pass = mining_config_get_wifi_pass();
    strncpy((char *)wifi_config.sta.ssid, ssid,
            sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, pass,
            sizeof(wifi_config.sta.password) - 1);

    ESP_LOGI(TAG, "Connecting to WiFi SSID: %s",
             ssid[0] ? ssid : "(empty — set CONFIG_MINE_WIFI_SSID)");

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

/* Periodic hashrate + stratum stats logger (every 10s). */
static void stats_logger_task(void *arg)
{
    (void)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(10000));

        double hr = sw_miner_get_hashrate();
        const stratum_client_state_t *st = stratum_client_get_state();

        ESP_LOGI(TAG,
                 "[stats] hashrate=%.4f MH/s  pool=%s:%u  connected=%d  "
                 "shares ok/rej=%llu/%llu  nbits=0x%08lx",
                 hr,
                 st->pool_host[0] ? st->pool_host : "(none)",
                 (unsigned)st->pool_port,
                 (int)st->connected,
                 (unsigned long long)st->shares_accepted,
                 (unsigned long long)st->shares_rejected,
                 (unsigned long)st->nbits);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== mining-standalone Phase 1 (sw_miner + stratum) ===");

    /* NVS — required by WiFi. */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* Load Kconfig values into the tollgate_config_t stub. */
    mining_config_init();

    /* Netif + event loop. */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* WiFi STA. */
    wifi_init_sta();

    /* Periodic stats logger — runs independently of WiFi state. */
    xTaskCreate(stats_logger_task, "stats_log", 4096, NULL, 1, NULL);

    ESP_LOGI(TAG, "Boot complete. Waiting for WiFi IP to start mining...");
}
