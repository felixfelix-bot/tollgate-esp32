#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "lwip/netif.h"
#include "lwip/dns.h"
#include "esp_sntp.h"
#include "dhcpserver/dhcpserver.h"
#include "tollgate_core.h"
#include "tollgate_core_firewall.h"
#include "tollgate_core_mining.h"
#include "tollgate_esp_platform.h"
#include "config.h"
#include "identity.h"
#include "dns_server.h"
#include "captive_portal.h"
#include "tollgate_api.h"
#include "nucula_wallet.h"
#include "wifistr.h"
#include "tollgate_client.h"
#include "lightning_payout.h"
#include "mint_health.h"
#include "cvm_server.h"
#include "display.h"
#include "local_relay.h"
#include "relay_selector.h"
#include "sync_manager.h"
#include "beacon_price.h"
#include "market.h"
#include "gps.h"
#include "stratum_client.h"
#include "stratum_proxy.h"
#include "sw_miner.h"
#include "asic_miner.h"
#include "faucet_client.h"
#include "lwip/prot/ip4.h"

int tollgate_ip4_canforward_filter(struct pbuf *p, u32_t dest_addr_hostorder)
{
    return tollgate_core_ip4_canforward_filter(p, dest_addr_hostorder);
}

#define MAX_STA_RETRY 5
static const char *TAG = "tollgate_main";

static EventGroupHandle_t s_wifi_event_group;
static const int WIFI_CONNECTED_BIT = BIT0;

static esp_netif_t *s_sta_netif = NULL;
static esp_netif_t *s_ap_netif = NULL;
static int s_retry_count = 0;
static bool s_services_running = false;
static bool s_ap_services_running = false;
static bool s_sta_connecting = false;
static SemaphoreHandle_t s_services_mutex = NULL;
static char s_ap_ip_str[16] = "10.0.0.1";

static relay_selector_t s_relay_selector;
static sync_manager_t s_sync_manager;

volatile bool s_start_services_called = false;
volatile bool s_start_ap_services_called = false;
volatile bool s_sta_got_ip = false;
volatile bool s_ap_started = false;
volatile esp_ip4_addr_t s_sta_ip = {0};
volatile esp_ip4_addr_t s_sta_gw = {0};

static void start_services(void);
static void stop_services(void);
static void start_ap_services(void);

static void start_ap_services(void)
{
    s_start_ap_services_called = true;
    if (s_ap_services_running) return;

    const tollgate_config_t *cfg = tollgate_config_get();
    captive_portal_start(cfg->ap_ip_str);
    tollgate_api_start();
    beacon_price_start();
    market_init();

    if (cfg->mining_enabled) {
        tollgate_core_fw_set_sandbox_ports(cfg->mining_port);
        tollgate_core_fw_set_sandbox_mint_access(cfg->mining_sandbox_mint_access);
    }

    s_ap_services_running = true;
    ESP_LOGI(TAG, "=== AP-only services started (no STA) ===");
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        if (!s_sta_connecting) {
            wifi_config_t wifi_cfg;
            if (tollgate_config_get_wifi(&wifi_cfg) == ESP_OK) {
                esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
            }
            s_sta_connecting = true;
            esp_wifi_connect();
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *disc = (wifi_event_sta_disconnected_t *)event_data;
        s_retry_count++;
        s_sta_connecting = false;
        ESP_LOGW(TAG, "WiFi disconnected, reason=%d, retry %d/%d", disc->reason, s_retry_count, MAX_STA_RETRY);
        tollgate_client_on_sta_disconnected();
        if (s_services_running) stop_services();
        if (s_retry_count < MAX_STA_RETRY) {
            vTaskDelay(pdMS_TO_TICKS(2000));
            s_sta_connecting = true;
            esp_wifi_connect();
        } else {
            wifi_config_t wifi_cfg;
            if (tollgate_config_get_next_wifi(&wifi_cfg) == ESP_OK) {
                esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
                const tollgate_config_t *cfg = tollgate_config_get();
                int idx = cfg->current_network;
                ESP_LOGI(TAG, "Trying WiFi network %d: %s", idx, cfg->networks[idx].ssid);
                s_retry_count = 0;
                vTaskDelay(pdMS_TO_TICKS(2000));
                s_sta_connecting = true;
                esp_wifi_connect();
            } else {
                ESP_LOGI(TAG, "All WiFi networks exhausted, STA stopped (market scans active)");
            }
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "Station connected: MAC=%02x:%02x:%02x:%02x:%02x:%02x",
                 event->mac[0], event->mac[1], event->mac[2],
                 event->mac[3], event->mac[4], event->mac[5]);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(TAG, "Station disconnected: MAC=%02x:%02x:%02x:%02x:%02x:%02x",
                 event->mac[0], event->mac[1], event->mac[2],
                 event->mac[3], event->mac[4], event->mac[5]);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_START) {
        s_ap_started = true;
        start_ap_services();
    }
}

