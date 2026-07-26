/*
 * config.h — Minimal stub for standalone mining build.
 *
 * In the full tollgate firmware, tollgate_config_t has ~30 fields covering
 * WiFi, Nostr, wallet, mining, display, etc.  For standalone SW mining we
 * only need the stratum-related fields that stratum_client.c accesses.
 *
 * This header shadows the tollgate's config.h so the verbatim stratum_client.c
 * compiles without modification.
 */
#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    char stratum_host[128];
    uint16_t stratum_port;
    char stratum_user[128];
    char stratum_pass[64];
} tollgate_config_t;

const tollgate_config_t *tollgate_config_get(void);

#endif /* CONFIG_H */
