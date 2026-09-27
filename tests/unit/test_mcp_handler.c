#include "test_framework.h"
#include "mcp_handler.h"
#include "config.h"
#include "gps.h"
#include "tollgate_core_session.h"
#include "tollgate_core.h"
#include "nucula_wallet.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

static tollgate_config_t g_test_config;
static uint64_t g_wallet_balance = 0;
static int g_wallet_proof_count = 0;
static int g_wallet_send_rc = 0;
static char g_wallet_send_token[256] = "cashuA_test_token";
static esp_err_t g_wallet_melt_rc = ESP_OK;

const tollgate_config_t *tollgate_config_get(void) {
    return &g_test_config;
}

uint64_t nucula_wallet_balance(void) {
    return g_wallet_balance;
}

int nucula_wallet_proof_count(void) {
    return g_wallet_proof_count;
}

int nucula_wallet_send(uint64_t amount, char *token_out, size_t token_max) {
    (void)amount;
    (void)token_max;
    if (g_wallet_send_rc == 0) {
        strncpy(token_out, g_wallet_send_token, token_max - 1);
    }
    return g_wallet_send_rc;
}

esp_err_t nucula_wallet_melt(const char *bolt11, uint64_t max_fee) {
    (void)bolt11;
    (void)max_fee;
    return g_wallet_melt_rc;
}

static tg_session_t g_test_sessions[TG_SESSION_MAX_CLIENTS];
static int g_test_session_count = 0;

tg_session_t *tollgate_core_session_get_array(void) {
    return g_test_sessions;
}

int tollgate_core_session_get_array_size(void) {
    return TG_SESSION_MAX_CLIENTS;
}

static void test_mcp_parse_tool(void)
{
    printf("\n=== MCP tool parsing ===\n");
    ASSERT_EQ_INT(MCP_TOOL_GET_CONFIG, mcp_parse_tool("get_config"), "get_config");
    ASSERT_EQ_INT(MCP_TOOL_SET_CONFIG, mcp_parse_tool("set_config"), "set_config");
    ASSERT_EQ_INT(MCP_TOOL_GET_BALANCE, mcp_parse_tool("get_balance"), "get_balance");
    ASSERT_EQ_INT(MCP_TOOL_WALLET_SEND, mcp_parse_tool("wallet_send"), "wallet_send");
    ASSERT_EQ_INT(MCP_TOOL_GET_SESSIONS, mcp_parse_tool("get_sessions"), "get_sessions");
    ASSERT_EQ_INT(MCP_TOOL_GET_USAGE, mcp_parse_tool("get_usage"), "get_usage");
    ASSERT_EQ_INT(MCP_TOOL_SET_PAYOUT, mcp_parse_tool("set_payout"), "set_payout");
    ASSERT_EQ_INT(MCP_TOOL_SET_METRIC, mcp_parse_tool("set_metric"), "set_metric");
    ASSERT_EQ_INT(MCP_TOOL_SET_PRICE, mcp_parse_tool("set_price"), "set_price");
    ASSERT_EQ_INT(MCP_TOOL_WALLET_MELT, mcp_parse_tool("wallet_melt"), "wallet_melt");
    ASSERT_EQ_INT(MCP_TOOL_GET_LOCATION, mcp_parse_tool("get_location"), "get_location");
    ASSERT_EQ_INT(MCP_TOOL_UNKNOWN, mcp_parse_tool("foo"), "unknown tool");
    ASSERT_EQ_INT(MCP_TOOL_UNKNOWN, mcp_parse_tool(NULL), "NULL tool");
}

