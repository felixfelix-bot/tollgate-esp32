#include "mcp_handler.h"
#include "config.h"
#include "gps.h"
#include "nucula_wallet.h"
#include "tollgate_core_session.h"
#include "tollgate_core.h"
#include "cJSON.h"
#include "lwip/ip4_addr.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "mcp_handler";

mcp_tool_t mcp_parse_tool(const char *method)
{
    if (!method) return MCP_TOOL_UNKNOWN;
    if (strcmp(method, "get_config") == 0) return MCP_TOOL_GET_CONFIG;
    if (strcmp(method, "set_config") == 0) return MCP_TOOL_SET_CONFIG;
    if (strcmp(method, "get_balance") == 0) return MCP_TOOL_GET_BALANCE;
    if (strcmp(method, "wallet_send") == 0) return MCP_TOOL_WALLET_SEND;
    if (strcmp(method, "get_sessions") == 0) return MCP_TOOL_GET_SESSIONS;
    if (strcmp(method, "get_usage") == 0) return MCP_TOOL_GET_USAGE;
    if (strcmp(method, "set_payout") == 0) return MCP_TOOL_SET_PAYOUT;
    if (strcmp(method, "set_metric") == 0) return MCP_TOOL_SET_METRIC;
    if (strcmp(method, "set_price") == 0) return MCP_TOOL_SET_PRICE;
    if (strcmp(method, "wallet_melt") == 0) return MCP_TOOL_WALLET_MELT;
    if (strcmp(method, "get_location") == 0) return MCP_TOOL_GET_LOCATION;
    return MCP_TOOL_UNKNOWN;
}

mcp_response_t mcp_handle_get_config(void)
{
    mcp_response_t resp = {0};
    const tollgate_config_t *cfg = tollgate_config_get();
    if (!cfg) {
        resp.success = false;
        snprintf(resp.error, sizeof(resp.error), "Config not loaded");
        return resp;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "ssid", cfg->ap_ssid);
    cJSON_AddStringToObject(root, "metric", cfg->metric);
    cJSON_AddNumberToObject(root, "price_per_step", cfg->price_per_step);
    cJSON_AddNumberToObject(root, "step_size_ms", cfg->step_size_ms);
    cJSON_AddNumberToObject(root, "step_size_bytes", cfg->step_size_bytes);
    cJSON_AddStringToObject(root, "mint_url", cfg->mint_url);
    cJSON_AddBoolToObject(root, "client_enabled", cfg->client_enabled);
    cJSON_AddBoolToObject(root, "payout_enabled", cfg->payout.enabled);
    cJSON_AddStringToObject(root, "wifi_ssid",
        cfg->network_count > 0 ? cfg->networks[0].ssid : "");

    char *json = cJSON_PrintUnformatted(root);
    snprintf(resp.result_json, sizeof(resp.result_json), "%s", json);
    cJSON_free(json);
    cJSON_Delete(root);
    resp.success = true;
    return resp;
}

mcp_response_t mcp_handle_set_config(const char *params_json)
{
    mcp_response_t resp = {0};
    cJSON *root = cJSON_Parse(params_json);
    if (!root) {
        resp.success = false;
        snprintf(resp.error, sizeof(resp.error), "Invalid JSON params");
        return resp;
    }

    tollgate_config_t *cfg = (tollgate_config_t *)tollgate_config_get();
    if (!cfg) {
        cJSON_Delete(root);
        resp.success = false;
        snprintf(resp.error, sizeof(resp.error), "Config not loaded");
        return resp;
    }

    cJSON *item;
    item = cJSON_GetObjectItem(root, "price_per_step");
    if (item && cJSON_IsNumber(item)) cfg->price_per_step = item->valueint;
    item = cJSON_GetObjectItem(root, "step_size_ms");
    if (item && cJSON_IsNumber(item)) cfg->step_size_ms = item->valueint;
    item = cJSON_GetObjectItem(root, "step_size_bytes");
    if (item && cJSON_IsNumber(item)) cfg->step_size_bytes = item->valueint;
    item = cJSON_GetObjectItem(root, "client_enabled");
    if (item && cJSON_IsBool(item)) cfg->client_enabled = cJSON_IsTrue(item);
    item = cJSON_GetObjectItem(root, "payout_enabled");
    if (item && cJSON_IsBool(item)) cfg->payout.enabled = cJSON_IsTrue(item);
    item = cJSON_GetObjectItem(root, "metric");
    if (item && cJSON_IsString(item)) {
        strncpy(cfg->metric, item->valuestring, sizeof(cfg->metric) - 1);
    }

    cJSON_Delete(root);
    resp.success = true;
    snprintf(resp.result_json, sizeof(resp.result_json), "{\"status\":\"ok\"}");
    return resp;
}

