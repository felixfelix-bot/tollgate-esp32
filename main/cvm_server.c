#include "cvm_server.h"
#include "mcp_handler.h"
#include "nip04.h"
#include "identity.h"
#include "nostr_event.h"
#include "config.h"
#include "tollgate_core_session.h"
#include "nucula_wallet.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_tls.h"
#include "esp_crt_bundle.h"
#include "esp_random.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "cvm_server";

static bool g_running = false;
static TaskHandle_t g_task = NULL;

static void publish_announcements_via_ws(esp_tls_t *tls);

#define CVM_VERSION "2025-07-02"
#define CVM_SERVER_NAME "TollGate"
#define CVM_SERVER_VERSION "1.0.0"
#define CVM_WS_BUF_SIZE 8192
#define CVM_MAX_RESPONSE_SIZE 4096
#define CVM_RECONNECT_DELAY_MS 5000
#define CVM_WS_PING_INTERVAL_S 30

static char *parse_ws_text_frame(const uint8_t *buf, int len)
{
    if (len < 2) return NULL;
    bool masked = (buf[1] & 0x80) != 0;
    uint64_t payload_len = buf[1] & 0x7F;
    int offset = 2;

    if (payload_len == 126) {
        if (len < 4) return NULL;
        payload_len = ((uint64_t)buf[2] << 8) | buf[3];
        offset = 4;
    } else if (payload_len == 127) {
        if (len < 10) return NULL;
        payload_len = 0;
        for (int i = 0; i < 8; i++)
            payload_len = (payload_len << 8) | buf[2 + i];
        offset = 10;
    }

    if (masked) offset += 4;
    if (offset + payload_len > (uint64_t)len) return NULL;

    char *text = malloc((size_t)payload_len + 1);
    if (!text) return NULL;

    if (masked) {
        uint8_t mask[4] = { buf[offset - 4], buf[offset - 3], buf[offset - 2], buf[offset - 1] };
        for (uint64_t i = 0; i < payload_len; i++)
            text[i] = buf[offset + i] ^ mask[i & 3];
    } else {
        memcpy(text, buf + offset, (size_t)payload_len);
    }
    text[payload_len] = '\0';
    return text;
}

