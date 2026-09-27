#include "config.h"
#include "identity.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "lwip/ip4_addr.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "tollgate_config";
static tollgate_config_t g_config;

esp_err_t tollgate_config_init(void)
{
    memset(&g_config, 0, sizeof(g_config));
    g_config.max_retry = 5;
    g_config.ap_channel = 1;
    g_config.ap_max_conn = 4;
    g_config.price_per_step = 21;
    g_config.step_size_ms = 60000;
    g_config.step_size_bytes = 22020096;
    strncpy(g_config.metric, "milliseconds", sizeof(g_config.metric) - 1);
    g_config.persist_threshold_sats = 1;
    g_config.nostr_publish_interval_s = 21600;
    g_config.nostr_sync_interval_s = 1800;
    g_config.nostr_fallback_sync_interval_s = 21600;
    g_config.client_enabled = false;
    g_config.client_steps_to_buy = 1;
    g_config.client_renewal_threshold_pct = 20;
    g_config.client_retry_interval_ms = 30000;
    g_config.payout.enabled = true;
    g_config.payout.fee_tolerance_pct = 10;
    g_config.payout.check_interval_s = 60;
    g_config.payout.recipient_count = 0;
    g_config.payout.mint_count = 0;
    g_config.cvm_enabled = true;
    strncpy(g_config.cvm_relays, "wss://nos.lol", sizeof(g_config.cvm_relays) - 1);
    strncpy(g_config.wifi_auth_mode, "WPA2", sizeof(g_config.wifi_auth_mode) - 1);
    g_config.display_enabled = true;
    g_config.nostr_sync_interval_s = 1800;
    g_config.nostr_fallback_sync_interval_s = 21600;
    g_config.mining_enabled = false;
    g_config.mining_payout_mode = MINING_PAYOUT_AUTO;
    g_config.stratum_port = 3333;
    g_config.mining_port = 3334;
    g_config.mining_sandbox_mint_access = false;
    g_config.proxy_self_test = true;
    g_config.faucet_poll_interval_s = 120;
    g_config.sync_enabled = true;
    g_config.wifistr_enabled = true;
    g_config.local_relay_enabled = true;
    g_config.mint_health_enabled = true;
    g_config.market_enabled = true;
    g_config.market_scan_interval_s = 30;
    g_config.client_auto_switch = false;
    g_config.gps_enabled = false;
    g_config.gps_uart_num = 1;
    g_config.gps_tx_pin = 8;
    g_config.gps_rx_pin = 9;
    g_config.gps_baud = 9600;

    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true,
    };
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SPIFFS: %s", esp_err_to_name(ret));
        return ret;
    }

    FILE *f = fopen("/spiffs/config.json", "r");
    if (!f) {
        ESP_LOGW(TAG, "No config.json found, generating default");
        const char *default_json = "{"
            "\"nsec\":\"a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2\","
            "\"wifi_networks\":["
              "{\"ssid\":\"c03rad0r\",\"password\":\"c03rad0r123\"}"
            "],"
            "\"ap_password\":\"\","
            "\"mint_url\":\"https://testnut.cashu.exchange\","
            "\"accepted_mints\":[\"https://testnut.cashu.exchange\"],"
            "\"price_per_step\":21,"
            "\"step_size_ms\":60000,"
            "\"nostr_geohash\":\"u281w0dfz\","
            "\"nostr_relays\":[\"wss://relay.damus.io\",\"wss://nos.lol\"],"
            "\"nostr_publish_interval_s\":21600,"
            "\"nostr_sync_interval_s\":1800,"
            "\"nostr_fallback_sync_interval_s\":21600,"
            "\"client_enabled\":false,"
            "\"client_steps_to_buy\":1,"
            "\"client_renewal_threshold_pct\":20,"
            "\"client_retry_interval_ms\":30000"
          "}";
        f = fopen("/spiffs/config.json", "w");
        if (f) {
            fputs(default_json, f);
            fclose(f);
        }
        f = fopen("/spiffs/config.json", "r");
    }

    if (!f) {
        ESP_LOGE(TAG, "Failed to open config.json");
        return ESP_FAIL;
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(fsize + 1);
    if (!buf) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }
    fread(buf, 1, fsize, f);
    buf[fsize] = '\0';
    fclose(f);

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse config.json");
        return ESP_FAIL;
    }

    cJSON *nsec = cJSON_GetObjectItem(root, "nsec");
    if (nsec && cJSON_IsString(nsec)) {
        strncpy(g_config.nsec, nsec->valuestring, sizeof(g_config.nsec) - 1);
    } else {
        ESP_LOGE(TAG, "Missing 'nsec' in config.json");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    cJSON *networks = cJSON_GetObjectItem(root, "wifi_networks");
    if (networks && cJSON_IsArray(networks)) {
        int count = cJSON_GetArraySize(networks);
        if (count > TOLLGATE_MAX_WIFI_NETWORKS) count = TOLLGATE_MAX_WIFI_NETWORKS;
        for (int i = 0; i < count; i++) {
            cJSON *net = cJSON_GetArrayItem(networks, i);
            cJSON *ssid = cJSON_GetObjectItem(net, "ssid");
            cJSON *pass = cJSON_GetObjectItem(net, "password");
            if (ssid && pass) {
                strncpy(g_config.networks[i].ssid, ssid->valuestring, sizeof(g_config.networks[i].ssid) - 1);
                strncpy(g_config.networks[i].password, pass->valuestring, sizeof(g_config.networks[i].password) - 1);
                g_config.network_count++;
            }
        }
    }

    if (g_config.network_count == 0) {
        cJSON *ssid = cJSON_GetObjectItem(root, "wifi_ssid");
        cJSON *pass = cJSON_GetObjectItem(root, "wifi_password");
        if (ssid && cJSON_IsString(ssid) && pass && cJSON_IsString(pass)) {
            strncpy(g_config.networks[0].ssid, ssid->valuestring, sizeof(g_config.networks[0].ssid) - 1);
            strncpy(g_config.networks[0].password, pass->valuestring, sizeof(g_config.networks[0].password) - 1);
            g_config.network_count = 1;
        }
    }

    cJSON *ap_pass = cJSON_GetObjectItem(root, "ap_password");
    if (ap_pass) strncpy(g_config.ap_password, ap_pass->valuestring, sizeof(g_config.ap_password) - 1);

    cJSON *mint = cJSON_GetObjectItem(root, "mint_url");
    if (mint) strncpy(g_config.mint_url, mint->valuestring, sizeof(g_config.mint_url) - 1);

    cJSON *acc_mints = cJSON_GetObjectItem(root, "accepted_mints");
    if (acc_mints && cJSON_IsArray(acc_mints)) {
        int mcount = cJSON_GetArraySize(acc_mints);
        if (mcount > TOLLGATE_MAX_MINT_URLS) mcount = TOLLGATE_MAX_MINT_URLS;
        for (int i = 0; i < mcount; i++) {
            cJSON *m = cJSON_GetArrayItem(acc_mints, i);
            if (m && cJSON_IsString(m)) {
                strncpy(g_config.accepted_mints[i], m->valuestring,
                        sizeof(g_config.accepted_mints[i]) - 1);
                g_config.accepted_mint_count++;
            }
        }
    }

    cJSON *lnurl = cJSON_GetObjectItem(root, "lnurl_url");
    if (lnurl) strncpy(g_config.lnurl_url, lnurl->valuestring, sizeof(g_config.lnurl_url) - 1);

    cJSON *price = cJSON_GetObjectItem(root, "price_per_step");
    if (price) g_config.price_per_step = price->valueint;

    cJSON *step = cJSON_GetObjectItem(root, "step_size_ms");
    if (step) g_config.step_size_ms = step->valueint;

    cJSON *step_bytes = cJSON_GetObjectItem(root, "step_size_bytes");
    if (step_bytes) g_config.step_size_bytes = step_bytes->valueint;

    cJSON *metric = cJSON_GetObjectItem(root, "metric");
    if (metric && cJSON_IsString(metric)) {
        strncpy(g_config.metric, metric->valuestring, sizeof(g_config.metric) - 1);
    }

    cJSON *persist = cJSON_GetObjectItem(root, "persist_threshold_sats");
    if (persist) g_config.persist_threshold_sats = (uint64_t)persist->valuedouble;

    cJSON *geohash = cJSON_GetObjectItem(root, "nostr_geohash");
    if (geohash) strncpy(g_config.nostr_geohash, geohash->valuestring, sizeof(g_config.nostr_geohash) - 1);
    else strncpy(g_config.nostr_geohash, "u281w0dfz", sizeof(g_config.nostr_geohash) - 1);

    cJSON *relays = cJSON_GetObjectItem(root, "nostr_relays");
    if (relays && cJSON_IsArray(relays)) {
        int rcount = cJSON_GetArraySize(relays);
        if (rcount > TOLLGATE_MAX_RELAYS) rcount = TOLLGATE_MAX_RELAYS;
        for (int i = 0; i < rcount; i++) {
            cJSON *r = cJSON_GetArrayItem(relays, i);
            if (r && cJSON_IsString(r)) {
                strncpy(g_config.nostr_relays[i], r->valuestring, sizeof(g_config.nostr_relays[i]) - 1);
                g_config.nostr_relay_count++;
            }
        }
    }

    cJSON *pub_interval = cJSON_GetObjectItem(root, "nostr_publish_interval_s");
    if (pub_interval) g_config.nostr_publish_interval_s = pub_interval->valueint;

    cJSON *sync_interval = cJSON_GetObjectItem(root, "nostr_sync_interval_s");
    if (sync_interval) g_config.nostr_sync_interval_s = sync_interval->valueint;

    cJSON *fallback_interval = cJSON_GetObjectItem(root, "nostr_fallback_sync_interval_s");
    if (fallback_interval) g_config.nostr_fallback_sync_interval_s = fallback_interval->valueint;

    cJSON *seed_relays = cJSON_GetObjectItem(root, "nostr_seed_relays");
    if (seed_relays && cJSON_IsArray(seed_relays)) {
        int srcount = cJSON_GetArraySize(seed_relays);
        if (srcount > TOLLGATE_MAX_SEED_RELAYS) srcount = TOLLGATE_MAX_SEED_RELAYS;
        for (int i = 0; i < srcount; i++) {
            cJSON *r = cJSON_GetArrayItem(seed_relays, i);
            if (r && cJSON_IsString(r)) {
                strncpy(g_config.nostr_seed_relays[i], r->valuestring,
                        sizeof(g_config.nostr_seed_relays[i]) - 1);
                g_config.nostr_seed_relay_count++;
            }
        }
    }

    cJSON *client_enabled = cJSON_GetObjectItem(root, "client_enabled");
    if (client_enabled && cJSON_IsBool(client_enabled)) g_config.client_enabled = cJSON_IsTrue(client_enabled);

    cJSON *client_steps = cJSON_GetObjectItem(root, "client_steps_to_buy");
    if (client_steps) g_config.client_steps_to_buy = client_steps->valueint;

    cJSON *client_renewal = cJSON_GetObjectItem(root, "client_renewal_threshold_pct");
    if (client_renewal) g_config.client_renewal_threshold_pct = client_renewal->valueint;

    cJSON *client_retry = cJSON_GetObjectItem(root, "client_retry_interval_ms");
    if (client_retry) g_config.client_retry_interval_ms = client_retry->valueint;

    cJSON *payout = cJSON_GetObjectItem(root, "payout");
    if (payout && cJSON_IsObject(payout)) {
        cJSON *p_en = cJSON_GetObjectItem(payout, "enabled");
        if (p_en && cJSON_IsBool(p_en)) g_config.payout.enabled = cJSON_IsTrue(p_en);

        cJSON *p_fee = cJSON_GetObjectItem(payout, "fee_tolerance_pct");
        if (p_fee) g_config.payout.fee_tolerance_pct = (uint64_t)p_fee->valuedouble;

        cJSON *p_interval = cJSON_GetObjectItem(payout, "check_interval_s");
        if (p_interval) g_config.payout.check_interval_s = p_interval->valueint;

        cJSON *recipients = cJSON_GetObjectItem(payout, "recipients");
        if (recipients && cJSON_IsArray(recipients)) {
            int rcount = cJSON_GetArraySize(recipients);
            if (rcount > PAYOUT_MAX_RECIPIENTS) rcount = PAYOUT_MAX_RECIPIENTS;
            for (int i = 0; i < rcount; i++) {
                cJSON *r = cJSON_GetArrayItem(recipients, i);
                cJSON *addr = cJSON_GetObjectItem(r, "lightning_address");
                cJSON *factor = cJSON_GetObjectItem(r, "factor");
                if (addr && cJSON_IsString(addr)) {
                    strncpy(g_config.payout.recipients[i].lightning_address, addr->valuestring,
                            sizeof(g_config.payout.recipients[i].lightning_address) - 1);
                }
                if (factor && cJSON_IsNumber(factor)) {
                    g_config.payout.recipients[i].factor = factor->valuedouble;
                }
            }
            g_config.payout.recipient_count = rcount;
        }

        cJSON *mints = cJSON_GetObjectItem(payout, "mints");
        if (mints && cJSON_IsArray(mints)) {
            int mcount = cJSON_GetArraySize(mints);
            if (mcount > PAYOUT_MAX_MINTS) mcount = PAYOUT_MAX_MINTS;
            for (int i = 0; i < mcount; i++) {
                cJSON *m = cJSON_GetArrayItem(mints, i);
                cJSON *murl = cJSON_GetObjectItem(m, "url");
                cJSON *mbal = cJSON_GetObjectItem(m, "min_balance");
                cJSON *mpay = cJSON_GetObjectItem(m, "min_payout_amount");
                if (murl && cJSON_IsString(murl)) {
                    strncpy(g_config.payout.mints[i].url, murl->valuestring,
                            sizeof(g_config.payout.mints[i].url) - 1);
                }
                if (mbal && cJSON_IsNumber(mbal)) {
                    g_config.payout.mints[i].min_balance = (uint64_t)mbal->valuedouble;
                }
                if (mpay && cJSON_IsNumber(mpay)) {
                    g_config.payout.mints[i].min_payout_amount = (uint64_t)mpay->valuedouble;
                }
            }
            g_config.payout.mint_count = mcount;
        }
    }

    cJSON *cvm = cJSON_GetObjectItem(root, "cvm");
    if (cvm && cJSON_IsObject(cvm)) {
        cJSON *cvm_en = cJSON_GetObjectItem(cvm, "enabled");
        if (cvm_en && cJSON_IsBool(cvm_en)) g_config.cvm_enabled = cJSON_IsTrue(cvm_en);
        cJSON *cvm_relays = cJSON_GetObjectItem(cvm, "relays");
        if (cvm_relays && cJSON_IsString(cvm_relays)) {
            strncpy(g_config.cvm_relays, cvm_relays->valuestring, sizeof(g_config.cvm_relays) - 1);
        }
    }
    cJSON *cvm_en_top = cJSON_GetObjectItem(root, "cvm_enabled");
    if (cvm_en_top && cJSON_IsBool(cvm_en_top)) g_config.cvm_enabled = cJSON_IsTrue(cvm_en_top);

    cJSON *auth_mode = cJSON_GetObjectItem(root, "wifi_auth_mode");
    if (auth_mode && cJSON_IsString(auth_mode)) {
        strncpy(g_config.wifi_auth_mode, auth_mode->valuestring, sizeof(g_config.wifi_auth_mode) - 1);
    }

    cJSON *disp_en = cJSON_GetObjectItem(root, "display_enabled");
    if (disp_en && cJSON_IsBool(disp_en)) g_config.display_enabled = cJSON_IsTrue(disp_en);

    cJSON *gps = cJSON_GetObjectItem(root, "gps");
    if (gps && cJSON_IsObject(gps)) {
        cJSON *g_en = cJSON_GetObjectItem(gps, "enabled");
        if (g_en && cJSON_IsBool(g_en)) g_config.gps_enabled = cJSON_IsTrue(g_en);
        cJSON *g_uart = cJSON_GetObjectItem(gps, "uart_num");
        if (g_uart && cJSON_IsNumber(g_uart)) g_config.gps_uart_num = g_uart->valueint;
        cJSON *g_tx = cJSON_GetObjectItem(gps, "tx_pin");
        if (g_tx && cJSON_IsNumber(g_tx)) g_config.gps_tx_pin = g_tx->valueint;
        cJSON *g_rx = cJSON_GetObjectItem(gps, "rx_pin");
        if (g_rx && cJSON_IsNumber(g_rx)) g_config.gps_rx_pin = g_rx->valueint;
        cJSON *g_baud = cJSON_GetObjectItem(gps, "baud");
        if (g_baud && cJSON_IsNumber(g_baud)) g_config.gps_baud = g_baud->valueint;
    }
    cJSON *gps_en_top = cJSON_GetObjectItem(root, "gps_enabled");
    if (gps_en_top && cJSON_IsBool(gps_en_top)) g_config.gps_enabled = cJSON_IsTrue(gps_en_top);

    if (g_config.payout.mint_count == 0 && g_config.mint_url[0] != '\0') {
        strncpy(g_config.payout.mints[0].url, g_config.mint_url,
                sizeof(g_config.payout.mints[0].url) - 1);
        g_config.payout.mints[0].min_balance = 64;
        g_config.payout.mints[0].min_payout_amount = 128;
        g_config.payout.mint_count = 1;
    }

    cJSON *mining = cJSON_GetObjectItem(root, "mining");
    if (mining && cJSON_IsObject(mining)) {
        cJSON *m_en = cJSON_GetObjectItem(mining, "enabled");
        if (m_en && cJSON_IsBool(m_en)) g_config.mining_enabled = cJSON_IsTrue(m_en);

        cJSON *m_mode = cJSON_GetObjectItem(mining, "payout_mode");
        if (m_mode && cJSON_IsString(m_mode)) {
            if (strcmp(m_mode->valuestring, "pool") == 0) g_config.mining_payout_mode = MINING_PAYOUT_POOL;
            else if (strcmp(m_mode->valuestring, "upstream") == 0) g_config.mining_payout_mode = MINING_PAYOUT_UPSTREAM;
            else if (strcmp(m_mode->valuestring, "proxy_only") == 0) g_config.mining_payout_mode = MINING_PAYOUT_PROXY_ONLY;
        }

        cJSON *m_host = cJSON_GetObjectItem(mining, "stratum_host");
        if (m_host && cJSON_IsString(m_host)) strncpy(g_config.stratum_host, m_host->valuestring, sizeof(g_config.stratum_host) - 1);

        cJSON *m_port = cJSON_GetObjectItem(mining, "stratum_port");
        if (m_port) g_config.stratum_port = (uint16_t)m_port->valueint;

        cJSON *m_user = cJSON_GetObjectItem(mining, "stratum_user");
        if (m_user && cJSON_IsString(m_user)) strncpy(g_config.stratum_user, m_user->valuestring, sizeof(g_config.stratum_user) - 1);

        cJSON *m_pass = cJSON_GetObjectItem(mining, "stratum_pass");
        if (m_pass && cJSON_IsString(m_pass)) strncpy(g_config.stratum_pass, m_pass->valuestring, sizeof(g_config.stratum_pass) - 1);

        cJSON *m_fb_host = cJSON_GetObjectItem(mining, "stratum_fallback_host");
        if (m_fb_host && cJSON_IsString(m_fb_host)) strncpy(g_config.stratum_fallback_host, m_fb_host->valuestring, sizeof(g_config.stratum_fallback_host) - 1);

        cJSON *m_fb_port = cJSON_GetObjectItem(mining, "stratum_fallback_port");
        if (m_fb_port) g_config.stratum_fallback_port = (uint16_t)m_fb_port->valueint;

        cJSON *m_mport = cJSON_GetObjectItem(mining, "mining_port");
        if (m_mport) g_config.mining_port = (uint16_t)m_mport->valueint;

        cJSON *m_hp = cJSON_GetObjectItem(mining, "hashprice_sats_per_ghs_day");
        if (m_hp) g_config.hashprice_sats_per_ghs_day = (uint64_t)m_hp->valuedouble;

        cJSON *m_sandbox = cJSON_GetObjectItem(mining, "sandbox_mint_access");
        if (m_sandbox && cJSON_IsBool(m_sandbox)) g_config.mining_sandbox_mint_access = cJSON_IsTrue(m_sandbox);
        cJSON *m_selftest = cJSON_GetObjectItem(mining, "proxy_self_test");
        if (m_selftest && cJSON_IsBool(m_selftest)) g_config.proxy_self_test = cJSON_IsTrue(m_selftest);

        cJSON *m_faucet = cJSON_GetObjectItem(mining, "faucet_url");
        if (m_faucet && cJSON_IsString(m_faucet)) strncpy(g_config.faucet_url, m_faucet->valuestring, sizeof(g_config.faucet_url) - 1);

        cJSON *m_fi = cJSON_GetObjectItem(mining, "faucet_poll_interval_s");
        if (m_fi) g_config.faucet_poll_interval_s = m_fi->valueint;
    }

    cJSON *market_enabled = cJSON_GetObjectItem(root, "market_enabled");
    if (market_enabled && cJSON_IsBool(market_enabled)) g_config.market_enabled = cJSON_IsTrue(market_enabled);

    cJSON *market_scan_interval = cJSON_GetObjectItem(root, "market_scan_interval_s");
    if (market_scan_interval) g_config.market_scan_interval_s = market_scan_interval->valueint;

    cJSON *client_auto_switch = cJSON_GetObjectItem(root, "client_auto_switch");
    if (client_auto_switch && cJSON_IsBool(client_auto_switch)) g_config.client_auto_switch = cJSON_IsTrue(client_auto_switch);

    cJSON *sync_en = cJSON_GetObjectItem(root, "sync_enabled");
    if (sync_en && cJSON_IsBool(sync_en)) g_config.sync_enabled = cJSON_IsTrue(sync_en);

    cJSON *wifistr_en = cJSON_GetObjectItem(root, "wifistr_enabled");
    if (wifistr_en && cJSON_IsBool(wifistr_en)) g_config.wifistr_enabled = cJSON_IsTrue(wifistr_en);

    cJSON *relay_en = cJSON_GetObjectItem(root, "local_relay_enabled");
    if (relay_en && cJSON_IsBool(relay_en)) g_config.local_relay_enabled = cJSON_IsTrue(relay_en);

    cJSON *mh_en = cJSON_GetObjectItem(root, "mint_health_enabled");
    if (mh_en && cJSON_IsBool(mh_en)) g_config.mint_health_enabled = cJSON_IsTrue(mh_en);

    cJSON_Delete(root);

    if (g_config.payout.recipient_count == 0) {
        strncpy(g_config.payout.recipients[0].lightning_address, "TollGate@coinos.io",
                sizeof(g_config.payout.recipients[0].lightning_address) - 1);
        g_config.payout.recipients[0].factor = 1.0;
        g_config.payout.recipient_count = 1;
    }

    if (g_config.accepted_mint_count == 0 && g_config.mint_url[0] != '\0') {
        strncpy(g_config.accepted_mints[0], g_config.mint_url,
                sizeof(g_config.accepted_mints[0]) - 1);
        g_config.accepted_mint_count = 1;
    }

    if (g_config.nostr_relay_count == 0) {
        strncpy(g_config.nostr_relays[0], "wss://relay.damus.io", sizeof(g_config.nostr_relays[0]) - 1);
        strncpy(g_config.nostr_relays[1], "wss://nos.lol", sizeof(g_config.nostr_relays[1]) - 1);
        strncpy(g_config.nostr_relays[2], "wss://relay.anzenkodo.workers.dev",
                sizeof(g_config.nostr_relays[2]) - 1);
        strncpy(g_config.nostr_relays[3], "wss://nostr.koning-degraaf.nl",
                sizeof(g_config.nostr_relays[3]) - 1);
        g_config.nostr_relay_count = 4;
    }

    if (g_config.nostr_seed_relay_count == 0) {
        strncpy(g_config.nostr_seed_relays[0], "wss://relay.orangesync.tech",
                sizeof(g_config.nostr_seed_relays[0]) - 1);
        strncpy(g_config.nostr_seed_relays[1], "wss://relay.damus.io",
                sizeof(g_config.nostr_seed_relays[1]) - 1);
        strncpy(g_config.nostr_seed_relays[2], "wss://nos.lol",
                sizeof(g_config.nostr_seed_relays[2]) - 1);
        strncpy(g_config.nostr_seed_relays[3], "wss://relay.nostr.band",
                sizeof(g_config.nostr_seed_relays[3]) - 1);
        strncpy(g_config.nostr_seed_relays[4], "wss://relay.anzenkodo.workers.dev",
                sizeof(g_config.nostr_seed_relays[4]) - 1);
        strncpy(g_config.nostr_seed_relays[5], "wss://nostr.koning-degraaf.nl",
                sizeof(g_config.nostr_seed_relays[5]) - 1);
        strncpy(g_config.nostr_seed_relays[6], "wss://knostr.neutrine.com",
                sizeof(g_config.nostr_seed_relays[6]) - 1);
        strncpy(g_config.nostr_seed_relays[7], "wss://nostr.einundzwanzig.space",
                sizeof(g_config.nostr_seed_relays[7]) - 1);
        g_config.nostr_seed_relay_count = 8;
    }

    ESP_LOGI(TAG, "Config loaded: nsec=%s...%s, %d WiFi networks, %d accepted mints, price=%d sats/%dms",
             g_config.nsec, g_config.nsec + 60, g_config.network_count,
             g_config.accepted_mint_count, g_config.price_per_step, g_config.step_size_ms);
    return ESP_OK;
}