mcp_response_t mcp_handle_get_balance(void)
{
    mcp_response_t resp = {0};
    uint64_t balance = nucula_wallet_balance();
    int proof_count = nucula_wallet_proof_count();

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "balance_sats", (double)balance);
    cJSON_AddNumberToObject(root, "proof_count", proof_count);

    char *json = cJSON_PrintUnformatted(root);
    snprintf(resp.result_json, sizeof(resp.result_json), "%s", json);
    cJSON_free(json);
    cJSON_Delete(root);
    resp.success = true;
    return resp;
}

mcp_response_t mcp_handle_wallet_send(const char *params_json)
{
    mcp_response_t resp = {0};
    cJSON *root = cJSON_Parse(params_json);
    if (!root) {
        resp.success = false;
        snprintf(resp.error, sizeof(resp.error), "Invalid JSON params");
        return resp;
    }

    cJSON *amount_item = cJSON_GetObjectItem(root, "amount");
    if (!amount_item || !cJSON_IsNumber(amount_item)) {
        cJSON_Delete(root);
        resp.success = false;
        snprintf(resp.error, sizeof(resp.error), "Missing 'amount' field");
        return resp;
    }

    uint64_t amount = (uint64_t)amount_item->valuedouble;
    char token_out[2048] = {0};
    int rc = nucula_wallet_send(amount, token_out, sizeof(token_out));

    if (rc != 0) {
        cJSON_Delete(root);
        resp.success = false;
        snprintf(resp.error, sizeof(resp.error), "Send failed: %d", rc);
        return resp;
    }

    cJSON *result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "token", token_out);
    cJSON_AddNumberToObject(result, "amount", (double)amount);
    char *json = cJSON_PrintUnformatted(result);
    snprintf(resp.result_json, sizeof(resp.result_json), "%s", json);
    cJSON_free(json);
    cJSON_Delete(result);
    cJSON_Delete(root);
    resp.success = true;
    return resp;
}

mcp_response_t mcp_handle_get_sessions(void)
{
    mcp_response_t resp = {0};

    cJSON *arr = cJSON_CreateArray();
    int count = tollgate_core_session_get_array_size();
    tg_session_t *sessions = tollgate_core_session_get_array();

    if (sessions && count > 0) {
        for (int i = 0; i < count; i++) {
            if (!sessions[i].active) continue;
            cJSON *s = cJSON_CreateObject();
            esp_ip4_addr_t ip = { .addr = sessions[i].client_ip };
            char ip_str[16];
            snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip));
            cJSON_AddStringToObject(s, "client_ip", ip_str);
            if (sessions[i].mac[0])
                cJSON_AddStringToObject(s, "mac", sessions[i].mac);
            cJSON_AddNumberToObject(s, "allotment_ms", (double)sessions[i].allotment_ms);
            cJSON_AddNumberToObject(s, "allotment_bytes", (double)sessions[i].allotment_bytes);
            cJSON_AddNumberToObject(s, "bytes_consumed", (double)sessions[i].bytes_consumed);
            cJSON_AddBoolToObject(s, "active", sessions[i].active);
            cJSON_AddItemToArray(arr, s);
        }
    }

    char *json = cJSON_PrintUnformatted(arr);
    snprintf(resp.result_json, sizeof(resp.result_json), "%s", json);
    cJSON_free(json);
    cJSON_Delete(arr);
    resp.success = true;
    return resp;
}

mcp_response_t mcp_handle_get_usage(void)
{
    mcp_response_t resp = {0};
    const tollgate_config_t *cfg = tollgate_config_get();

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "metric", cfg->metric);
    cJSON_AddNumberToObject(root, "price_per_step", cfg->price_per_step);
    cJSON_AddNumberToObject(root, "step_size_ms", cfg->step_size_ms);
    cJSON_AddNumberToObject(root, "step_size_bytes", cfg->step_size_bytes);
    cJSON_AddBoolToObject(root, "client_enabled", cfg->client_enabled);

    char *json = cJSON_PrintUnformatted(root);
    snprintf(resp.result_json, sizeof(resp.result_json), "%s", json);
    cJSON_free(json);
    cJSON_Delete(root);
    resp.success = true;
    return resp;
}