static void test_mcp_get_config(void)
{
    printf("\n=== MCP get_config ===\n");
    memset(&g_test_config, 0, sizeof(g_test_config));
    strncpy(g_test_config.ap_ssid, "TollGate-TEST", sizeof(g_test_config.ap_ssid) - 1);
    strncpy(g_test_config.metric, "bytes", sizeof(g_test_config.metric) - 1);
    g_test_config.price_per_step = 21;
    g_test_config.step_size_ms = 60000;
    g_test_config.step_size_bytes = 22020096;
    strncpy(g_test_config.mint_url, "https://testnut.cashu.space", sizeof(g_test_config.mint_url) - 1);

    mcp_response_t resp = mcp_handle_get_config();
    ASSERT(resp.success, "get_config succeeds");

    cJSON *result = cJSON_Parse(resp.result_json);
    ASSERT(result != NULL, "result is valid JSON");
    ASSERT_EQ_STR("bytes", cJSON_GetObjectItem(result, "metric")->valuestring, "metric=bytes");
    ASSERT_EQ_INT(21, cJSON_GetObjectItem(result, "price_per_step")->valueint, "price=21");
    cJSON_Delete(result);
}

static void test_mcp_set_config(void)
{
    printf("\n=== MCP set_config ===\n");
    memset(&g_test_config, 0, sizeof(g_test_config));
    g_test_config.price_per_step = 21;

    const char *params = "{\"price_per_step\":42,\"metric\":\"milliseconds\"}";
    mcp_response_t resp = mcp_handle_set_config(params);
    ASSERT(resp.success, "set_config succeeds");
    ASSERT_EQ_INT(42, g_test_config.price_per_step, "price updated to 42");
    ASSERT_EQ_STR("milliseconds", g_test_config.metric, "metric updated");

    resp = mcp_handle_set_config("not json");
    ASSERT(!resp.success, "invalid JSON fails");
}

static void test_mcp_get_balance(void)
{
    printf("\n=== MCP get_balance ===\n");
    g_wallet_balance = 500;
    g_wallet_proof_count = 8;

    mcp_response_t resp = mcp_handle_get_balance();
    ASSERT(resp.success, "get_balance succeeds");

    cJSON *result = cJSON_Parse(resp.result_json);
    ASSERT(result != NULL, "result is valid JSON");
    ASSERT_EQ_INT(500, (int)cJSON_GetObjectItem(result, "balance_sats")->valuedouble, "balance=500");
    ASSERT_EQ_INT(8, cJSON_GetObjectItem(result, "proof_count")->valueint, "proofs=8");
    cJSON_Delete(result);
}

static void test_mcp_wallet_send(void)
{
    printf("\n=== MCP wallet_send ===\n");
    g_wallet_send_rc = 0;
    strncpy(g_wallet_send_token, "cashuA_send_test", sizeof(g_wallet_send_token) - 1);

    const char *params = "{\"amount\":21}";
    mcp_response_t resp = mcp_handle_wallet_send(params);
    ASSERT(resp.success, "wallet_send succeeds");

    cJSON *result = cJSON_Parse(resp.result_json);
    ASSERT(result != NULL, "result is valid JSON");
    ASSERT_EQ_STR("cashuA_send_test", cJSON_GetObjectItem(result, "token")->valuestring, "token matches");
    cJSON_Delete(result);

    printf("\n--- wallet_send missing amount ---\n");
    resp = mcp_handle_wallet_send("{}");
    ASSERT(!resp.success, "missing amount fails");

    printf("\n--- wallet_send send fails ---\n");
    g_wallet_send_rc = -1;
    resp = mcp_handle_wallet_send("{\"amount\":100}");
    ASSERT(!resp.success, "send failure reported");
}