const tollgate_config_t *tollgate_config_get(void)
{
    return &g_config;
}

esp_err_t tollgate_config_get_wifi(wifi_config_t *wifi_config)
{
    if (g_config.network_count == 0) return ESP_ERR_NOT_FOUND;
    int idx = g_config.current_network % g_config.network_count;
    memset(wifi_config, 0, sizeof(wifi_config_t));
    strncpy((char *)wifi_config->sta.ssid, g_config.networks[idx].ssid, sizeof(wifi_config->sta.ssid) - 1);
    strncpy((char *)wifi_config->sta.password, g_config.networks[idx].password, sizeof(wifi_config->sta.password) - 1);
    wifi_config->sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    if (g_config.networks[idx].password[0] == '\0') {
        wifi_config->sta.threshold.authmode = WIFI_AUTH_OPEN;
    } else if (strstr(g_config.wifi_auth_mode, "WPA3")) {
        wifi_config->sta.threshold.authmode = WIFI_AUTH_WPA3_PSK;
    } else {
        wifi_config->sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    }
    ESP_LOGI(TAG, "STA auth threshold: %s -> %d", g_config.wifi_auth_mode, wifi_config->sta.threshold.authmode);
    return ESP_OK;
}

esp_err_t tollgate_config_get_next_wifi(wifi_config_t *wifi_config)
{
    if (g_config.network_count == 0) return ESP_ERR_NOT_FOUND;
    g_config.current_network = (g_config.current_network + 1) % g_config.network_count;
    return tollgate_config_get_wifi(wifi_config);
}

