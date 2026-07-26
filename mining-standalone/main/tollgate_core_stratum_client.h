#ifndef TOLLGATE_CORE_STRATUM_CLIENT_H
#define TOLLGATE_CORE_STRATUM_CLIENT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define TG_STRATUM_MAX_JOB_ID_LEN 32
#define TG_STRATUM_MAX_TOKEN_LEN 2048

typedef struct {
    uint32_t job_id;
    uint8_t prevhash[32];
    uint8_t merkle_root[32];
    uint32_t ntime;
    uint32_t nbits;
    uint32_t version;
    uint8_t target[32];
    int target_len;
    bool valid;
} tollgate_stratum_job_t;

typedef struct {
    bool connected;
    char pool_host[128];
    uint16_t pool_port;
    uint32_t nbits;
    uint64_t difficulty;
    uint64_t shares_accepted;
    uint64_t shares_rejected;
} tollgate_stratum_client_state_t;

typedef void (*tollgate_stratum_token_cb)(const char *token);

void tollgate_core_stratum_hex_to_bytes(const char *hex, uint8_t *out, int len);

bool tollgate_core_stratum_parse_notify(const void *params_json,
                                         tollgate_stratum_job_t *job,
                                         uint32_t *out_nbits);

bool tollgate_core_stratum_parse_difficulty(const void *params_json,
                                             uint64_t *difficulty_out);

bool tollgate_core_stratum_parse_token(const void *params_json,
                                        char *token_out, size_t token_out_size);

int tollgate_core_stratum_build_subscribe(char *buf, size_t buf_size, uint32_t req_id);

int tollgate_core_stratum_build_authorize(char *buf, size_t buf_size, uint32_t req_id,
                                            const char *user, const char *pass);

int tollgate_core_stratum_build_submit(char *buf, size_t buf_size, uint32_t req_id,
                                        const char *user, uint32_t job_id,
                                        const char *extranonce2_hex,
                                        uint32_t ntime, uint32_t nonce);

#endif