static int ws_send_text(esp_tls_t *tls, const char *msg)
{
    size_t len = strlen(msg);
    uint8_t mask[4];
    esp_fill_random(mask, 4);

    size_t frame_len = 6 + len;
    if (len > 125) frame_len += 2;
    if (len > 65535) frame_len += 6;

    uint8_t *frame = malloc(frame_len + len);
    if (!frame) return -1;

    int pos = 0;
    frame[pos++] = 0x81;
    if (len <= 125) {
        frame[pos++] = (uint8_t)(0x80 | len);
    } else if (len <= 65535) {
        frame[pos++] = 0x80 | 126;
        frame[pos++] = (uint8_t)((len >> 8) & 0xff);
        frame[pos++] = (uint8_t)(len & 0xff);
    } else {
        frame[pos++] = 0x80 | 127;
        for (int i = 0; i < 8; i++)
            frame[pos++] = (uint8_t)((len >> (56 - i * 8)) & 0xff);
    }
    memcpy(frame + pos, mask, 4);
    pos += 4;

    for (size_t i = 0; i < len; i++)
        frame[pos + i] = (uint8_t)msg[i] ^ mask[i & 3];
    pos += len;

    int total = pos;
    int written = 0;
    while (written < total) {
        int w = esp_tls_conn_write(tls, frame + written, total - written);
        if (w < 0) {
            ESP_LOGE(TAG, "ws_send: write failed at %d/%d", written, total);
            free(frame);
            return -1;
        }
        if (w == 0) {
            ESP_LOGW(TAG, "ws_send: write returned 0 at %d/%d", written, total);
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        written += w;
    }
    ESP_LOGD(TAG, "ws_send: sent %d bytes (payload %d)", total, (int)len);
    free(frame);
    return 0;
}

static esp_err_t ws_connect(const char *relay_url, esp_tls_t **tls_out)
{
    char host[128] = {0};
    int port = 443;
    char path[128] = "/";

    if (strncmp(relay_url, "wss://", 6) != 0) return ESP_ERR_INVALID_ARG;

    const char *url_start = relay_url + 6;
    const char *path_ptr = strchr(url_start, '/');
    if (path_ptr) {
        size_t host_len = path_ptr - url_start;
        if (host_len >= sizeof(host)) host_len = sizeof(host) - 1;
        memcpy(host, url_start, host_len);
        host[host_len] = '\0';
        strncpy(path, path_ptr, sizeof(path) - 1);
    } else {
        strncpy(host, url_start, sizeof(host) - 1);
    }

    char *colon = strchr(host, ':');
    if (colon) {
        *colon = '\0';
        port = atoi(colon + 1);
    }

    esp_tls_cfg_t tls_cfg = {
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 5000,
    };
    esp_tls_t *tls = esp_tls_init();
    if (!tls) return ESP_ERR_NO_MEM;

    int ret = esp_tls_conn_new_sync(host, strlen(host), port, &tls_cfg, tls);
    if (ret < 0) {
        esp_tls_conn_destroy(tls);
        return ESP_FAIL;
    }

    char upgrade[512];
    snprintf(upgrade, sizeof(upgrade),
             "GET %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "Upgrade: websocket\r\n"
             "Connection: Upgrade\r\n"
             "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
             "Sec-WebSocket-Version: 13\r\n"
             "\r\n",
             path, host);

    int written = esp_tls_conn_write(tls, (const unsigned char *)upgrade, strlen(upgrade));
    if (written < 0) {
        esp_tls_conn_destroy(tls);
        return ESP_FAIL;
    }

    char resp[1024];
    int rlen = esp_tls_conn_read(tls, (unsigned char *)resp, sizeof(resp) - 1);
    if (rlen <= 0 || !strstr(resp, "101")) {
        ESP_LOGE(TAG, "WS upgrade failed to %s (read %d)", host, rlen);
        esp_tls_conn_destroy(tls);
        return ESP_FAIL;
    }

    *tls_out = tls;
    ESP_LOGI(TAG, "Connected to %s", host);
    return ESP_OK;
}

static cJSON *build_tools_list(void)
{
    cJSON *tools = cJSON_CreateArray();

    const char *tool_defs[][3] = {
        {"get_config",  "Get current device configuration",      "{\"type\":\"object\",\"properties\":{},\"required\":[]}"},
        {"set_config",  "Update device configuration",            "{\"type\":\"object\",\"properties\":{\"price_per_step\":{\"type\":\"integer\"},\"step_size_ms\":{\"type\":\"integer\"},\"step_size_bytes\":{\"type\":\"integer\"},\"metric\":{\"type\":\"string\"},\"client_enabled\":{\"type\":\"boolean\"},\"payout_enabled\":{\"type\":\"boolean\"}}}"},
        {"get_balance", "Get wallet balance and proof count",     "{\"type\":\"object\",\"properties\":{},\"required\":[]}"},
        {"wallet_send", "Send e-cash tokens from wallet",        "{\"type\":\"object\",\"properties\":{\"amount\":{\"type\":\"integer\",\"description\":\"Amount in sats\"}},\"required\":[\"amount\"]}"},
        {"get_sessions","Get active client sessions",             "{\"type\":\"object\",\"properties\":{},\"required\":[]}"},
        {"get_usage",   "Get current billing usage info",         "{\"type\":\"object\",\"properties\":{},\"required\":[]}"},
        {"set_payout",  "Configure payout recipients",            "{\"type\":\"object\",\"properties\":{\"enabled\":{\"type\":\"boolean\"},\"recipients\":{\"type\":\"array\"}}}"},
        {"set_metric",  "Set billing metric",                     "{\"type\":\"object\",\"properties\":{\"metric\":{\"type\":\"string\",\"enum\":[\"bytes\",\"milliseconds\"]}},\"required\":[\"metric\"]}"},
        {"set_price",   "Set price per step",                     "{\"type\":\"object\",\"properties\":{\"price_per_step\":{\"type\":\"integer\",\"minimum\":1}},\"required\":[\"price_per_step\"]}"},
        {"wallet_melt", "Melt tokens for lightning payment",      "{\"type\":\"object\",\"properties\":{\"bolt11\":{\"type\":\"string\"},\"max_fee_sats\":{\"type\":\"integer\"}},\"required\":[\"bolt11\"]}"},
        {"get_location","Get current GPS position (lat/lon/geohash)","{\"type\":\"object\",\"properties\":{},\"required\":[]}"},
    };

    for (int i = 0; i < (int)(sizeof(tool_defs) / sizeof(tool_defs[0])); i++) {
        cJSON *tool = cJSON_CreateObject();
        cJSON_AddStringToObject(tool, "name", tool_defs[i][0]);
        cJSON_AddStringToObject(tool, "description", tool_defs[i][1]);
        cJSON *schema = cJSON_Parse(tool_defs[i][2]);
        if (schema) cJSON_AddItemToObject(tool, "inputSchema", schema);
        cJSON_AddItemToArray(tools, tool);
    }

    return tools;
}

static char *build_initialize_response(const char *request_id_str, const char *client_pubkey)
{
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(response, "id", request_id_str ? atof(request_id_str) : 0);

    cJSON *result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "protocolVersion", CVM_VERSION);

    cJSON *capabilities = cJSON_CreateObject();
    cJSON_AddItemToObject(capabilities, "tools", cJSON_CreateObject());
    cJSON_AddItemToObject(result, "capabilities", capabilities);

    cJSON *serverInfo = cJSON_CreateObject();
    cJSON_AddStringToObject(serverInfo, "name", CVM_SERVER_NAME);
    cJSON_AddStringToObject(serverInfo, "version", CVM_SERVER_VERSION);
    cJSON_AddItemToObject(result, "serverInfo", serverInfo);

    cJSON_AddItemToObject(response, "result", result);

    char *json = cJSON_PrintUnformatted(response);
    cJSON_Delete(response);
    return json;
}