static void services_start_task(void *pvParameters)
{
    start_services();
    vTaskDelete(NULL);
}

static void ip_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR ", GW:" IPSTR, IP2STR(&event->ip_info.ip), IP2STR(&event->ip_info.gw));
        s_retry_count = 0;
        s_sta_got_ip = true;
        s_sta_ip.addr = event->ip_info.ip.addr;
        s_sta_gw.addr = event->ip_info.gw.addr;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

        static TaskHandle_t s_svc_task = NULL;
        if (s_svc_task == NULL) {
            xTaskCreate(services_start_task, "svc_start", 16384, NULL, 5, &s_svc_task);
            ESP_LOGI(TAG, "services_start_task spawned (3s delay)");
        }

        esp_sntp_stop();
        esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, "pool.ntp.org");
        esp_sntp_setservername(1, "time.google.com");
        esp_sntp_init();
        ESP_LOGI(TAG, "SNTP time sync started");

        char gw_ip_str[16];
        snprintf(gw_ip_str, sizeof(gw_ip_str), IPSTR, IP2STR(&event->ip_info.gw));
        tollgate_client_on_sta_connected(gw_ip_str);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_LOST_IP) {
        ESP_LOGW(TAG, "Lost IP address");
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        stop_services();
    }
}

static void publish_wifistr_task(void *pvParameters)
{
    vTaskDelay(pdMS_TO_TICKS(5000));
    wifistr_publish();
    const tollgate_config_t *cfg = tollgate_config_get();
    wifistr_start_periodic(cfg->nostr_publish_interval_s);
    vTaskDelete(NULL);
}