static void test_mcp_get_location(void)
{
    printf("\n=== MCP get_location ===\n");
    gps_reset_state();

    mcp_response_t resp = mcp_handle_get_location();
    ASSERT(resp.success, "get_location succeeds without a fix");
    cJSON *result = cJSON_Parse(resp.result_json);
    ASSERT(result != NULL, "result is valid JSON");
    ASSERT(!cJSON_IsTrue(cJSON_GetObjectItem(result, "valid")), "no fix -> valid=false");
    cJSON_Delete(result);

    /* Install a real NMEA fix and query again. */
    ASSERT(gps_feed_line("$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47"),
           "NMEA fix accepted");
    ASSERT(gps_feed_line("$GNRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*74"),
           "NMEA RMC accepted");

    resp = mcp_handle_get_location();
    ASSERT(resp.success, "get_location succeeds with a fix");
    result = cJSON_Parse(resp.result_json);
    ASSERT(result != NULL, "result is valid JSON");
    ASSERT(cJSON_IsTrue(cJSON_GetObjectItem(result, "valid")), "valid=true");
    ASSERT(fabs(cJSON_GetObjectItem(result, "latitude")->valuedouble - 48.1173) < 1e-4,
           "latitude 48.1173");
    ASSERT(fabs(cJSON_GetObjectItem(result, "longitude")->valuedouble - 11.5167) < 1e-4,
           "longitude 11.5167");
    ASSERT_EQ_STR("u281wp6pt", cJSON_GetObjectItem(result, "geohash")->valuestring,
                  "geohash for the fix");
    ASSERT_EQ_INT(8, cJSON_GetObjectItem(result, "satellites")->valueint, "8 satellites");
    ASSERT(fabs(cJSON_GetObjectItem(result, "altitude_m")->valuedouble - 545.4) < 1e-4,
           "altitude 545.4");
    cJSON_Delete(result);

    /* Dispatch path. */
    mcp_request_t req = {0};
    req.tool = MCP_TOOL_GET_LOCATION;
    strncpy(req.method, "get_location", sizeof(req.method) - 1);
    mcp_response_t dresp = mcp_dispatch(&req);
    ASSERT(dresp.success, "dispatch get_location succeeds");
    cJSON *dresult = cJSON_Parse(dresp.result_json);
    ASSERT(dresult != NULL, "dispatch result is valid JSON");
    ASSERT(cJSON_IsTrue(cJSON_GetObjectItem(dresult, "valid")), "dispatch valid=true");
    cJSON_Delete(dresult);

    gps_reset_state();
}

static void test_mcp_dispatch(void)
{
    printf("\n=== MCP dispatch ===\n");
    mcp_request_t req = {0};
    req.tool = MCP_TOOL_UNKNOWN;
    strncpy(req.method, "bogus", sizeof(req.method) - 1);
    mcp_response_t resp = mcp_dispatch(&req);
    ASSERT(!resp.success, "unknown tool dispatch fails");

    resp = mcp_dispatch(NULL);
    ASSERT(!resp.success, "NULL request dispatch fails");
}

static void test_mcp_get_sessions(void)
{
    printf("\n=== MCP get_sessions ===\n");
    memset(g_test_sessions, 0, sizeof(g_test_sessions));

    mcp_response_t resp = mcp_handle_get_sessions();
    ASSERT(resp.success, "get_sessions succeeds");
    cJSON *result = cJSON_Parse(resp.result_json);
    ASSERT(result != NULL, "result is valid JSON array");
    ASSERT(cJSON_IsArray(result), "result is an array");
    ASSERT_EQ_INT(0, cJSON_GetArraySize(result), "empty sessions");
    cJSON_Delete(result);

    g_test_sessions[0].active = true;
    g_test_sessions[0].client_ip = 0x0100000A;
    strncpy(g_test_sessions[0].mac, "AA:BB:CC:DD:EE:FF", sizeof(g_test_sessions[0].mac) - 1);
    g_test_sessions[0].allotment_ms = 60000;

    resp = mcp_handle_get_sessions();
    ASSERT(resp.success, "get_sessions with data succeeds");
    result = cJSON_Parse(resp.result_json);
    ASSERT_EQ_INT(1, cJSON_GetArraySize(result), "one active session");
    cJSON *s = cJSON_GetArrayItem(result, 0);
    ASSERT_EQ_STR("AA:BB:CC:DD:EE:FF", cJSON_GetObjectItem(s, "mac")->valuestring, "mac matches");
    cJSON_Delete(result);
    g_test_sessions[0].active = false;
}

static void test_mcp_get_usage(void)
{
    printf("\n=== MCP get_usage ===\n");
    memset(&g_test_config, 0, sizeof(g_test_config));
    strncpy(g_test_config.metric, "milliseconds", sizeof(g_test_config.metric) - 1);
    g_test_config.price_per_step = 21;
    g_test_config.step_size_ms = 60000;
    g_test_config.step_size_bytes = 22020096;

    mcp_response_t resp = mcp_handle_get_usage();
    ASSERT(resp.success, "get_usage succeeds");
    cJSON *result = cJSON_Parse(resp.result_json);
    ASSERT(result != NULL, "result is valid JSON");
    ASSERT_EQ_STR("milliseconds", cJSON_GetObjectItem(result, "metric")->valuestring, "metric matches");
    ASSERT_EQ_INT(21, cJSON_GetObjectItem(result, "price_per_step")->valueint, "price matches");
    cJSON_Delete(result);
}

