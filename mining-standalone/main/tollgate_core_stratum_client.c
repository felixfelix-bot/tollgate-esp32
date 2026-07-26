#include "tollgate_core_stratum_client.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

void tollgate_core_stratum_hex_to_bytes(const char *hex, uint8_t *out, int len)
{
    for (int i = 0; i < len && hex[i * 2] && hex[i * 2 + 1]; i++) {
        char byte[3] = {hex[i * 2], hex[i * 2 + 1], 0};
        out[i] = (uint8_t)strtoul(byte, NULL, 16);
    }
}

bool tollgate_core_stratum_parse_notify(const void *params_json,
                                         tollgate_stratum_job_t *job,
                                         uint32_t *out_nbits)
{
    const cJSON *params = (const cJSON *)params_json;
    if (!params || !cJSON_IsArray(params) || cJSON_GetArraySize(params) < 6) return false;

    const cJSON *p_job_id = cJSON_GetArrayItem(params, 0);
    const cJSON *p_prevhash = cJSON_GetArrayItem(params, 1);
    const cJSON *p_version = cJSON_GetArrayItem(params, 5);
    const cJSON *p_nbits = cJSON_GetArrayItem(params, 6);
    const cJSON *p_ntime = cJSON_GetArrayItem(params, 7);

    if (!p_job_id || !p_prevhash || !p_nbits) return false;

    memset(job, 0, sizeof(*job));
    job->job_id = (uint32_t)atoi(p_job_id->valuestring);
    job->valid = true;

    tollgate_core_stratum_hex_to_bytes(p_prevhash->valuestring, job->prevhash, 32);

    if (p_version && cJSON_IsString(p_version)) {
        job->version = (uint32_t)strtoul(p_version->valuestring, NULL, 16);
    }
    if (p_nbits && cJSON_IsString(p_nbits)) {
        job->nbits = (uint32_t)strtoul(p_nbits->valuestring, NULL, 16);
        if (out_nbits) *out_nbits = job->nbits;
    }
    if (p_ntime && cJSON_IsString(p_ntime)) {
        job->ntime = (uint32_t)strtoul(p_ntime->valuestring, NULL, 16);
    }

    memset(job->target, 0xFF, 32);
    job->target_len = 32;

    return true;
}

bool tollgate_core_stratum_parse_difficulty(const void *params_json,
                                             uint64_t *difficulty_out)
{
    const cJSON *params = (const cJSON *)params_json;
    if (!params || !cJSON_IsArray(params) || cJSON_GetArraySize(params) < 1) return false;

    const cJSON *diff = cJSON_GetArrayItem(params, 0);
    if (diff && cJSON_IsNumber(diff)) {
        *difficulty_out = (uint64_t)diff->valuedouble;
        return true;
    }
    return false;
}

bool tollgate_core_stratum_parse_token(const void *params_json,
                                        char *token_out, size_t token_out_size)
{
    const cJSON *params = (const cJSON *)params_json;
    if (!params || !cJSON_IsArray(params) || cJSON_GetArraySize(params) < 1) return false;

    const cJSON *token_item = cJSON_GetArrayItem(params, 0);
    if (!token_item || !cJSON_IsString(token_item)) return false;

    const char *token = token_item->valuestring;
    size_t len = strlen(token);
    if (len == 0 || len >= token_out_size) return false;

    memcpy(token_out, token, len + 1);
    return true;
}

int tollgate_core_stratum_build_subscribe(char *buf, size_t buf_size, uint32_t req_id)
{
    return snprintf(buf, buf_size,
                    "{\"id\":%lu,\"method\":\"mining.subscribe\",\"params\":[\"TollGate/1.0\"]}\n",
                    (unsigned long)req_id);
}

int tollgate_core_stratum_build_authorize(char *buf, size_t buf_size, uint32_t req_id,
                                            const char *user, const char *pass)
{
    return snprintf(buf, buf_size,
                    "{\"id\":%lu,\"method\":\"mining.authorize\",\"params\":[\"%s\",\"%s\"]}\n",
                    (unsigned long)req_id, user, pass);
}

int tollgate_core_stratum_build_submit(char *buf, size_t buf_size, uint32_t req_id,
                                        const char *user, uint32_t job_id,
                                        const char *extranonce2_hex,
                                        uint32_t ntime, uint32_t nonce)
{
    return snprintf(buf, buf_size,
                    "{\"id\":%lu,\"method\":\"mining.submit\",\"params\":[\"%s\",\"%lu\",\"%s\",\"%08lx\",\"%08lx\"]}\n",
                    (unsigned long)req_id, user,
                    (unsigned long)job_id, extranonce2_hex,
                    (unsigned long)ntime, (unsigned long)nonce);
}
