/*
 * test_gsm_upstream.c — host tests for the GSM/LTE upstream AT core (CTG-1).
 *
 * Everything here runs against main/gsm_upstream.c compiled with -DTEST_HOST,
 * i.e. the protocol core only.  No modem, no UART, no PPP: a line of text in,
 * a decision out.  The target-side glue (power/UART/AT transport) is not
 * covered here and has to be exercised on hardware — see
 * docs/GSM-UPSTREAM-INTEGRATION.md.
 */
#include "test_framework.h"
#include "../../main/gsm_upstream.h"

#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------------------- */
/* helpers                                                                   */
/* ------------------------------------------------------------------------- */

/* Advance the machine by issuing the next command and replying with `reply`
 * (NULL = modem stays silent).  Returns whether the state machine advanced. */
static bool step(gsm_ctx_t *ctx, const char *reply)
{
    (void)gsm_next_command(ctx);
    return (reply != NULL) ? gsm_feed_response(ctx, reply) : false;
}

/* Bring the machine to the point where it has just accepted registration on
 * CREG.  Returns true on success. */
static bool bring_to_creg_ok(gsm_ctx_t *ctx)
{
    step(ctx, "OK");                 /* AT        -> SIM_CHECK */
    step(ctx, "+CPIN: READY");       /* +CPIN?    -> SIGNAL    */
    step(ctx, "+CSQ: 20,0");         /* +CSQ      -> REG_CREG  */
    step(ctx, "+CREG: 0,1");         /* +CREG?    -> REG_CEREG / PDP_SETUP */
    return ctx->state == GSM_ST_REG_CEREG || ctx->state == GSM_ST_PDP_SETUP;
}

