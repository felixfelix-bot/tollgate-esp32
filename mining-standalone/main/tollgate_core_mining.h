#ifndef TOLLGATE_CORE_MINING_H
#define TOLLGATE_CORE_MINING_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MINING_SHARE_WINDOW_S 30
#define MINING_BLOCK_SUBSIDY_SATS 312500000ULL
#define MINING_BLOCKS_PER_DAY 144ULL
#define MINING_MAX_CLIENTS 10

typedef struct {
    uint32_t ip;
    uint64_t shares_accepted;
    uint64_t shares_rejected;
    int64_t first_share_time_ms;
    int64_t last_share_time_ms;
    double hashrate_ghs;
} tollgate_mining_client_stats_t;

uint64_t tollgate_core_mining_nbits_to_difficulty(uint32_t nbits);
double tollgate_core_mining_calc_hashprice(uint32_t nbits);
double tollgate_core_mining_calc_hashprice_override(uint64_t sats_per_ghs_day);
esp_err_t tollgate_core_mining_validate_share(const uint8_t *header80, uint32_t nonce, const uint8_t *target, int target_len);
uint64_t tollgate_core_mining_shares_to_allotment_ms(double hashrate_ghs, double hashprice, int price, int step_ms);
uint64_t tollgate_core_mining_shares_to_allotment_bytes(double hashrate_ghs, double hashprice, int price, int step_bytes);
tollgate_mining_client_stats_t *tollgate_core_mining_get_or_create_client(uint32_t client_ip);
void tollgate_core_mining_update_hashrate(uint32_t client_ip, bool accepted);
const tollgate_mining_client_stats_t *tollgate_core_mining_get_client_stats(uint32_t client_ip);
double tollgate_core_mining_get_current_hashprice(void);
void tollgate_core_mining_set_current_nbits(uint32_t nbits);
void tollgate_core_mining_init(void);

#ifdef __cplusplus
}
#endif

#endif
