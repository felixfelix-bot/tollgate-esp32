# Balloon TollGate — ESP32-C3 source list
# Re-enabled: local_relay (operator decision: Nostr relay priority)
# Stripped: display, font, cvm_server, mcp_handler, wifistr, tollgate_client,
#           beacon_price, market, negentropy_adapter, stratum_*, sw_miner,
#           asic_miner, faucet_client, remote_miner, touch, keyboard, wifi_setup,
#           mint_health, tls_worker, lightning_payout, lnurl_pay, nip04,
#           relay_selector, sync_manager
# Kept: tollgate_main, config, dns_server, captive_portal, tollgate_api,
#       identity, nostr_event, geohash, local_relay
idf_component_register(SRCS "tollgate_main.c"
                            "config.c"
                            "dns_server.c"
                            "captive_portal.c"
                            "tollgate_api.c"
                            "identity.c"
                            "nostr_event.c"
                            "geohash.c"
                            "local_relay.c"
                    INCLUDE_DIRS "."
                     REQUIRES esp_wifi esp_event esp_netif nvs_flash esp_http_server
                               lwip json esp_http_client mbedtls esp-tls log spiffs
                               nucula_lib secp256k1 qrcode tollgate_esp wisp_relay esp_littlefs
                      PRIV_REQUIRES esp-tls)