void tollgate_config_derive_unique(tollgate_config_t *cfg)
{
    if (cfg->identity_initialized) return;

    const tollgate_identity_t *id = identity_get();
    if (!id || !id->initialized) {
        ESP_LOGE(TAG, "Cannot derive unique config: identity not initialized");
        return;
    }

    strncpy(cfg->ap_ssid, id->ap_ssid, sizeof(cfg->ap_ssid) - 1);
    memcpy(cfg->sta_mac, id->sta_mac, 6);
    memcpy(cfg->ap_mac, id->ap_mac, 6);
    cfg->ap_ip = id->ap_ip;
    strncpy(cfg->ap_ip_str, id->ap_ip_str, sizeof(cfg->ap_ip_str) - 1);
    strncpy(cfg->npub, id->npub_hex, sizeof(cfg->npub) - 1);

    cfg->identity_initialized = true;

    ESP_LOGI(TAG, "Unique config derived from nsec: SSID='%s', AP_IP=%s",
             cfg->ap_ssid, cfg->ap_ip_str);
}

esp_err_t tollgate_config_add_wifi(const char *ssid, const char *password) {
    if (!ssid || !password) return ESP_ERR_INVALID_ARG;
    if (g_config.network_count >= TOLLGATE_MAX_WIFI_NETWORKS) return ESP_ERR_NO_MEM;

    strncpy(g_config.networks[g_config.network_count].ssid, ssid,
            sizeof(g_config.networks[g_config.network_count].ssid) - 1);
    strncpy(g_config.networks[g_config.network_count].password, password,
            sizeof(g_config.networks[g_config.network_count].password) - 1);
    g_config.network_count++;

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "nsec", g_config.nsec);

    cJSON *networks = cJSON_CreateArray();
    for (int i = 0; i < g_config.network_count; i++) {
        cJSON *net = cJSON_CreateObject();
        cJSON_AddStringToObject(net, "ssid", g_config.networks[i].ssid);
        cJSON_AddStringToObject(net, "password", g_config.networks[i].password);
        cJSON_AddItemToArray(networks, net);
    }
    cJSON_AddItemToObject(root, "wifi_networks", networks);

    if (g_config.ap_password[0])
        cJSON_AddStringToObject(root, "ap_password", g_config.ap_password);
    cJSON_AddStringToObject(root, "mint_url", g_config.mint_url);
    cJSON_AddNumberToObject(root, "price_per_step", g_config.price_per_step);
    cJSON_AddNumberToObject(root, "step_size_ms", g_config.step_size_ms);

    if (g_config.metric[0])
        cJSON_AddStringToObject(root, "metric", g_config.metric);
    if (g_config.nostr_geohash[0])
        cJSON_AddStringToObject(root, "nostr_geohash", g_config.nostr_geohash);

    cJSON *relays = cJSON_CreateArray();
    for (int i = 0; i < g_config.nostr_relay_count; i++) {
        cJSON_AddItemToArray(relays, cJSON_CreateString(g_config.nostr_relays[i]));
    }
    cJSON_AddItemToObject(root, "nostr_relays", relays);

    FILE *f = fopen("/spiffs/config.json", "w");
    if (f) {
        char *json = cJSON_PrintUnformatted(root);
        fputs(json, f);
        free(json);
        fclose(f);
    }
    cJSON_Delete(root);

    ESP_LOGI(TAG, "WiFi network added: %s (total: %d)", ssid, g_config.network_count);
    return ESP_OK;
}