static void start_services(void)
{
    vTaskDelay(pdMS_TO_TICKS(3000));
    ESP_LOGI(TAG, ">>> start_services() called");
    if (s_services_mutex) xSemaphoreTake(s_services_mutex, portMAX_DELAY);
    if (s_services_running) {
        if (s_services_mutex) xSemaphoreGive(s_services_mutex);
        return;
    }

    s_start_services_called = true;

    esp_netif_get_ip_info(s_ap_netif, &(esp_netif_ip_info_t){0});
    esp_netif_ip_info_t ap_ip_info;
    esp_netif_get_ip_info(s_ap_netif, &ap_ip_info);

    esp_ip4_addr_t upstream_dns;
    const ip_addr_t *dns_addr = dns_getserver(0);
    upstream_dns.addr = dns_addr->addr;

    if (upstream_dns.addr == ap_ip_info.ip.addr) {
        ESP_LOGW(TAG, "DNS[0] is our own AP IP — trying DNS[1] and STA gateway");
        const ip_addr_t *dns1 = dns_getserver(1);
        if (dns1->addr != 0 && dns1->addr != ap_ip_info.ip.addr) {
            upstream_dns.addr = dns1->addr;
            dns_setserver(0, dns1);
            ESP_LOGI(TAG, "Fixed DNS[0] to " IPSTR " from DNS[1]", IP2STR(&upstream_dns));
        } else {
            esp_netif_dns_info_t sta_dns;
            if (esp_netif_get_dns_info(s_sta_netif, ESP_NETIF_DNS_MAIN, &sta_dns) == ESP_OK
                && sta_dns.ip.u_addr.ip4.addr != 0
                && sta_dns.ip.u_addr.ip4.addr != ap_ip_info.ip.addr) {
                upstream_dns.addr = sta_dns.ip.u_addr.ip4.addr;
                ip_addr_t lwip_dns = {0};
                lwip_dns.addr = sta_dns.ip.u_addr.ip4.addr;
                dns_setserver(0, &lwip_dns);
                ESP_LOGI(TAG, "Fixed DNS[0] to " IPSTR " from STA netif", IP2STR(&upstream_dns));
            } else {
                ESP_LOGE(TAG, "No valid upstream DNS found! Mint verification will fail.");
            }
        }
    }

    ESP_LOGI(TAG, "DNS config: [0]=" IPSTR " [1]=" IPSTR " [2]=" IPSTR,
             IP2STR(&(esp_ip4_addr_t){.addr=dns_getserver(0)->addr}),
             IP2STR(&(esp_ip4_addr_t){.addr=dns_getserver(1)->addr}),
             IP2STR(&(esp_ip4_addr_t){.addr=dns_getserver(2)->addr}));

    tollgate_core_init(tollgate_esp_get_platform(), ap_ip_info.ip);

    const tollgate_config_t *cfg = tollgate_config_get();

    if (cfg->cvm_enabled) {
        cvm_server_init();
        cvm_server_start();
    }

    if (cfg->gps_enabled) {
        if (gps_start(cfg->gps_uart_num, cfg->gps_tx_pin, cfg->gps_rx_pin, cfg->gps_baud) == ESP_OK) {
            ESP_LOGI(TAG, "GPS NMEA reader enabled (UART%d tx=%d rx=%d baud=%d)",
                     cfg->gps_uart_num, cfg->gps_tx_pin, cfg->gps_rx_pin, cfg->gps_baud);
        } else {
            ESP_LOGE(TAG, "GPS NMEA reader failed to start");
        }
    }

    if (cfg->mint_health_enabled) {
        mint_health_init(cfg->accepted_mints, cfg->accepted_mint_count);
        mint_health_start();
    }

    if (cfg->accepted_mint_count > 1) {
        nucula_wallet_init_multi(cfg->accepted_mints, cfg->accepted_mint_count);
    } else {
        nucula_wallet_init(cfg->mint_url);
    }

    if (cfg->mining_enabled) {
        tollgate_core_fw_set_sandbox_ports(cfg->mining_port);
        tollgate_core_fw_set_sandbox_mint_access(cfg->mining_sandbox_mint_access);
    }
    lightning_payout_init(&cfg->payout);

    dns_server_start(ap_ip_info.ip, upstream_dns);
    captive_portal_start(cfg->ap_ip_str);
    if (!s_ap_services_running) {
        tollgate_api_start();
        beacon_price_start();
        market_init();
    }

    if (cfg->sync_enabled || cfg->wifistr_enabled) {
        relay_selector_init(&s_relay_selector);
        relay_selector_seed_from_config(&s_relay_selector);
    }

    if (cfg->sync_enabled) {
        sync_manager_init(&s_sync_manager, &s_relay_selector);
        sync_manager_start(&s_sync_manager);
    }

    if (cfg->wifistr_enabled) {
        xTaskCreate(publish_wifistr_task, "wifistr_init", 16384, NULL, 3, NULL);
    }

    if (cfg->mining_enabled) {
        ESP_LOGI(TAG, "Mining subsystem enabled, initializing...");
        stratum_client_init();
        stratum_proxy_init(cfg->mining_port, cfg->proxy_self_test);

        if (cfg->mining_payout_mode != MINING_PAYOUT_UPSTREAM) {
            stratum_client_start();
        }

        asic_miner_init();
        if (asic_miner_is_present()) {
            asic_miner_start();
            ESP_LOGI(TAG, "ASIC miner started");
        } else {
            sw_miner_start();
            ESP_LOGI(TAG, "Software miner started (no ASIC)");
        }
    }

    if (cfg->faucet_url[0] != '\0') {
        faucet_client_start();
    }

    s_services_running = true;
    if (s_services_mutex) xSemaphoreGive(s_services_mutex);
    ESP_LOGI(TAG, "=== TollGate services started ===");

    if (tollgate_config_get()->display_enabled) {
        display_set_state(DISPLAY_READY);
        char portal_url[128];
        snprintf(portal_url, sizeof(portal_url), "http://%s/", cfg->ap_ip_str);
        display_update(cfg->ap_ssid, 0, 0, portal_url, cfg->mint_url, cfg->price_per_step, "connected");
    }
}

static void stop_services(void)
{
    if (s_services_mutex) xSemaphoreTake(s_services_mutex, portMAX_DELAY);
    if (!s_services_running) {
        if (s_services_mutex) xSemaphoreGive(s_services_mutex);
        return;
    }

    captive_portal_stop();
    if (!s_ap_services_running) {
        tollgate_api_stop();
        beacon_price_stop();
    }
    const tollgate_config_t *cfg = tollgate_config_get();
    dns_server_stop();
    cvm_server_stop();
    if (cfg->sync_enabled) sync_manager_stop(&s_sync_manager);
    if (cfg->local_relay_enabled) local_relay_stop();
    if (cfg->sync_enabled || cfg->wifistr_enabled) relay_selector_destroy(&s_relay_selector);
    tollgate_core_fw_revoke_all();
    s_services_running = false;
    if (s_services_mutex) xSemaphoreGive(s_services_mutex);
    ESP_LOGI(TAG, "=== TollGate services stopped ===");
}