int main(void)
{
    printf("=== test_gsm_upstream ===\n");

    /* --------------------------------------------------------------------- */
    printf("\n-- response classification --\n");
    ASSERT_EQ_INT(GSM_RESP_OK, gsm_classify_response("OK"), "OK");
    ASSERT_EQ_INT(GSM_RESP_OK, gsm_classify_response("ok"), "lowercase ok");
    ASSERT_EQ_INT(GSM_RESP_OK, gsm_classify_response("  OK \r\n"), "OK with padding");
    ASSERT_EQ_INT(GSM_RESP_ERROR, gsm_classify_response("ERROR"), "ERROR");
    ASSERT_EQ_INT(GSM_RESP_CONNECT, gsm_classify_response("CONNECT"), "CONNECT");
    ASSERT_EQ_INT(GSM_RESP_CONNECT, gsm_classify_response("CONNECT 115200"), "CONNECT <baud>");
    ASSERT_EQ_INT(GSM_RESP_NO_CARRIER, gsm_classify_response("NO CARRIER"), "NO CARRIER");
    ASSERT_EQ_INT(GSM_RESP_BUSY, gsm_classify_response("BUSY"), "BUSY");
    ASSERT_EQ_INT(GSM_RESP_NO_DIALTONE, gsm_classify_response("NO DIALTONE"), "NO DIALTONE");
    ASSERT_EQ_INT(GSM_RESP_CME_ERROR, gsm_classify_response("+CME ERROR: 10"), "+CME ERROR");
    ASSERT_EQ_INT(GSM_RESP_CMS_ERROR, gsm_classify_response("+CMS ERROR: 500"), "+CMS ERROR");
    ASSERT_EQ_INT(GSM_RESP_UNKNOWN, gsm_classify_response("+CSQ: 17,0"), "info line is not final");
    ASSERT_EQ_INT(GSM_RESP_UNKNOWN, gsm_classify_response("RDY"), "RDY is not final");
    ASSERT_EQ_INT(GSM_RESP_UNKNOWN, gsm_classify_response(""), "empty line");

    ASSERT_EQ_INT(10, gsm_response_error_code("+CME ERROR: 10"), "CME 10 parsed");
    ASSERT_EQ_INT(500, gsm_response_error_code("+CMS ERROR: 500"), "CMS 500 parsed");
    ASSERT_EQ_INT(-1, gsm_response_error_code("OK"), "no error code in OK");
    ASSERT_EQ_INT(-1, gsm_response_error_code("+CME ERROR: not inserted"), "non-numeric CME");
    ASSERT_EQ_STR("CONNECT", gsm_response_name(GSM_RESP_CONNECT), "response name CONNECT");
    ASSERT_EQ_STR("UNKNOWN", gsm_response_name(GSM_RESP_UNKNOWN), "response name UNKNOWN");

    /* --------------------------------------------------------------------- */
    printf("\n-- SIM state (AT+CPIN?) --\n");
    gsm_sim_state_t sim = GSM_SIM_UNKNOWN;
    ASSERT(gsm_parse_cpin("+CPIN: READY", &sim) && sim == GSM_SIM_READY, "+CPIN: READY");
    ASSERT(gsm_parse_cpin("+CPIN: SIM PIN", &sim) && sim == GSM_SIM_PIN_REQUIRED, "+CPIN: SIM PIN");
    ASSERT(gsm_parse_cpin("+CPIN: SIM PIN2", &sim) && sim == GSM_SIM_PIN_REQUIRED, "+CPIN: SIM PIN2");
    ASSERT(gsm_parse_cpin("+CPIN: SIM PUK", &sim) && sim == GSM_SIM_PUK_REQUIRED, "+CPIN: SIM PUK");
    ASSERT(gsm_parse_cpin("+CPIN: SIM BUSY", &sim) && sim == GSM_SIM_BUSY, "+CPIN: SIM BUSY");
    ASSERT(gsm_parse_cpin("+CME ERROR: 10", &sim) && sim == GSM_SIM_NOT_INSERTED, "CME 10 -> not inserted");
    ASSERT(gsm_parse_cpin("+CME ERROR: 11", &sim) && sim == GSM_SIM_PIN_REQUIRED, "CME 11 -> PIN");
    ASSERT(gsm_parse_cpin("+CME ERROR: 12", &sim) && sim == GSM_SIM_PUK_REQUIRED, "CME 12 -> PUK");
    ASSERT(gsm_parse_cpin("+CME ERROR: 14", &sim) && sim == GSM_SIM_BUSY, "CME 14 -> busy");
    ASSERT(gsm_parse_cpin("+CME ERROR: 13", &sim) && sim == GSM_SIM_UNKNOWN, "CME 13 -> unknown");
    ASSERT(!gsm_parse_cpin("OK", &sim), "OK is not a CPIN reply");
    ASSERT_EQ_STR("READY", gsm_sim_state_name(GSM_SIM_READY), "sim name READY");

    /* --------------------------------------------------------------------- */
    printf("\n-- signal quality (AT+CSQ) --\n");
    int rssi = -1, ber = -1;
    ASSERT(gsm_parse_csq("+CSQ: 17,0", &rssi, &ber) && rssi == 17 && ber == 0, "+CSQ: 17,0");
    ASSERT(gsm_parse_csq("+CSQ: 99,99", &rssi, &ber) && rssi == 99 && ber == 99, "unknown signal");
    ASSERT(gsm_parse_csq("+CSQ: 20", &rssi, &ber) && rssi == 20 && ber == 99, "ber defaults to 99");
    ASSERT(!gsm_parse_csq("OK", &rssi, &ber), "OK is not a CSQ reply");

    ASSERT_EQ_INT(-113, gsm_rssi_dbm(0), "rssi 0 -> -113 dBm");
    ASSERT_EQ_INT(-111, gsm_rssi_dbm(1), "rssi 1 -> -111 dBm");
    ASSERT_EQ_INT(-83, gsm_rssi_dbm(15), "rssi 15 -> -83 dBm");
    ASSERT_EQ_INT(-51, gsm_rssi_dbm(31), "rssi 31 -> -51 dBm");
    ASSERT_EQ_INT(GSM_RSSI_UNKNOWN_DBM, gsm_rssi_dbm(99), "rssi 99 -> unknown");
    ASSERT_EQ_INT(GSM_RSSI_UNKNOWN_DBM, gsm_rssi_dbm(32), "rssi 32 out of range");

    ASSERT_EQ_INT(1, gsm_rssi_bars(0), "rssi 0 -> 1 bar");
    ASSERT_EQ_INT(2, gsm_rssi_bars(5), "rssi 5 -> 2 bars");
    ASSERT_EQ_INT(3, gsm_rssi_bars(10), "rssi 10 -> 3 bars");
    ASSERT_EQ_INT(4, gsm_rssi_bars(15), "rssi 15 -> 4 bars");
    ASSERT_EQ_INT(5, gsm_rssi_bars(20), "rssi 20 -> 5 bars");
    ASSERT_EQ_INT(5, gsm_rssi_bars(31), "rssi 31 -> 5 bars");
    ASSERT_EQ_INT(0, gsm_rssi_bars(99), "unknown rssi -> 0 bars");

    /* --------------------------------------------------------------------- */
    printf("\n-- registration (+CREG/+CGREG/+CEREG) --\n");
    gsm_reg_state_t reg = GSM_REG_UNKNOWN;
    int area = -99;
    ASSERT(gsm_parse_reg("+CREG: 0,1", &reg, &area) && reg == GSM_REG_HOME, "+CREG: 0,1 -> home");
    ASSERT_EQ_INT(-1, area, "no lac in 2-param reply");
    ASSERT(gsm_parse_reg("+CGREG: 0,5", &reg, &area) && reg == GSM_REG_ROAMING, "+CGREG: 0,5 -> roaming");
    ASSERT(gsm_parse_reg("+CEREG: 0,1", &reg, &area) && reg == GSM_REG_HOME, "+CEREG: 0,1 -> home");
    ASSERT(gsm_parse_reg("+CREG: 0,2", &reg, &area) && reg == GSM_REG_SEARCHING, "searching");
    ASSERT(gsm_parse_reg("+CREG: 0,3", &reg, &area) && reg == GSM_REG_DENIED, "denied");
    ASSERT(gsm_parse_reg("+CREG: 1", &reg, &area) && reg == GSM_REG_HOME, "unsolicited 1-param form");
    ASSERT(gsm_parse_reg("+CREG: 0,1,6789,ABCD", &reg, &area) && area == 6789, "lac parsed");
    ASSERT(gsm_parse_reg("+CREG: 0,1,\"1A2B\",\"0FFF\"", &reg, &area) && area == -1, "quoted lac skipped");
    ASSERT(!gsm_parse_reg("OK", &reg, &area), "OK is not a reg reply");

    ASSERT(gsm_reg_is_registered(GSM_REG_HOME), "home is registered");
    ASSERT(gsm_reg_is_registered(GSM_REG_ROAMING), "roaming is registered");
    ASSERT(!gsm_reg_is_registered(GSM_REG_SEARCHING), "searching is not registered");
    ASSERT(!gsm_reg_is_registered(GSM_REG_HOME_SMS_ONLY), "SMS-only is not data-registered");
    ASSERT_EQ_STR("denied", gsm_reg_state_name(GSM_REG_DENIED), "reg name denied");

    /* --------------------------------------------------------------------- */
    printf("\n-- operator + access technology --\n");
    char oper[32] = "";
    int act = -1;
    ASSERT(gsm_parse_cops("+COPS: 0,0,\"SilentLink\",7", oper, sizeof oper, &act), "+COPS parsed");
    ASSERT_EQ_STR("SilentLink", oper, "operator name");
    ASSERT_EQ_INT(7, act, "act LTE");
    ASSERT(gsm_parse_cops("+COPS: 0,2,26201,7", oper, sizeof oper, &act), "numeric operator");
    ASSERT_EQ_STR("", oper, "numeric format leaves name empty");
    ASSERT_EQ_INT(7, act, "numeric format act");
    ASSERT(gsm_parse_cops("+COPS: 0,0,\"Vodafone.de\",0", oper, sizeof oper, &act) && act == 0, "2G act");
    ASSERT(!gsm_parse_cops("+COPS: 0", oper, sizeof oper, &act), "+COPS: 0 alone is not a reply");
    ASSERT(!gsm_parse_cops("OK", oper, sizeof oper, &act), "OK is not a COPS reply");

    ASSERT(gsm_act_is_2g(GSM_ACT_GSM), "GSM is 2G");
    ASSERT(gsm_act_is_2g(GSM_ACT_EDGE), "EDGE is 2G");
    ASSERT(gsm_act_is_2g(GSM_ACT_GSM_COMPACT), "GSM compact is 2G");
    ASSERT(!gsm_act_is_2g(GSM_ACT_UTRAN), "UMTS is not 2G");
    ASSERT(!gsm_act_is_2g(GSM_ACT_EUTRAN), "LTE is not 2G");
    ASSERT(!gsm_act_is_2g(GSM_ACT_EUTRAN_NB), "NB-IoT is not 2G");
    ASSERT_EQ_STR("LTE (4G)", gsm_act_name(GSM_ACT_EUTRAN), "act name LTE");

    /* --------------------------------------------------------------------- */
    printf("\n-- APN validation --\n");
    ASSERT(gsm_apn_valid("silent.link"), "silent.link valid");
    ASSERT(gsm_apn_valid("iot.1nce.net"), "dotted APN valid");
    ASSERT(gsm_apn_valid("a.b-c_d"), "hyphen/underscore valid");
    ASSERT(!gsm_apn_valid(""), "empty APN rejected");
    ASSERT(!gsm_apn_valid(NULL), "NULL APN rejected");
    ASSERT(!gsm_apn_valid("12345"), "all-numeric APN rejected");
    ASSERT(!gsm_apn_valid("bad apn"), "space rejected");
    ASSERT(!gsm_apn_valid("apn;AT+CFUN=1"), "AT injection rejected");
    ASSERT(!gsm_apn_valid("\"quoted\""), "quotes rejected");
    ASSERT(!gsm_apn_valid(".leading"), "leading dot rejected");
    ASSERT(!gsm_apn_valid("trailing."), "trailing dot rejected");
    ASSERT(!gsm_apn_valid("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
           "63+ chars rejected");

    /* --------------------------------------------------------------------- */
    printf("\n-- PDP context / dial string --\n");
    char buf[96];
    size_t n = gsm_build_cgdcont(1, GSM_SILENTLINK_APN, buf, sizeof buf);
    ASSERT(n > 0, "cgdcont built");
    ASSERT_EQ_STR("AT+CGDCONT=1,\"IP\",\"silent.link\"", buf, "cgdcont content");
    ASSERT_EQ_INT(0, (int)gsm_build_cgdcont(0, "silent.link", buf, sizeof buf), "cid 0 rejected");
    ASSERT_EQ_INT(0, (int)gsm_build_cgdcont(1, "bad apn", buf, sizeof buf), "bad APN rejected");
    ASSERT_EQ_INT(0, (int)gsm_build_cgdcont(1, "silent.link", buf, 8), "short buffer rejected");
    ASSERT_EQ_STR("", buf, "short buffer left empty");
    ASSERT_EQ_STR("ATD*99***1#", gsm_dial_command(1), "dial string cid 1");
    ASSERT_EQ_STR("ATD*99***3#", gsm_dial_command(3), "dial string cid 3");
    ASSERT_EQ_STR("ATD*99***1#", gsm_dial_command(0), "dial string default cid");

    /* --------------------------------------------------------------------- */
    printf("\n-- context init --\n");
    gsm_ctx_t ctx;
    gsm_ctx_init(&ctx, NULL, 0);
    ASSERT_EQ_INT(GSM_ST_IDLE, ctx.state, "fresh context is IDLE");
    ASSERT_EQ_STR(GSM_SILENTLINK_APN, ctx.apn, "default APN is silent.link");
    ASSERT_EQ_INT(GSM_DEFAULT_CID, ctx.cid, "default cid");
    ASSERT_EQ_INT(99, ctx.rssi, "rssi starts unknown");
    ASSERT_EQ_STR("IDLE", gsm_state_name(ctx.state), "state name IDLE");

    gsm_ctx_init(&ctx, "iot.1nce.net", 4);
    ASSERT_EQ_STR("iot.1nce.net", ctx.apn, "custom APN kept");
    ASSERT_EQ_INT(4, ctx.cid, "custom cid kept");

    gsm_ctx_init(&ctx, "bad apn", 1);
    ASSERT_EQ_INT(GSM_ST_ERROR, ctx.state, "invalid APN is terminal at init");
    ASSERT_EQ_INT(GSM_FAIL_PDP_REJECTED, ctx.failure, "invalid APN -> PDP_REJECTED failure");
    ASSERT(gsm_next_command(&ctx) == NULL, "terminal context issues no command");

    /* --------------------------------------------------------------------- */
    printf("\n-- happy path: 2G-only modem (SIM800 class, no CEREG) --\n");
    gsm_ctx_init(&ctx, NULL, 0);
    ASSERT_EQ_STR("AT", gsm_next_command(&ctx), "first command is AT");
    ASSERT_EQ_INT(GSM_ST_AT_PROBE, ctx.state, "AT issued");
    ASSERT(gsm_feed_response(&ctx, "OK"), "OK advances past probe");
    ASSERT_EQ_INT(GSM_ST_SIM_CHECK, ctx.state, "state SIM_CHECK");
    ASSERT_EQ_STR("AT+CPIN?", gsm_next_command(&ctx), "CPIN query");
    ASSERT(gsm_feed_response(&ctx, "+CPIN: READY"), "SIM ready advances");
    ASSERT_EQ_INT(GSM_ST_SIGNAL, ctx.state, "state SIGNAL");
    ASSERT_EQ_STR("AT+CSQ", gsm_next_command(&ctx), "CSQ query");
    ASSERT(gsm_feed_response(&ctx, "+CSQ: 20,0"), "signal read advances");
    ASSERT_EQ_INT(20, ctx.rssi, "rssi recorded");
    ASSERT_EQ_INT(0, ctx.ber, "ber recorded");
    ASSERT_EQ_STR("AT+CREG?", gsm_next_command(&ctx), "CREG query");
    ASSERT(gsm_feed_response(&ctx, "+CREG: 0,1"), "registered advances");
    ASSERT_EQ_INT(GSM_ST_REG_CEREG, ctx.state, "state REG_CEREG");
    ASSERT_EQ_STR("AT+CEREG?", gsm_next_command(&ctx), "CEREG query");
    ASSERT(gsm_feed_response(&ctx, "ERROR"), "CEREG ERROR tolerated");
    ASSERT(ctx.lte_reg_unsupported, "CEREG marked unsupported");
    ASSERT_EQ_INT(GSM_ST_PDP_SETUP, ctx.state, "state PDP_SETUP");
    ASSERT_EQ_STR("AT+CGDCONT=1,\"IP\",\"silent.link\"", gsm_next_command(&ctx), "CGDCONT built");
    ASSERT(gsm_feed_response(&ctx, "OK"), "PDP context accepted");
    ASSERT_EQ_INT(GSM_ST_DIAL, ctx.state, "state DIAL");
    ASSERT_EQ_STR("ATD*99***1#", gsm_next_command(&ctx), "dial issued");
    ASSERT(gsm_feed_response(&ctx, "CONNECT"), "CONNECT reaches data mode");
    ASSERT_EQ_INT(GSM_ST_PPP_UP, ctx.state, "state PPP_UP");
    ASSERT(gsm_is_up(&ctx), "context reports up");
    ASSERT(gsm_next_command(&ctx) == NULL, "no AT commands once PPP is up");
    ASSERT_EQ_INT(GSM_REG_HOME, ctx.reg_2g, "registration recorded");
    ASSERT_EQ_INT(GSM_SIM_READY, ctx.sim, "SIM recorded");

    /* --------------------------------------------------------------------- */
    printf("\n-- happy path: LTE modem (CEREG supported) --\n");
    gsm_ctx_init(&ctx, NULL, 0);
    ASSERT(bring_to_creg_ok(&ctx), "reached CREG-registered");
    ASSERT_EQ_INT(GSM_ST_REG_CEREG, ctx.state, "LTE path goes through CEREG");
    ASSERT_EQ_STR("AT+CEREG?", gsm_next_command(&ctx), "CEREG query on LTE path");
    ASSERT(gsm_feed_response(&ctx, "+CEREG: 0,1"), "LTE registered");
    ASSERT_EQ_INT(GSM_ST_PDP_SETUP, ctx.state, "LTE -> PDP_SETUP");
    ASSERT_EQ_INT(GSM_REG_HOME, ctx.reg_lte, "LTE registration recorded");
    ASSERT(gsm_feed_response(&ctx, "OK"), "PDP ok");
    ASSERT(gsm_feed_response(&ctx, "CONNECT"), "dial connect");
    ASSERT(gsm_is_up(&ctx), "LTE path up");

    /* --------------------------------------------------------------------- */
    printf("\n-- failure paths --\n");
    gsm_ctx_init(&ctx, NULL, 0);
    step(&ctx, "ERROR");
    ASSERT_EQ_INT(GSM_ST_ERROR, ctx.state, "AT ERROR is terminal");
    ASSERT_EQ_INT(GSM_FAIL_NO_RESPONSE, ctx.failure, "AT ERROR -> NO_RESPONSE");

    gsm_ctx_init(&ctx, NULL, 0);
    step(&ctx, "OK");
    step(&ctx, "+CME ERROR: 10");
    ASSERT_EQ_INT(GSM_FAIL_SIM_NOT_INSERTED, ctx.failure, "CME 10 during CPIN -> not inserted");

    gsm_ctx_init(&ctx, NULL, 0);
    step(&ctx, "OK");
    step(&ctx, "+CPIN: SIM PIN");
    ASSERT_EQ_INT(GSM_FAIL_SIM_PIN, ctx.failure, "locked SIM -> SIM_PIN");

    gsm_ctx_init(&ctx, NULL, 0);
    step(&ctx, "OK");
    ASSERT(!step(&ctx, "+CPIN: SIM BUSY"), "SIM busy does not advance");
    ASSERT_EQ_INT(GSM_ST_SIM_CHECK, ctx.state, "still waiting for the SIM");
    ASSERT_EQ_STR("AT+CPIN?", gsm_next_command(&ctx), "CPIN re-issued while busy");

    gsm_ctx_init(&ctx, NULL, 0);
    step(&ctx, "OK");
    step(&ctx, "+CPIN: READY");
    step(&ctx, "+CSQ: 12,0");
    step(&ctx, "+CREG: 0,3");
    ASSERT_EQ_INT(GSM_FAIL_REG_DENIED, ctx.failure, "CREG 3 -> denied");

    gsm_ctx_init(&ctx, NULL, 0);
    step(&ctx, "OK");
    step(&ctx, "+CPIN: READY");
    step(&ctx, "+CSQ: 12,0");
    ASSERT(!step(&ctx, "+CREG: 0,2"), "searching does not advance");
    ASSERT_EQ_INT(GSM_ST_REG_CREG, ctx.state, "still in REG_CREG");
    ASSERT_EQ_STR("AT+CREG?", gsm_next_command(&ctx), "CREG re-issued while searching");
    step(&ctx, "+CREG: 0,1");            /* finally registered */
    step(&ctx, "ERROR");                 /* CEREG unsupported */
    step(&ctx, "ERROR");                 /* CGDCONT refused */
    ASSERT_EQ_INT(GSM_FAIL_PDP_REJECTED, ctx.failure, "CGDCONT ERROR -> PDP_REJECTED");

    gsm_ctx_init(&ctx, NULL, 0);
    step(&ctx, "OK");
    step(&ctx, "+CPIN: READY");
    step(&ctx, "+CSQ: 12,0");
    step(&ctx, "+CREG: 0,1");
    step(&ctx, "ERROR");                 /* CEREG unsupported */
    step(&ctx, "+CME ERROR: 3");         /* CGDCONT refused by operator */
    ASSERT_EQ_INT(GSM_FAIL_OPERATOR_REFUSED, ctx.failure, "CME 3 during PDP -> operator refused");

    gsm_ctx_init(&ctx, NULL, 0);
    step(&ctx, "OK");
    step(&ctx, "+CPIN: READY");
    step(&ctx, "+CSQ: 12,0");
    step(&ctx, "+CREG: 0,1");
    step(&ctx, "ERROR");
    step(&ctx, "OK");
    step(&ctx, "NO CARRIER");
    ASSERT_EQ_INT(GSM_FAIL_DIAL_FAILED, ctx.failure, "NO CARRIER -> dial failed");

    /* --------------------------------------------------------------------- */
    printf("\n-- retry budget --\n");
    gsm_ctx_init(&ctx, NULL, 0);
    ctx.max_attempts = 2;
    step(&ctx, "OK");
    step(&ctx, "+CPIN: READY");
    step(&ctx, "+CSQ: 12,0");
    ASSERT_EQ_STR("AT+CREG?", gsm_next_command(&ctx), "CREG attempt 1");
    gsm_feed_response(&ctx, "+CREG: 0,2");
    ASSERT(gsm_next_command(&ctx) != NULL, "CREG attempt 2 within budget");
    gsm_feed_response(&ctx, "+CREG: 0,2");
    ASSERT(gsm_next_command(&ctx) == NULL, "budget exhausted at CREG");
    ASSERT_EQ_INT(GSM_ST_ERROR, ctx.state, "retry exhaustion is terminal");
    ASSERT_EQ_INT(GSM_FAIL_NO_COVERAGE, ctx.failure, "retry exhaustion -> NO_COVERAGE");

    gsm_ctx_init(&ctx, NULL, 0);
    ctx.max_attempts = 1;
    step(&ctx, "OK");                    /* AT        attempt 1 */
    ASSERT_EQ_INT(GSM_ST_SIM_CHECK, ctx.state, "probe ok");
    step(&ctx, "+CPIN: READY");
    step(&ctx, "+CSQ: 12,0");
    step(&ctx, "+CREG: 0,1");
    step(&ctx, "ERROR");                 /* CEREG unsupported */
    step(&ctx, "OK");                    /* CGDCONT  */
    ASSERT_EQ_INT(GSM_ST_DIAL, ctx.state, "at DIAL");
    ASSERT_EQ_STR("ATD*99***1#", gsm_next_command(&ctx), "dial attempt 1");
    ASSERT(gsm_next_command(&ctx) == NULL, "silent modem exhausts DIAL budget");
    ASSERT_EQ_INT(GSM_FAIL_DIAL_FAILED, ctx.failure, "silent dial -> DIAL_FAILED");

    /* --------------------------------------------------------------------- */
    printf("\n-- unsolicited noise is ignored --\n");
    gsm_ctx_init(&ctx, NULL, 0);
    gsm_next_command(&ctx);
    ASSERT(!gsm_feed_response(&ctx, "RDY"), "RDY does not advance");
    ASSERT(!gsm_feed_response(&ctx, ""), "empty line does not advance");
    ASSERT_EQ_INT(GSM_ST_AT_PROBE, ctx.state, "still probing");
    gsm_feed_response(&ctx, "OK");
    gsm_next_command(&ctx);
    ASSERT(!gsm_feed_response(&ctx, "+CREG: 0,1"), "unexpected CREG ignored in SIM_CHECK");
    ASSERT_EQ_INT(GSM_ST_SIM_CHECK, ctx.state, "state unchanged by noise");

    /* --------------------------------------------------------------------- */
    printf("\n-- transcript diagnosis --\n");
    ASSERT_EQ_INT(GSM_FAIL_SIM_NOT_INSERTED,
                  gsm_diagnose("AT+CPIN?\r\n+CME ERROR: 10\r\n"),
                  "diagnose SIM not inserted");
    ASSERT_EQ_INT(GSM_FAIL_SIM_PIN,
                  gsm_diagnose("AT+CPIN?\r\n+CPIN: SIM PIN\r\n"),
                  "diagnose PIN required");
    ASSERT_EQ_INT(GSM_FAIL_REG_DENIED,
                  gsm_diagnose("AT+CREG?\r\n+CREG: 0,3\r\n"),
                  "diagnose registration denied");
    ASSERT_EQ_INT(GSM_FAIL_AUTH_FAILED,
                  gsm_diagnose("ATD*99***1#\r\nCHAP authentication failed\r\nERROR\r\n"),
                  "diagnose auth failure");
    ASSERT_EQ_INT(GSM_FAIL_DIAL_FAILED,
                  gsm_diagnose("ATD*99***1#\r\nNO CARRIER\r\n"),
                  "diagnose no carrier");
    ASSERT_EQ_INT(GSM_FAIL_PDP_REJECTED,
                  gsm_diagnose("AT+CGDCONT=1,\"IP\",\"silent.link\"\r\nERROR\r\n"),
                  "diagnose PDP rejected");
    ASSERT_EQ_INT(GSM_FAIL_OPERATOR_REFUSED,
                  gsm_diagnose("AT+CGDCONT=1,\"IP\",\"silent.link\"\r\n+CME ERROR: 3\r\n"),
                  "diagnose operator refused");
    ASSERT_EQ_INT(GSM_FAIL_NONE,
                  gsm_diagnose("AT\r\nOK\r\n+CREG: 0,1\r\nATD*99***1#\r\nCONNECT\r\n"),
                  "healthy transcript -> no failure");
    ASSERT_EQ_INT(GSM_FAIL_2G_ONLY,
                  gsm_diagnose("+COPS: 0,0,\"Vodafone.de\",0\r\nATD*99***1#\r\nCONNECT\r\n"),
                  "2G-only registration is flagged");
    ASSERT_EQ_INT(GSM_FAIL_NO_RESPONSE, gsm_diagnose(""), "empty transcript -> no response");
    ASSERT_EQ_INT(GSM_FAIL_NO_RESPONSE, gsm_diagnose(NULL), "NULL transcript -> no response");
    ASSERT_EQ_INT(GSM_FAIL_NO_COVERAGE,
                  gsm_diagnose("AT\r\nOK\r\n+CSQ: 12,0\r\n+CREG: 0,2\r\n"),
                  "searching transcript -> no coverage");
    ASSERT_EQ_STR("no coverage / not registered", gsm_failure_name(GSM_FAIL_NO_COVERAGE),
                  "failure name text");

    TEST_SUMMARY();
    return g_tests_failed == 0 ? 0 : 1;
}