static char *build_tools_list_response(const char *request_id_str)
{
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(response, "id", request_id_str ? atof(request_id_str) : 1);

    cJSON *result = cJSON_CreateObject();
    cJSON *tools = build_tools_list();
    cJSON_AddItemToObject(result, "tools", tools);
    cJSON_AddItemToObject(response, "result", result);

    char *json = cJSON_PrintUnformatted(response);
    cJSON_Delete(response);
    return json;
}

static char *build_tool_call_response(const char *request_id_str, const mcp_response_t *mcp_resp)
{
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(response, "id", request_id_str ? atof(request_id_str) : 2);

    if (mcp_resp->success) {
        cJSON *result = cJSON_CreateObject();
        cJSON_AddItemToObject(result, "content", cJSON_CreateArray());
        cJSON *content_arr = cJSON_GetObjectItem(result, "content");
        cJSON *text_item = cJSON_CreateObject();
        cJSON_AddStringToObject(text_item, "type", "text");
        cJSON_AddStringToObject(text_item, "text", mcp_resp->result_json);
        cJSON_AddItemToArray(content_arr, text_item);
        cJSON_AddBoolToObject(result, "isError", false);
        cJSON_AddItemToObject(response, "result", result);
    } else {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddNumberToObject(error, "code", -32603);
        cJSON_AddStringToObject(error, "message", mcp_resp->error);
        cJSON_AddItemToObject(response, "error", error);
    }

    char *json = cJSON_PrintUnformatted(response);
    cJSON_Delete(response);
    return json;
}

static char *build_ping_response(const char *request_id_str)
{
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(response, "id", request_id_str ? atof(request_id_str) : 0);
    cJSON *result = cJSON_CreateObject();
    cJSON_AddItemToObject(response, "result", result);
    char *json = cJSON_PrintUnformatted(response);
    cJSON_Delete(response);
    return json;
}

static esp_err_t publish_event_to_relay(const char *relay_url, const char *event_json)
{
    esp_tls_t *tls = NULL;
    esp_err_t err = ws_connect(relay_url, &tls);
    if (err != ESP_OK) return err;

    char *msg;
    size_t event_len2 = strlen(event_json);
     size_t msg_len2 = 10 + event_len2 + 2;
    msg = malloc(msg_len2);
    snprintf(msg, msg_len2, "[\"EVENT\",%s]", event_json);

    ws_send_text(tls, msg);
    free(msg);

    uint8_t resp_buf[256];
    esp_tls_conn_read(tls, resp_buf, sizeof(resp_buf) - 1);

    uint8_t close_frame[2] = {0x88, 0x00};
    esp_tls_conn_write(tls, close_frame, 2);
    esp_tls_conn_destroy(tls);
    return ESP_OK;
}

