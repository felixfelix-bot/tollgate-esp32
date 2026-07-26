/*
 * tls_worker_stub.c — No-op stub for standalone mining build.
 */
#include "tls_worker.h"
#include "esp_log.h"

static const char *TAG = "tls_stub";

void tls_worker_submit(const char *token)
{
    ESP_LOGD(TAG, "tls_worker_submit stub (token %.16s...)", token ? token : "(null)");
}
