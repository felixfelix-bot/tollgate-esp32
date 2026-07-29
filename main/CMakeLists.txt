# Balloon TollGate — ESP32-C3 source list (balloon-only strip)
# Tag v-balloon-pre-strip preserves full tollgate before this strip.
# Display/mining/marketplace stripped from balloon build ONLY.
# Original tollgate repo retains all components for other contexts.
#
# Kept (11 files — balloon-relevant):
#   tollgate_main  — boot + init
#   config         — config.json, nsec loading
#   dns_server     — captive portal DNS hijack
#   captive_portal — WiFi AP + Cashu payment portal HTML
#   tollgate_api   — HTTP endpoints (payment, wallet, mints, usage)
#   identity       — nsec → MAC/SSID/IP derivation
#   nostr_event    — Nostr event signing
#   geohash        — location encoding (balloon position)
#   mint_health    — mint status checking (dependency of tollgate_api)
#   local_relay    — local Nostr relay (wisp-esp32, port 4869)
#
# Dropped (23 files — balloon-irrelevant, preserved in repo):
#   display, font, touch, keyboard, asic_miner, sw_miner, stratum_client,
#   stratum_proxy, remote_miner, faucet_client, market, beacon_price,
#   cvm_server, mcp_handler, tollgate_client, wifistr, negentropy_adapter,
#   relay_selector, sync_manager, lightning_payout, lnurl_pay, wifi_setup,
#   nip04, tls_worker
idf_component_register(SRCS "tollgate_main.c"
                            "config.c"
                            "dns_server.c"
                            "captive_portal.c"
                            "tollgate_api.c"
                            "identity.c"
                            "nostr_event.c"
                            "geohash.c"
                            "mint_health.c"
                            "local_relay.c"
                    INCLUDE_DIRS "."
                     REQUIRES esp_wifi esp_event esp_netif nvs_flash esp_http_server
                               lwip json esp_http_client mbedtls esp-tls log spiffs
                               nucula_lib secp256k1 qrcode tollgate_esp wisp_relay esp_littlefs
                      PRIV_REQUIRES esp-tls)