mcp_response_t mcp_handle_set_payout(const char *params_json)
{
    mcp_response_t resp = {0};
    cJSON *root = cJSON_Parse(params_json);
    if (!root) {
        resp.success = false;
        snprintf(resp.error, sizeof(resp.error), "Invalid JSON params");
        return resp;
    }

    tollgate_config_t *cfg = (tollgate_config_t *)tollgate_config_get();
    if (!cfg) {
        cJSON_Delete(root);
        resp.success = false;
        snprintf(resp.error, sizeof(resp.error), "Config not loaded");
        return resp;
    }

    cJSON *enabled = cJSON_GetObjectItem(root, "enabled");
    if (enabled && cJSON_IsBool(enabled)) cfg->payout.enabled = cJSON_IsTrue(enabled);

    cJSON *recipients = cJSON_GetObjectItem(root, "recipients");
    if (recipients && cJSON_IsArray(recipients)) {
        int rcount = cJSON_GetArraySize(recipients);
        if (rcount > PAYOUT_MAX_RECIPIENTS) rcount = PAYOUT_MAX_RECIPIENTS;
        for (int i = 0; i < rcount; i++) {
            cJSON *r = cJSON_GetArrayItem(recipients, i);
            cJSON *addr = cJSON_GetObjectItem(r, "lightning_address");
            cJSON *factor = cJSON_GetObjectItem(r, "factor");
            if (addr && cJSON_IsString(addr)) {
                strncpy(cfg->payout.recipients[i].lightning_address, addr->valuestring,
                        sizeof(cfg->payout.recipients[i].lightning_address) - 1);
            }
            if (factor && cJSON_IsNumber(factor)) {
                cfg->payout.recipients[i].factor = factor->valuedouble;
            }
        }
        cfg->payout.recipient_count = rcount;
    }

    cJSON_Delete(root);
    resp.success = true;
    snprintf(resp.result_json, sizeof(resp.result_json), "{\"status\":\"ok\"}");
    return resp;
}

mcp_response_t mcp_handle_set_metric(const char *params_json)
{
    mcp_response_t resp = {0};
    cJSON *root = cJSON_Parse(params_json);
    if (!root) {
        resp.success = false;
        snprintf(resp.error, sizeof(resp.error), "Invalid JSON params");
        return resp;
    }

    tollgate_config_t *cfg = (tollgate_config_t *)tollgate_config_get();
    if (!cfg) {
        cJSON_Delete(root);
        resp.success = false;
        snprintf(resp.error, sizeof(resp.error), "Config not loaded");
        return resp;
    }

    cJSON *metric = cJSON_GetObjectItem(root, "metric");
    if (metric && cJSON_IsString(metric)) {
        const char *m = metric->valuestring;
        if (strcmp(m, "bytes") == 0 || strcmp(m, "milliseconds") == 0) {
            strncpy(cfg->metric, m, sizeof(cfg->metric) - 1);
        } else {
            cJSON_Delete(root);
            resp.success = false;
            snprintf(resp.error, sizeof(resp.error), "Invalid metric: must be 'bytes' or 'milliseconds'");
            return resp;
        }
    } else {
        cJSON_Delete(root);
        resp.success = false;
        snprintf(resp.error, sizeof(resp.error), "Missing 'metric' field");
        return resp;
    }

    cJSON_Delete(root);
    resp.success = true;
    snprintf(resp.result_json, sizeof(resp.result_json), "{\"status\":\"ok\",\"metric\":\"%s\"}", cfg->metric);
    return resp;
}

mcp_response_t mcp_handle_set_price(const char *params_json)
{
    mcp_response_t resp = {0};
    cJSON *root = cJSON_Parse(params_json);
    if (!root) {
        resp.success = false;
        snprintf(resp.error, sizeof(resp.error), "Invalid JSON params");
        return resp;
    }

    tollgate_config_t *cfg = (tollgate_config_t *)tollgate_config_get();
    if (!cfg) {
        cJSON_Delete(root);
        resp.success = false;
        snprintf(resp.error, sizeof(resp.error), "Config not loaded");
        return resp;
    }

    cJSON *price = cJSON_GetObjectItem(root, "price_per_step");
    if (price && cJSON_IsNumber(price) && price->valueint > 0) {
        cfg->price_per_step = price->valueint;
    } else {
        cJSON_Delete(root);
        resp.success = false;
        snprintf(resp.error, sizeof(resp.error), "Missing or invalid 'price_per_step' field");
        return resp;
    }

    cJSON_Delete(root);
    resp.success = true;
    snprintf(resp.result_json, sizeof(resp.result_json),
             "{\"status\":\"ok\",\"price_per_step\":%d}", cfg->price_per_step);
    return resp;
}

