#include "stratum_client.h"
#include "stratum_proxy.h"
#include "tollgate_core_mining.h"
#include "tollgate_core_stratum_client.h"
#include "config.h"
#include "identity.h"
#include "tls_worker.h"
#include "esp_log.h"
#include "esp_transport.h"
#include "esp_transport_tcp.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "stratum_client";
static stratum_client_state_t s_state = {0};
static esp_transport_handle_t s_transport = NULL;
static bool s_running = false;
static uint32_t s_req_id = 1;
static TaskHandle_t s_task_handle = NULL;
static tollgate_stratum_token_cb s_token_cb = NULL;

static int read_line(char *buf, int max_len)
{
    int total = 0;
    while (total < max_len - 1) {
        int r = esp_transport_read(s_transport, buf + total, 1, 5000);
        if (r <= 0) return -1;
        if (buf[total] == '\n') {
            buf[total + 1] = '\0';
            return total + 1;
        }
        total++;
    }
    buf[total] = '\0';
    return total;
}

static esp_err_t stratum_connect(const char *host, uint16_t port)
{
    if (s_transport) {
        esp_transport_close(s_transport);
        esp_transport_destroy(s_transport);
        s_transport = NULL;
    }

    s_transport = esp_transport_tcp_init();
    if (!s_transport) {
        ESP_LOGE(TAG, "Failed to init TCP transport");
        return ESP_FAIL;
    }

    esp_err_t err = esp_transport_connect(s_transport, host, port, 10000);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect to %s:%u", host, (unsigned)port);
        esp_transport_destroy(s_transport);
        s_transport = NULL;
        return ESP_FAIL;
    }

    strncpy(s_state.pool_host, host, sizeof(s_state.pool_host) - 1);
    s_state.pool_port = port;
    s_state.connected = true;
    ESP_LOGI(TAG, "Connected to %s:%u", host, (unsigned)port);
    return ESP_OK;
}

static void send_subscribe(void)
{
    char subscribe[256];
    int len = tollgate_core_stratum_build_subscribe(subscribe, sizeof(subscribe), s_req_id);
    if (len > 0) {
        s_state.subscribe_req_id = s_req_id;
        s_req_id++;
        esp_transport_write(s_transport, subscribe, len, 5000);
        ESP_LOGI(TAG, "Sent mining.subscribe (id=%lu)", (unsigned long)s_state.subscribe_req_id);
    }
}

static void send_authorize(void)
{
    const tollgate_config_t *cfg = tollgate_config_get();
    const tollgate_identity_t *id = identity_get();

    char password[256];
    if (id->initialized && id->locking_pubkey_hex[0] != '\0') {
        snprintf(password, sizeof(password), "%s.%s", cfg->stratum_pass, id->locking_pubkey_hex);
    } else {
        strncpy(password, cfg->stratum_pass, sizeof(password) - 1);
        password[sizeof(password) - 1] = '\0';
    }

    char authorize[512];
    int len = tollgate_core_stratum_build_authorize(authorize, sizeof(authorize),
                                                      s_req_id++, cfg->stratum_user, password);
    if (len > 0) {
        esp_transport_write(s_transport, authorize, len, 5000);
        ESP_LOGI(TAG, "Sent mining.authorize for user=%s (with locking pubkey)", cfg->stratum_user);
    }
}

static void handle_mining_notify(cJSON *params)
{
    tollgate_stratum_job_t job = {0};
    uint32_t nbits = 0;
    if (!tollgate_core_stratum_parse_notify(params, &job, &nbits)) return;

    stratum_job_t proxy_job = {0};
    proxy_job.job_id = job.job_id;
    memcpy(proxy_job.prevhash, job.prevhash, 32);
    proxy_job.version = job.version;
    proxy_job.nbits = job.nbits;
    proxy_job.ntime = job.ntime;
    memcpy(proxy_job.target, job.target, 32);
    proxy_job.target_len = job.target_len;
    proxy_job.valid = job.valid;

    tollgate_core_mining_set_current_nbits(proxy_job.nbits);
    stratum_proxy_set_job(&proxy_job);

    if (nbits) s_state.nbits = nbits;
    ESP_LOGI(TAG, "New mining job: id=%lu, nbits=0x%08lx", (unsigned long)job.job_id, (unsigned long)job.nbits);
}

static void handle_mining_set_difficulty(cJSON *params)
{
    uint64_t diff = 0;
    if (tollgate_core_stratum_parse_difficulty(params, &diff)) {
        s_state.difficulty = diff;
        ESP_LOGI(TAG, "Pool set difficulty: %llu", (unsigned long long)s_state.difficulty);
    }
}

static void handle_mining_token(cJSON *params)
{
    char token[TG_STRATUM_MAX_TOKEN_LEN];
    if (!tollgate_core_stratum_parse_token(params, token, sizeof(token))) {
        ESP_LOGW(TAG, "Failed to parse mining.token params");
        return;
    }

    ESP_LOGI(TAG, "Received mining.token: %s", token);

    if (s_token_cb) {
        s_token_cb(token);
    } else {
        tls_worker_submit(token);
    }
}