static esp_err_t publish_kind_25910_response(const char *relay_url,
                                              const char *content_json,
                                              const char *request_event_id)
{
    const tollgate_identity_t *id = identity_get();
    if (!id || !id->initialized) return ESP_FAIL;

    cJSON *tags = cJSON_CreateArray();
    cJSON *e_tag = cJSON_CreateArray();
    cJSON_AddItemToArray(e_tag, cJSON_CreateString("e"));
    cJSON_AddItemToArray(e_tag, cJSON_CreateString(request_event_id));
    cJSON_AddItemToArray(tags, e_tag);

    char *tags_str = cJSON_PrintUnformatted(tags);
    cJSON_Delete(tags);

    nostr_event_t event;
    nostr_event_init(&event, id->npub_hex, 25910, tags_str, content_json);
    nostr_event_sign(&event, id->nsec);
    free(tags_str);

    char *event_json = malloc(8192);
    if (!event_json) return ESP_ERR_NO_MEM;

    esp_err_t ret = nostr_event_to_json(&event, event_json, 8192);
    if (ret != ESP_OK) {
        free(event_json);
        return ret;
    }

    ret = publish_event_to_relay(relay_url, event_json);
    free(event_json);
    return ret;
}

static bool is_owner_pubkey(const char *pubkey_hex)
{
    const tollgate_identity_t *id = identity_get();
    if (!id || !id->initialized) return false;
    if (!pubkey_hex) return false;
    return strcmp(id->npub_hex, pubkey_hex) == 0;
}

static void handle_mcp_message(const char *relay_url, const char *sender_pubkey,
                                const char *event_id, const char *content)
{
    cJSON *msg = cJSON_Parse(content);
    if (!msg) {
        ESP_LOGW(TAG, "Invalid JSON in kind 25910 content");
        return;
    }

    cJSON *method = cJSON_GetObjectItem(msg, "method");
    cJSON *id_field = cJSON_GetObjectItem(msg, "id");
    const char *id_str = (id_field && cJSON_IsNumber(id_field))
                         ? cJSON_PrintUnformatted(id_field) : "0";

    if (method && cJSON_IsString(method)) {
        const char *m = method->valuestring;

        if (strcmp(m, "initialize") == 0) {
            ESP_LOGI(TAG, "MCP initialize from %s", sender_pubkey);
            char *resp = build_initialize_response(id_str, sender_pubkey);
            publish_kind_25910_response(relay_url, resp, event_id);
            free(resp);
        } else if (strcmp(m, "notifications/initialized") == 0) {
            ESP_LOGI(TAG, "Client initialized: %s", sender_pubkey);
        } else if (strcmp(m, "tools/list") == 0) {
            ESP_LOGI(TAG, "tools/list from %s", sender_pubkey);
            char *resp = build_tools_list_response(id_str);
            publish_kind_25910_response(relay_url, resp, event_id);
            free(resp);
        } else if (strcmp(m, "tools/call") == 0) {
            cJSON *params = cJSON_GetObjectItem(msg, "params");
            cJSON *name = params ? cJSON_GetObjectItem(params, "name") : NULL;
            cJSON *arguments = params ? cJSON_GetObjectItem(params, "arguments") : NULL;

            if (name && cJSON_IsString(name)) {
                ESP_LOGI(TAG, "tools/call %s from %s", name->valuestring, sender_pubkey);

                mcp_request_t req = {0};
                req.tool = mcp_parse_tool(name->valuestring);
                strncpy(req.method, name->valuestring, sizeof(req.method) - 1);
                if (arguments) {
                    char *ajson = cJSON_PrintUnformatted(arguments);
                    strncpy(req.params_json, ajson, sizeof(req.params_json) - 1);
                    cJSON_free(ajson);
                }

                mcp_response_t mcp_resp = mcp_dispatch(&req);
                char *resp = build_tool_call_response(id_str, &mcp_resp);
                publish_kind_25910_response(relay_url, resp, event_id);
                free(resp);
            }
        } else if (strcmp(m, "ping") == 0) {
            char *resp = build_ping_response(id_str);
            publish_kind_25910_response(relay_url, resp, event_id);
            free(resp);
        } else {
            ESP_LOGW(TAG, "Unknown MCP method: %s", m);
        }
    }

    if (id_field && cJSON_IsNumber(id_field) && id_str[0] != '0') {
        free((void *)id_str);
    } else if (id_str[0] != '0') {
    }
    cJSON_Delete(msg);
}