static void wifi_create_ap_netif(void)
{
    s_ap_netif = esp_netif_create_default_wifi_ap();

     const tollgate_config_t *cfg = tollgate_config_get();
     esp_ip4_addr_t ap_ip = cfg->ap_ip;
     esp_ip4_addr_t ap_gw = ap_ip;
     esp_ip4_addr_t ap_mask;
    IP4_ADDR(&ap_mask, 255, 255, 255, 0);

    strncpy(s_ap_ip_str, cfg->ap_ip_str, sizeof(s_ap_ip_str) - 1);

    esp_netif_ip_info_t ip_info = {
        .ip.addr = ap_ip.addr,
        .gw.addr = ap_gw.addr,
        .netmask.addr = ap_mask.addr,
    };
    ESP_ERROR_CHECK(esp_netif_dhcps_stop(s_ap_netif));
    ESP_ERROR_CHECK(esp_netif_set_ip_info(s_ap_netif, &ip_info));
    ESP_ERROR_CHECK(esp_netif_dhcps_start(s_ap_netif));

    esp_netif_dns_info_t dns_info;
    dns_info.ip.u_addr.ip4.addr = ip_info.ip.addr;
    dns_info.ip.type = IPADDR_TYPE_V4;
    esp_netif_set_dns_info(s_ap_netif, ESP_NETIF_DNS_MAIN, &dns_info);
    ESP_LOGI(TAG, "AP DNS server set to " IPSTR, IP2STR(&ip_info.ip));

    dhcps_offer_t offer_dns = true;
    esp_netif_dhcps_option(s_ap_netif, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER,
                           &offer_dns, sizeof(offer_dns));
}

static void wifi_configure_ap(void)
{
    const tollgate_config_t *cfg = tollgate_config_get();
    wifi_config_t ap_config = {0};
    strncpy((char *)ap_config.ap.ssid, cfg->ap_ssid, sizeof(ap_config.ap.ssid) - 1);
    if (strlen(cfg->ap_password) > 0) {
        strncpy((char *)ap_config.ap.password, cfg->ap_password, sizeof(ap_config.ap.password) - 1);
        ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        ap_config.ap.authmode = WIFI_AUTH_OPEN;
    }
    ap_config.ap.channel = cfg->ap_channel;
    ap_config.ap.max_connection = cfg->ap_max_conn;
    ap_config.ap.ssid_hidden = 0;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_LOGI(TAG, "AP configured: SSID='%s', channel=%d", cfg->ap_ssid, cfg->ap_channel);
}

static void wifi_init_sta(void)
{
    s_sta_netif = esp_netif_create_default_wifi_sta();
}

 void app_main(void)
{
    ESP_LOGI(TAG, "=== TollGate ESP32 Starting ===");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(tollgate_config_init());

    if (tollgate_config_get()->display_enabled) {
        if (display_init() == ESP_OK) {
            display_set_state(DISPLAY_BOOT);
        } else {
            ESP_LOGW(TAG, "Display init failed — continuing without display");
        }
    }

    ESP_ERROR_CHECK(identity_init(tollgate_config_get()->nsec));

    tollgate_config_derive_unique((tollgate_config_t *)tollgate_config_get());

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    s_wifi_event_group = xEventGroupCreate();
    s_services_mutex = xSemaphoreCreateMutex();

    wifi_init_sta();
    wifi_create_ap_netif();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                         &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                         &ip_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_LOST_IP,
                                                         &ip_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

    const tollgate_config_t *tcfg = tollgate_config_get();
    ESP_ERROR_CHECK(esp_wifi_set_mac(WIFI_IF_STA, tcfg->sta_mac));
    ESP_ERROR_CHECK(esp_wifi_set_mac(WIFI_IF_AP, tcfg->ap_mac));
    ESP_LOGI(TAG, "MACs set from identity");

    wifi_configure_ap();

    wifi_config_t sta_config;
    if (tollgate_config_get_wifi(&sta_config) == ESP_OK) {
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
        const tollgate_config_t *tcfg2 = tollgate_config_get();
        ESP_LOGI(TAG, "STA configured for SSID: %s", tcfg2->networks[tcfg2->current_network].ssid);
    }

    ESP_ERROR_CHECK(esp_wifi_set_country_code("DE", true));

    ESP_ERROR_CHECK(esp_wifi_start());

    if (tollgate_config_get()->local_relay_enabled) {
        local_relay_init();
        local_relay_start();
    }

    ESP_LOGI(TAG, "WiFi AP+STA started, waiting for connection...");

    if (tollgate_config_get_wifi(&(wifi_config_t){0}) != ESP_OK) {
        ESP_LOGI(TAG, "No STA network configured, starting services immediately");
        xTaskCreate(services_start_task, "svc_start_fb", 16384, NULL, 5, NULL);
    }

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        tollgate_core_tick();
        tollgate_client_tick();
        lightning_payout_tick();
        market_tick();
        stratum_client_tick();
    }
}
