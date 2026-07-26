/*
 * stratum_proxy_stub.c — Minimal in-memory job holder.
 *
 * In the tollgate firmware, stratum_proxy.c delegates to
 * tollgate_core_stratum_proxy.c which runs a full Stratum v1 server
 * (listens on port 3333, manages miner connections, broadcasts jobs).
 *
 * For standalone SW mining we don't need the server — sw_miner.c just
 * calls stratum_proxy_get_current_job() to get the latest job that
 * stratum_client.c received via mining.notify and stored via
 * stratum_proxy_set_job().
 *
 * This stub provides exactly those two functions + init/stop no-ops.
 */
#include "stratum_proxy.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "stratum_px";

static stratum_job_t s_current_job = {0};
static SemaphoreHandle_t s_mutex = NULL;

esp_err_t stratum_proxy_init(uint16_t port, bool self_test)
{
    (void)port;
    (void)self_test;
    if (!s_mutex) {
        s_mutex = xSemaphoreCreateMutex();
    }
    memset(&s_current_job, 0, sizeof(s_current_job));
    ESP_LOGI(TAG, "Stratum proxy stub initialized (no server, in-memory job only)");
    return ESP_OK;
}

void stratum_proxy_set_job(const stratum_job_t *job)
{
    if (!job || !s_mutex) return;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_current_job = *job;
    xSemaphoreGive(s_mutex);
}

const stratum_job_t *stratum_proxy_get_current_job(void)
{
    /* Return pointer to the static job.  Caller copies it before use
     * (sw_miner.c does memcpy), so we don't need a read-lock here. */
    if (!s_mutex) return NULL;
    return &s_current_job;
}

void stratum_proxy_get_stats(stratum_proxy_stats_t *stats)
{
    if (stats) memset(stats, 0, sizeof(*stats));
}

void stratum_proxy_stop(void)
{
    /* Nothing to clean up in standalone mode */
}
