#ifndef STRATUM_CLIENT_H
#define STRATUM_CLIENT_H

#include "esp_err.h"
#include "stratum_proxy.h"
#include "tollgate_core_stratum_client.h"
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    bool connected;
    char pool_host[128];
    uint16_t pool_port;
    uint32_t nbits;
    uint64_t difficulty;
    uint64_t shares_accepted;
    uint64_t shares_rejected;
    bool sv2_active;
    int extranonce2_size;
    uint32_t subscribe_req_id;
} stratum_client_state_t;

esp_err_t stratum_client_init(void);
esp_err_t stratum_client_start(void);
void stratum_client_stop(void);
esp_err_t stratum_client_submit_share(uint32_t job_id, uint32_t nonce, uint32_t ntime);
const stratum_client_state_t *stratum_client_get_state(void);
void stratum_client_tick(void);
void stratum_client_set_token_callback(tollgate_stratum_token_cb cb);

#endif
