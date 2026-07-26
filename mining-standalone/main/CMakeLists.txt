# Phase 1 standalone mining component
# Core: sw_miner.c + stratum_client.c (verbatim from esp32-tollgate)
# Stubs: config, identity, tls_worker, tollgate_core_mining, stratum_proxy
idf_component_register(
    SRCS
        "mining_main.c"
        "sw_miner.c"
        "stratum_client.c"
        "stratum_proxy_stub.c"
        "tollgate_core_stratum_client.c"
        "mining_config.c"
        "identity_stub.c"
        "tls_worker_stub.c"
        "mining_stubs.c"
    INCLUDE_DIRS "."
    REQUIRES
        json
        mbedtls
        tcp_transport
        esp_event
        esp_netif
        nvs_flash
        esp_wifi
        log
)