static void handle_subscribe_response(cJSON *result, uint32_t req_id)
{
    if (req_id != s_state.subscribe_req_id) return;
    if (!cJSON_IsArray(result)) return;

    int size = cJSON_GetArraySize(result);
    if (size < 3) {
        ESP_LOGW(TAG, "Subscribe response too short: %d params", size);
        return;
    }

    cJSON *en2_size_item = cJSON_GetArrayItem(result, 2);
    if (en2_size_item && cJSON_IsNumber(en2_size_item)) {
        s_state.extranonce2_size = en2_size_item->valueint;
        ESP_LOGI(TAG, "Subscribe response: extranonce2_size=%d", s_state.extranonce2_size);
    }
}

void stratum_client_set_token_callback(tollgate_stratum_token_cb cb)
{
    s_token_cb = cb;
}

static void stratum_client_task(void *arg)
{
    const tollgate_config_t *cfg = tollgate_config_get();

    while (s_running) {
        if (!s_state.connected) {
            esp_err_t err = stratum_connect(cfg->stratum_host, cfg->stratum_port);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Connection failed, retrying in 10s...");
                vTaskDelay(pdMS_TO_TICKS(10000));
                continue;
            }
            send_subscribe();
            send_authorize();
        }

        char *recv_buf = malloc(2048);
        if (!recv_buf) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        int len = read_line(recv_buf, 2048);
        if (len <= 0) {
            ESP_LOGW(TAG, "Connection lost");
            s_state.connected = false;
            if (s_transport) {
                esp_transport_close(s_transport);
                esp_transport_destroy(s_transport);
                s_transport = NULL;
            }
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        cJSON *root = cJSON_Parse(recv_buf);
        if (!root) continue;

        cJSON *method = cJSON_GetObjectItemCaseSensitive(root, "method");
        if (method && cJSON_IsString(method)) {
            cJSON *params = cJSON_GetObjectItemCaseSensitive(root, "params");

            if (strcmp(method->valuestring, "mining.notify") == 0) {
                handle_mining_notify(params);
            } else if (strcmp(method->valuestring, "mining.set_difficulty") == 0) {
                handle_mining_set_difficulty(params);
            } else if (strcmp(method->valuestring, "mining.token") == 0) {
                handle_mining_token(params);
            }
        }

        cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "id");
        cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
        cJSON *error = cJSON_GetObjectItemCaseSensitive(root, "error");

        if (id && result) {
            if (cJSON_IsFalse(result) || (error && !cJSON_IsNull(error))) {
                ESP_LOGW(TAG, "Request %d rejected", id->valueint);
            } else if (cJSON_IsArray(result)) {
                handle_subscribe_response(result, (uint32_t)id->valueint);
            }
        }

        cJSON_Delete(root);
        free(recv_buf);
    }

    if (s_transport) {
        esp_transport_close(s_transport);
        esp_transport_destroy(s_transport);
        s_transport = NULL;
    }
    s_state.connected = false;
    vTaskDelete(NULL);
}

esp_err_t stratum_client_init(void)
{
    memset(&s_state, 0, sizeof(s_state));
    s_req_id = 1;
    return ESP_OK;
}

esp_err_t stratum_client_start(void)
{
    if (s_running) return ESP_OK;
    s_running = true;
    BaseType_t ret = xTaskCreate(stratum_client_task, "stratum_cli", 6144, NULL, 4, &s_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create stratum client task");
        s_running = false;
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Stratum client started");
    return ESP_OK;
}

void stratum_client_stop(void)
{
    s_running = false;
    if (s_task_handle) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        s_task_handle = NULL;
    }
}

esp_err_t stratum_client_submit_share(uint32_t job_id, uint32_t nonce, uint32_t ntime)
{
    if (!s_state.connected || !s_transport) return ESP_FAIL;

    const tollgate_config_t *cfg = tollgate_config_get();

    int en2_size = s_state.extranonce2_size > 0 ? s_state.extranonce2_size : 8;
    char extranonce2_hex[65];
    memset(extranonce2_hex, '0', en2_size * 2);
    extranonce2_hex[en2_size * 2] = '\0';

    char submit[512];
    int len = tollgate_core_stratum_build_submit(submit, sizeof(submit), s_req_id++,
                                                   cfg->stratum_user, job_id,
                                                   extranonce2_hex, ntime, nonce);
    if (len <= 0) return ESP_FAIL;

    int written = esp_transport_write(s_transport, submit, len, 5000);
    if (written < 0) {
        ESP_LOGW(TAG, "Failed to submit share");
        s_state.shares_rejected++;
        return ESP_FAIL;
    }

    s_state.shares_accepted++;
    ESP_LOGI(TAG, "Share submitted: job=%lu nonce=%08lx en2_size=%d",
             (unsigned long)job_id, (unsigned long)nonce, en2_size);
    return ESP_OK;
}

const stratum_client_state_t *stratum_client_get_state(void)
{
    return &s_state;
}

void stratum_client_tick(void)
{
}
