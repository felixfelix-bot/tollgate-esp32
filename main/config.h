#ifndef CONFIG_H
#define CONFIG_H

#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include <stdbool.h>
#include <stdint.h>

#define PAYOUT_MAX_RECIPIENTS     4
#define PAYOUT_MAX_MINTS          3
#define PAYOUT_MAX_ADDR_LEN       128

typedef struct {
    char lightning_address[PAYOUT_MAX_ADDR_LEN];
    double factor;
} payout_recipient_t;

typedef struct {
    char url[256];
    uint64_t min_balance;
    uint64_t min_payout_amount;
} payout_mint_config_t;

typedef struct {
    bool enabled;
    payout_mint_config_t mints[PAYOUT_MAX_MINTS];
    int mint_count;
    payout_recipient_t recipients[PAYOUT_MAX_RECIPIENTS];
    int recipient_count;
    uint64_t fee_tolerance_pct;
    int check_interval_s;
} payout_config_t;

#define TOLLGATE_MAX_WIFI_NETWORKS 5
#define TOLLGATE_MAX_MINT_URLS     8
#define TOLLGATE_MAX_AP_SSID_LEN   32
#define TOLLGATE_MAX_AP_PASS_LEN   64
#define TOLLGATE_MAX_RELAYS        4
#define TOLLGATE_MAX_SEED_RELAYS   8

typedef enum {
    MINING_PAYOUT_AUTO,
    MINING_PAYOUT_POOL,
    MINING_PAYOUT_UPSTREAM,
    MINING_PAYOUT_PROXY_ONLY
} mining_payout_mode_t;

typedef struct {
    char ssid[32];
    char password[64];
} wifi_network_t;

typedef struct {
    wifi_network_t networks[TOLLGATE_MAX_WIFI_NETWORKS];
    int network_count;
    int current_network;
    int max_retry;

    char nsec[65];
    char npub[65];

    char ap_ssid[TOLLGATE_MAX_AP_SSID_LEN];
    char ap_password[TOLLGATE_MAX_AP_PASS_LEN];
    uint8_t ap_channel;
    uint8_t ap_max_conn;

    uint8_t sta_mac[6];
    uint8_t ap_mac[6];

    esp_ip4_addr_t ap_ip;
    char ap_ip_str[16];

    char mint_url[256];
    char accepted_mints[TOLLGATE_MAX_MINT_URLS][256];
    int accepted_mint_count;
    char lnurl_url[256];
    int price_per_step;
    int step_size_ms;
    int step_size_bytes;
    char metric[16];
    uint64_t persist_threshold_sats;

    char nostr_geohash[16];
    char nostr_relays[TOLLGATE_MAX_RELAYS][128];
    int nostr_relay_count;
    int nostr_publish_interval_s;
    int nostr_sync_interval_s;
    int nostr_fallback_sync_interval_s;

    bool identity_initialized;

    bool client_enabled;
    int client_steps_to_buy;
    int client_renewal_threshold_pct;
    int client_retry_interval_ms;

    payout_config_t payout;

    bool cvm_enabled;
    char cvm_relays[256];

    char wifi_auth_mode[16];
    bool display_enabled;

    char nostr_seed_relays[TOLLGATE_MAX_SEED_RELAYS][128];
    int nostr_seed_relay_count;

    bool market_enabled;
    int market_scan_interval_s;
    bool client_auto_switch;

    bool sync_enabled;
    bool wifistr_enabled;
    bool local_relay_enabled;
    bool mint_health_enabled;

    bool mining_enabled;
    mining_payout_mode_t mining_payout_mode;
    char stratum_host[128];
    uint16_t stratum_port;
    char stratum_user[128];
    char stratum_pass[64];
    char stratum_fallback_host[128];
    uint16_t stratum_fallback_port;
    uint16_t mining_port;
    uint64_t hashprice_sats_per_ghs_day;
    bool mining_sandbox_mint_access;
    bool proxy_self_test;
    char faucet_url[256];
    int faucet_poll_interval_s;

    bool gps_enabled;
    int gps_uart_num;
    int gps_tx_pin;
    int gps_rx_pin;
    int gps_baud;
} tollgate_config_t;

void tollgate_config_derive_unique(tollgate_config_t *cfg);

esp_err_t tollgate_config_init(void);
const tollgate_config_t *tollgate_config_get(void);
esp_err_t tollgate_config_get_wifi(wifi_config_t *wifi_config);
esp_err_t tollgate_config_get_next_wifi(wifi_config_t *wifi_config);
esp_err_t tollgate_config_add_wifi(const char *ssid, const char *password);

#endif