static void test_mcp_set_payout(void)
{
    printf("\n=== MCP set_payout ===\n");
    memset(&g_test_config, 0, sizeof(g_test_config));

    const char *params = "{\"enabled\":true,\"recipients\":[{\"lightning_address\":\"test@coinos.io\",\"factor\":0.5}]}";
    mcp_response_t resp = mcp_handle_set_payout(params);
    ASSERT(resp.success, "set_payout succeeds");
    ASSERT(g_test_config.payout.enabled, "payout enabled");
    ASSERT_EQ_INT(1, g_test_config.payout.recipient_count, "1 recipient");
    ASSERT_EQ_STR("test@coinos.io", g_test_config.payout.recipients[0].lightning_address, "address matches");

    resp = mcp_handle_set_payout("not json");
    ASSERT(!resp.success, "invalid JSON fails");
}

static void test_mcp_set_metric(void)
{
    printf("\n=== MCP set_metric ===\n");
    memset(&g_test_config, 0, sizeof(g_test_config));

    mcp_response_t resp = mcp_handle_set_metric("{\"metric\":\"bytes\"}");
    ASSERT(resp.success, "set_metric bytes succeeds");
    ASSERT_EQ_STR("bytes", g_test_config.metric, "metric updated to bytes");

    resp = mcp_handle_set_metric("{\"metric\":\"milliseconds\"}");
    ASSERT(resp.success, "set_metric milliseconds succeeds");
    ASSERT_EQ_STR("milliseconds", g_test_config.metric, "metric updated to milliseconds");

    resp = mcp_handle_set_metric("{\"metric\":\"invalid\"}");
    ASSERT(!resp.success, "invalid metric rejected");

    resp = mcp_handle_set_metric("{}");
    ASSERT(!resp.success, "missing metric rejected");
}

static void test_mcp_set_price(void)
{
    printf("\n=== MCP set_price ===\n");
    memset(&g_test_config, 0, sizeof(g_test_config));
    g_test_config.price_per_step = 21;

    mcp_response_t resp = mcp_handle_set_price("{\"price_per_step\":50}");
    ASSERT(resp.success, "set_price succeeds");
    ASSERT_EQ_INT(50, g_test_config.price_per_step, "price updated to 50");

    resp = mcp_handle_set_price("{\"price_per_step\":0}");
    ASSERT(!resp.success, "zero price rejected");

    resp = mcp_handle_set_price("{}");
    ASSERT(!resp.success, "missing price rejected");
}

static void test_mcp_wallet_melt(void)
{
    printf("\n=== MCP wallet_melt ===\n");
    g_wallet_melt_rc = ESP_OK;

    mcp_response_t resp = mcp_handle_wallet_melt("{\"bolt11\":\"lnbc100n1...\"}");
    ASSERT(resp.success, "wallet_melt succeeds");

    g_wallet_melt_rc = ESP_FAIL;
    resp = mcp_handle_wallet_melt("{\"bolt11\":\"lnbc100n1...\"}");
    ASSERT(!resp.success, "melt failure reported");

    resp = mcp_handle_wallet_melt("{}");
    ASSERT(!resp.success, "missing bolt11 fails");
}

int main(void)
{
    printf("=== test_mcp_handler ===\n");
    test_mcp_parse_tool();
    test_mcp_get_config();
    test_mcp_set_config();
    test_mcp_get_balance();
    test_mcp_wallet_send();
    test_mcp_get_sessions();
    test_mcp_get_usage();
    test_mcp_set_payout();
    test_mcp_set_metric();
    test_mcp_set_price();
    test_mcp_wallet_melt();
    test_mcp_get_location();
    test_mcp_dispatch();
    TEST_SUMMARY();
}