static void process_relay_message(const char *relay_url, const char *msg_str)
{
    cJSON *arr = cJSON_Parse(msg_str);
    if (!arr || !cJSON_IsArray(arr)) {
        if (arr) cJSON_Delete(arr);
        return;
    }

    cJSON *cmd = cJSON_GetArrayItem(arr, 0);
    if (!cmd || !cJSON_IsString(cmd)) {
        cJSON_Delete(arr);
        return;
    }

    if (strcmp(cmd->valuestring, "OK") == 0) {
        cJSON *ev_id = cJSON_GetArrayItem(arr, 1);
        cJSON *ok = cJSON_GetArrayItem(arr, 2);
        cJSON *reason = cJSON_GetArrayItem(arr, 3);
        ESP_LOGI(TAG, "Relay OK: id=%.16s success=%s reason=%s",
                 ev_id ? ev_id->valuestring : "?",
                 ok ? (cJSON_IsTrue(ok) ? "true" : "FALSE") : "?",
                 reason ? reason->valuestring : "");
        cJSON_Delete(arr);
        return;
    }

    if (strcmp(cmd->valuestring, "EVENT") != 0) {
        ESP_LOGI(TAG, "Relay msg: %.100s", msg_str);
        cJSON_Delete(arr);
        return;
    }

    cJSON *event = cJSON_GetArrayItem(arr, 2);
    if (!event) {
        cJSON_Delete(arr);
        return;
    }

    cJSON *kind = cJSON_GetObjectItem(event, "kind");
    if (!kind || kind->valueint != 25910) {
        cJSON_Delete(arr);
        return;
    }

    cJSON *pubkey = cJSON_GetObjectItem(event, "pubkey");
    cJSON *event_id = cJSON_GetObjectItem(event, "id");
    cJSON *content = cJSON_GetObjectItem(event, "content");

    if (!pubkey || !content || !event_id) {
        cJSON_Delete(arr);
        return;
    }

    if (!is_owner_pubkey(pubkey->valuestring)) {
        ESP_LOGW(TAG, "Ignoring request from non-owner: %.16s...", pubkey->valuestring);
        cJSON_Delete(arr);
        return;
    }

    handle_mcp_message(relay_url, pubkey->valuestring, event_id->valuestring, content->valuestring);
    cJSON_Delete(arr);
}

static esp_err_t subscribe_to_relay(esp_tls_t *tls, const char *npub)
{
    cJSON *sub = cJSON_CreateArray();
    cJSON_AddItemToArray(sub, cJSON_CreateString("REQ"));
    cJSON_AddItemToArray(sub, cJSON_CreateString("cvm_sub"));
    cJSON *filter = cJSON_CreateObject();
    cJSON *kinds = cJSON_CreateArray();
    cJSON_AddItemToArray(kinds, cJSON_CreateNumber(25910));
    cJSON_AddItemToObject(filter, "kinds", kinds);
    cJSON *p_tag = cJSON_CreateArray();
    cJSON_AddItemToArray(p_tag, cJSON_CreateString(npub));
    cJSON_AddItemToObject(filter, "#p", p_tag);
    cJSON_AddNumberToObject(filter, "limit", 100);
    cJSON_AddItemToArray(sub, filter);

    char *msg = cJSON_PrintUnformatted(sub);
    cJSON_Delete(sub);

    int rc = ws_send_text(tls, msg);
    free(msg);
    return rc == 0 ? ESP_OK : ESP_FAIL;
}