mcp_response_t mcp_handle_wallet_melt(const char *params_json)
{
    mcp_response_t resp = {0};
    cJSON *root = cJSON_Parse(params_json);
    if (!root) {
        resp.success = false;
        snprintf(resp.error, sizeof(resp.error), "Invalid JSON params");
        return resp;
    }

    cJSON *bolt11 = cJSON_GetObjectItem(root, "bolt11");
    if (!bolt11 || !cJSON_IsString(bolt11)) {
        cJSON_Delete(root);
        resp.success = false;
        snprintf(resp.error, sizeof(resp.error), "Missing 'bolt11' field");
        return resp;
    }

    cJSON *max_fee = cJSON_GetObjectItem(root, "max_fee_sats");
    uint64_t fee = 10;
    if (max_fee && cJSON_IsNumber(max_fee)) fee = (uint64_t)max_fee->valuedouble;

    esp_err_t rc = nucula_wallet_melt(bolt11->valuestring, fee);

    if (rc != ESP_OK) {
        cJSON_Delete(root);
        resp.success = false;
        snprintf(resp.error, sizeof(resp.error), "Melt failed: %s", esp_err_to_name(rc));
        return resp;
    }

    cJSON_Delete(root);
    resp.success = true;
    snprintf(resp.result_json, sizeof(resp.result_json), "{\"status\":\"ok\"}");
    return resp;
}

mcp_response_t mcp_handle_get_location(void)
{
    mcp_response_t resp = {0};
    const gps_fix_t *fix = gps_get_fix();

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "valid", fix->valid);
    cJSON_AddNumberToObject(root, "latitude", fix->latitude);
    cJSON_AddNumberToObject(root, "longitude", fix->longitude);
    cJSON_AddStringToObject(root, "geohash", fix->geohash);
    cJSON_AddNumberToObject(root, "altitude_m", fix->altitude_m);
    cJSON_AddNumberToObject(root, "satellites", fix->satellites);
    cJSON_AddNumberToObject(root, "hdop", fix->hdop);
    cJSON_AddNumberToObject(root, "fix_quality", fix->fix_quality);
    cJSON_AddNumberToObject(root, "speed_knots", fix->speed_knots);
    cJSON_AddNumberToObject(root, "course_deg", fix->course_deg);
    cJSON_AddStringToObject(root, "utc_time", fix->utc_time);
    cJSON_AddStringToObject(root, "utc_date", fix->utc_date);
    cJSON_AddNumberToObject(root, "updated_ms", (double)fix->updated_ms);

    char *json = cJSON_PrintUnformatted(root);
    snprintf(resp.result_json, sizeof(resp.result_json), "%s", json);
    cJSON_free(json);
    cJSON_Delete(root);
    resp.success = true;
    return resp;
}

mcp_response_t mcp_dispatch(const mcp_request_t *req)
{
    if (!req) {
        mcp_response_t resp = {0};
        resp.success = false;
        snprintf(resp.error, sizeof(resp.error), "NULL request");
        return resp;
    }

    switch (req->tool) {
        case MCP_TOOL_GET_CONFIG:
            return mcp_handle_get_config();
        case MCP_TOOL_SET_CONFIG:
            return mcp_handle_set_config(req->params_json);
        case MCP_TOOL_GET_BALANCE:
            return mcp_handle_get_balance();
        case MCP_TOOL_WALLET_SEND:
            return mcp_handle_wallet_send(req->params_json);
        case MCP_TOOL_GET_SESSIONS:
            return mcp_handle_get_sessions();
        case MCP_TOOL_GET_USAGE:
            return mcp_handle_get_usage();
        case MCP_TOOL_SET_PAYOUT:
            return mcp_handle_set_payout(req->params_json);
        case MCP_TOOL_SET_METRIC:
            return mcp_handle_set_metric(req->params_json);
        case MCP_TOOL_SET_PRICE:
            return mcp_handle_set_price(req->params_json);
        case MCP_TOOL_WALLET_MELT:
            return mcp_handle_wallet_melt(req->params_json);
        case MCP_TOOL_GET_LOCATION:
            return mcp_handle_get_location();
        default:
            break;
    }

    mcp_response_t resp = {0};
    resp.success = false;
    snprintf(resp.error, sizeof(resp.error), "Unknown tool: %s", req->method);
    return resp;
}
