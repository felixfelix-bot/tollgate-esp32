/*
 * mining_stubs.c — Stub implementations of tollgate_core_mining functions
 * referenced by sw_miner.c and stratum_client.c.
 *
 * In the tollgate firmware, tollgate_core_mining.c manages share windows,
 * hashprice calculation, client stats, and payment allotments.  For
 * standalone mining we stub the two functions that are actually called:
 *   - tollgate_core_mining_update_hashrate(ip, accepted)
 *   - tollgate_core_mining_set_current_nbits(nbits)
 *
 * The full header (tollgate_core_mining.h) is vendored verbatim so the
 * original source files compile unchanged.  Other functions declared in
 * that header are simply not linked (no references from our code).
 */
#include "tollgate_core_mining.h"
#include "esp_log.h"

static const char *TAG = "mine_stub";

void tollgate_core_mining_update_hashrate(uint32_t client_ip, bool accepted)
{
    ESP_LOGD(TAG, "update_hashrate stub: ip=0x%08lx accepted=%d",
             (unsigned long)client_ip, accepted);
}

void tollgate_core_mining_set_current_nbits(uint32_t nbits)
{
    ESP_LOGD(TAG, "set_current_nbits stub: nbits=0x%08lx", (unsigned long)nbits);
}