static void cvm_relay_task(void *arg)
{
    const char *relay_url = (const char *)arg;
    const tollgate_identity_t *id = identity_get();
    if (!id || !id->initialized) {
        ESP_LOGE(TAG, "Identity not initialized");
        vTaskDelete(NULL);
        return;
    }

    while (g_running) {
        esp_tls_t *tls = NULL;
        esp_err_t err = ws_connect(relay_url, &tls);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Connect failed to %s, retrying", relay_url);
            vTaskDelay(pdMS_TO_TICKS(CVM_RECONNECT_DELAY_MS));
            continue;
        }

        err = subscribe_to_relay(tls, id->npub_hex);
        if (err != ESP_OK) {
            esp_tls_conn_destroy(tls);
            vTaskDelay(pdMS_TO_TICKS(CVM_RECONNECT_DELAY_MS));
            continue;
        }

        ESP_LOGI(TAG, "Listening on %s for kind 25910 events", relay_url);
        publish_announcements_via_ws(tls);

        uint8_t *buf = malloc(CVM_WS_BUF_SIZE);
        if (!buf) {
            esp_tls_conn_destroy(tls);
            vTaskDelete(NULL);
            return;
        }

        int64_t last_ping_time = (int64_t)(xTaskGetTickCount() * portTICK_PERIOD_MS) / 1000;
        while (g_running) {
            int rlen = esp_tls_conn_read(tls, buf, CVM_WS_BUF_SIZE - 1);
            if (rlen == 0) {
                ESP_LOGW(TAG, "Connection closed by relay");
                break;
            }
            if (rlen < 0) {
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }

            ESP_LOGI(TAG, "WS frame received: %d bytes, opcode=0x%02x", rlen, buf[0] & 0x0F);

            if ((buf[0] & 0x0F) == 0x01) {
                char *text = parse_ws_text_frame(buf, rlen);
                if (text) {
                    if (strlen(text) > 0) {
                        process_relay_message(relay_url, text);
                    }
                    free(text);
                }
            } else if ((buf[0] & 0x0F) == 0x09) {
                uint8_t pong[2] = {0x8A, 0x00};
                esp_tls_conn_write(tls, pong, 2);
            } else if ((buf[0] & 0x0F) == 0x08) {
                ESP_LOGW(TAG, "Relay sent close frame");
                break;
            }

            int64_t now = (int64_t)(xTaskGetTickCount() * portTICK_PERIOD_MS) / 1000;
            if (now - last_ping_time >= CVM_WS_PING_INTERVAL_S) {
                uint8_t ping[2] = {0x89, 0x00};
                esp_tls_conn_write(tls, ping, 2);
                last_ping_time = now;
            }
        }

        free(buf);
        uint8_t close_frame[2] = {0x88, 0x00};
        esp_tls_conn_write(tls, close_frame, 2);
        esp_tls_conn_destroy(tls);
        ESP_LOGW(TAG, "Disconnected from %s, reconnecting", relay_url);
        vTaskDelay(pdMS_TO_TICKS(CVM_RECONNECT_DELAY_MS));
    }

    vTaskDelete(NULL);
}

static esp_err_t publish_event_via_ws(esp_tls_t *tls, int kind,
                                       const char *content, const char *tags_json)
{
    const tollgate_identity_t *id = identity_get();
    if (!id || !id->initialized) return ESP_FAIL;

    nostr_event_t event;
    nostr_event_init(&event, id->npub_hex, kind, tags_json, content);
    nostr_event_sign(&event, id->nsec);

    char *event_json = malloc(4096);
    if (!event_json) return ESP_ERR_NO_MEM;

    esp_err_t ret = nostr_event_to_json(&event, event_json, 4096);
    if (ret != ESP_OK) {
        free(event_json);
        return ret;
    }

    char *msg;
    size_t event_len = strlen(event_json);
    size_t msg_len = 10 + event_len + 2;
    msg = malloc(msg_len);
    snprintf(msg, msg_len, "[\"EVENT\",%s]", event_json);

    ws_send_text(tls, msg);
    ESP_LOGI(TAG, "Published kind %d event (%d bytes)", kind, (int)strlen(event_json));
    free(msg);
    free(event_json);
    return ESP_OK;
}

static void publish_announcements_via_ws(esp_tls_t *tls)
{
    const tollgate_identity_t *id = identity_get();
    if (!id || !id->initialized) return;

    ESP_LOGI(TAG, "Publishing CEP-6 announcements via active WS");

    cJSON *ann_content = cJSON_CreateObject();
    cJSON_AddStringToObject(ann_content, "protocolVersion", CVM_VERSION);
    cJSON *capabilities = cJSON_CreateObject();
    cJSON *tools_cap = cJSON_CreateObject();
    cJSON_AddBoolToObject(tools_cap, "listChanged", true);
    cJSON_AddItemToObject(capabilities, "tools", tools_cap);
    cJSON_AddItemToObject(ann_content, "capabilities", capabilities);
    cJSON *serverInfo = cJSON_CreateObject();
    cJSON_AddStringToObject(serverInfo, "name", CVM_SERVER_NAME);
    cJSON_AddStringToObject(serverInfo, "version", CVM_SERVER_VERSION);
    cJSON_AddItemToObject(ann_content, "serverInfo", serverInfo);
    char *ann_str = cJSON_PrintUnformatted(ann_content);
    cJSON_Delete(ann_content);

    cJSON *ann_tags = cJSON_CreateArray();
    cJSON *name_tag = cJSON_CreateArray();
    cJSON_AddItemToArray(name_tag, cJSON_CreateString("name"));
    cJSON_AddItemToArray(name_tag, cJSON_CreateString(CVM_SERVER_NAME));
    cJSON_AddItemToArray(ann_tags, name_tag);
    cJSON *about_tag = cJSON_CreateArray();
    cJSON_AddItemToArray(about_tag, cJSON_CreateString("about"));
    cJSON_AddItemToArray(about_tag, cJSON_CreateString("ESP32 TollGate WiFi hotspot with Cashu e-cash payments"));
    cJSON_AddItemToArray(ann_tags, about_tag);
    char *ann_tags_str = cJSON_PrintUnformatted(ann_tags);
    cJSON_Delete(ann_tags);

    publish_event_via_ws(tls, 11316, ann_str, ann_tags_str);
    free(ann_str);
    free(ann_tags_str);

    cJSON *tools = build_tools_list();
    cJSON *tools_content = cJSON_CreateObject();
    cJSON_AddItemToObject(tools_content, "tools", tools);
    char *tools_str = cJSON_PrintUnformatted(tools_content);
    cJSON_Delete(tools_content);

    publish_event_via_ws(tls, 11317, tools_str, "[]");
    free(tools_str);

    cJSON *relay_tags = cJSON_CreateArray();
    const char *relays[] = {"wss://relay.primal.net", "wss://nostr-pub.wellorder.net", NULL};
    for (int i = 0; relays[i]; i++) {
        cJSON *r_tag = cJSON_CreateArray();
        cJSON_AddItemToArray(r_tag, cJSON_CreateString("r"));
        cJSON_AddItemToArray(r_tag, cJSON_CreateString(relays[i]));
        cJSON_AddItemToArray(relay_tags, r_tag);
    }
    char *relay_tags_str = cJSON_PrintUnformatted(relay_tags);
    cJSON_Delete(relay_tags);

    publish_event_via_ws(tls, 10002, "", relay_tags_str);
    free(relay_tags_str);

    ESP_LOGI(TAG, "CEP-6 announcements published (kinds 11316, 11317, 10002)");
}

esp_err_t cvm_publish_announcements(void)
{
    return ESP_OK;
}

const char *cvm_get_pubkey_hex(void)
{
    const tollgate_identity_t *id = identity_get();
    if (!id || !id->initialized) return NULL;
    return id->npub_hex;
}

esp_err_t cvm_server_init(void)
{
    ESP_LOGI(TAG, "CVM server initialized");
    return ESP_OK;
}

void cvm_server_start(void)
{
    if (g_running) return;
    g_running = true;

    const tollgate_config_t *cfg = tollgate_config_get();
    const char *relay = (cfg->cvm_relays[0]) ? cfg->cvm_relays : "wss://relay.primal.net";

    ESP_LOGI(TAG, "Starting CVM relay task (free internal: %u, largest: %u)",
             heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));

    char *relay_copy = strdup(relay);
    BaseType_t ret = xTaskCreatePinnedToCore(cvm_relay_task, "cvm_relay", 16384, relay_copy, 5, &g_task, 1);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create cvm_relay task (ret=%d, free internal: %u)",
                 ret, heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
        g_running = false;
        free(relay_copy);
        return;
    }
    ESP_LOGI(TAG, "CVM relay task created on core %d", xPortGetCoreID());
}

void cvm_server_stop(void)
{
    g_running = false;
    if (g_task) {
        vTaskDelay(pdMS_TO_TICKS(500));
        g_task = NULL;
    }
}
