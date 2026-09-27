/*
 * gsm_upstream.c — AT protocol core + PPPoS hardware glue for the GSM/LTE
 * upstream (CTG-1).  See gsm_upstream.h for the split between the host-testable
 * core (compiled with -DTEST_HOST) and the target-only glue.
 */
#include "gsm_upstream.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ------------------------------------------------------------------------- */
/* small helpers                                                             */
/* ------------------------------------------------------------------------- */

static void trim_copy(const char *in, char *out, size_t out_len)
{
    if (out_len == 0) {
        return;
    }
    if (in == NULL) {
        out[0] = '\0';
        return;
    }
    while (*in == ' ' || *in == '\t' || *in == '\r' || *in == '\n') {
        in++;
    }
    size_t n = strlen(in);
    while (n > 0 && (in[n - 1] == '\r' || in[n - 1] == '\n' ||
                     in[n - 1] == ' ' || in[n - 1] == '\t')) {
        n--;
    }
    if (n > out_len - 1) {
        n = out_len - 1;
    }
    memcpy(out, in, n);
    out[n] = '\0';
}

static bool starts_with_ci(const char *s, const char *prefix)
{
    while (*prefix != '\0') {
        if (*s == '\0') {
            return false;
        }
        if (toupper((unsigned char)*s) != toupper((unsigned char)*prefix)) {
            return false;
        }
        s++;
        prefix++;
    }
    return true;
}

static bool equals_ci(const char *a, const char *b)
{
    return strcasecmp(a, b) == 0;
}

/* Case-insensitive substring search (ASCII). */
static const char *strstr_ci(const char *hay, const char *needle)
{
    if (hay == NULL || needle == NULL || *needle == '\0') {
        return NULL;
    }
    size_t nl = strlen(needle);
    for (const char *p = hay; *p != '\0'; p++) {
        if (strncasecmp(p, needle, nl) == 0) {
            return p;
        }
    }
    return NULL;
}

/* Locate the payload of a "+XXX:" info line.  Returns NULL when absent. */
static const char *info_payload(const char *line, const char *tag)
{
    const char *p = strstr_ci(line, tag);
    if (p == NULL) {
        return NULL;
    }
    p += strlen(tag);
    if (*p == ':') {
        p++;
    }
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    return p;
}

/* Collect up to max ints from a comma/space separated string. */
static int collect_ints(const char *s, int *vals, int max)
{
    int n = 0;
    while (n < max) {
        char *end = NULL;
        long v;
        while (*s == ' ' || *s == '\t' || *s == ',') {
            s++;
        }
        if (*s == '\0' || *s == '"') {
            break;
        }
        v = strtol(s, &end, 10);
        if (end == s) {
            break;
        }
        vals[n++] = (int)v;
        s = end;
        while (*s == ' ' || *s == '\t') {
            s++;
        }
        if (*s != ',') {
            break;
        }
    }
    return n;
}

/* ------------------------------------------------------------------------- */
/* response classification                                                   */
/* ------------------------------------------------------------------------- */

gsm_resp_t gsm_classify_response(const char *line)
{
    char buf[128];
    trim_copy(line, buf, sizeof buf);

    if (buf[0] == '\0') {
        return GSM_RESP_UNKNOWN;
    }
    if (equals_ci(buf, "OK")) {
        return GSM_RESP_OK;
    }
    if (equals_ci(buf, "ERROR")) {
        return GSM_RESP_ERROR;
    }
    if (starts_with_ci(buf, "+CME ERROR")) {
        return GSM_RESP_CME_ERROR;
    }
    if (starts_with_ci(buf, "+CMS ERROR")) {
        return GSM_RESP_CMS_ERROR;
    }
    if (equals_ci(buf, "NO CARRIER")) {
        return GSM_RESP_NO_CARRIER;
    }
    if (equals_ci(buf, "NO DIALTONE")) {
        return GSM_RESP_NO_DIALTONE;
    }
    if (equals_ci(buf, "BUSY")) {
        return GSM_RESP_BUSY;
    }
    if (starts_with_ci(buf, "CONNECT")) {
        /* "CONNECT", "CONNECT 115200" */
        return GSM_RESP_CONNECT;
    }
    return GSM_RESP_UNKNOWN;
}

int gsm_response_error_code(const char *line)
{
    const char *p = strstr_ci(line, "+CME ERROR");
    if (p == NULL) {
        p = strstr_ci(line, "+CMS ERROR");
    }
    if (p == NULL) {
        return -1;
    }
    p = strchr(p, ':');
    if (p == NULL) {
        return -1;
    }
    p++;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (!isdigit((unsigned char)*p)) {
        return -1;
    }
    return (int)strtol(p, NULL, 10);
}

const char *gsm_response_name(gsm_resp_t r)
{
    switch (r) {
    case GSM_RESP_OK:          return "OK";
    case GSM_RESP_ERROR:       return "ERROR";
    case GSM_RESP_CONNECT:     return "CONNECT";
    case GSM_RESP_NO_CARRIER:  return "NO CARRIER";
    case GSM_RESP_BUSY:        return "BUSY";
    case GSM_RESP_NO_DIALTONE: return "NO DIALTONE";
    case GSM_RESP_CME_ERROR:   return "+CME ERROR";
    case GSM_RESP_CMS_ERROR:   return "+CMS ERROR";
    case GSM_RESP_UNKNOWN:
    default:                   return "UNKNOWN";
    }
}

/* ------------------------------------------------------------------------- */
/* SIM state                                                                 */
/* ------------------------------------------------------------------------- */

bool gsm_parse_cpin(const char *line, gsm_sim_state_t *out)
{
    char buf[128];
    const char *p;
    gsm_resp_t r = gsm_classify_response(line);

    if (out != NULL) {
        *out = GSM_SIM_UNKNOWN;
    }

    if (r == GSM_RESP_CME_ERROR) {
        gsm_sim_state_t s = GSM_SIM_UNKNOWN;
        switch (gsm_response_error_code(line)) {
        case 10: s = GSM_SIM_NOT_INSERTED; break;
        case 11: s = GSM_SIM_PIN_REQUIRED; break;
        case 12: s = GSM_SIM_PUK_REQUIRED; break;
        case 13: s = GSM_SIM_UNKNOWN; break;         /* SIM failure */
        case 14: s = GSM_SIM_BUSY; break;
        case 15: s = GSM_SIM_NOT_INSERTED; break;    /* SIM wrong */
        default: break;
        }
        if (out != NULL) {
            *out = s;
        }
        return true;
    }

    p = info_payload(line, "+CPIN");
    if (p == NULL) {
        return false;
    }
    trim_copy(p, buf, sizeof buf);
    if (buf[0] == '\0') {
        return false;
    }

    gsm_sim_state_t s;
    if (equals_ci(buf, "READY")) {
        s = GSM_SIM_READY;
    } else if (starts_with_ci(buf, "SIM PIN") || starts_with_ci(buf, "SIM PIN2")) {
        s = GSM_SIM_PIN_REQUIRED;
    } else if (starts_with_ci(buf, "SIM PUK") || starts_with_ci(buf, "SIM PUK2")) {
        s = GSM_SIM_PUK_REQUIRED;
    } else if (equals_ci(buf, "SIM BUSY")) {
        s = GSM_SIM_BUSY;
    } else {
        s = GSM_SIM_UNKNOWN;
    }
    if (out != NULL) {
        *out = s;
    }
    return true;
}

const char *gsm_sim_state_name(gsm_sim_state_t s)
{
    switch (s) {
    case GSM_SIM_READY:        return "READY";
    case GSM_SIM_PIN_REQUIRED: return "PIN required";
    case GSM_SIM_PUK_REQUIRED: return "PUK required";
    case GSM_SIM_NOT_INSERTED: return "not inserted";
    case GSM_SIM_BUSY:         return "busy";
    case GSM_SIM_UNKNOWN:
    default:                   return "unknown";
    }
}

/* ------------------------------------------------------------------------- */
/* signal quality                                                            */
/* ------------------------------------------------------------------------- */

bool gsm_parse_csq(const char *line, int *rssi, int *ber)
{
    const char *p = info_payload(line, "+CSQ");
    int vals[2] = { 99, 99 };
    int n;

    if (p == NULL) {
        return false;
    }
    n = collect_ints(p, vals, 2);
    if (n == 0) {
        return false;
    }
    if (rssi != NULL) {
        *rssi = vals[0];
    }
    if (ber != NULL) {
        *ber = (n >= 2) ? vals[1] : 99;
    }
    return true;
}

int gsm_rssi_dbm(int rssi)
{
    if (rssi < 0 || rssi > 31) {
        return GSM_RSSI_UNKNOWN_DBM;
    }
    return -113 + 2 * rssi;
}

int gsm_rssi_bars(int rssi)
{
    int dbm = gsm_rssi_dbm(rssi);

    if (dbm == GSM_RSSI_UNKNOWN_DBM) {
        return 0;
    }
    if (dbm >= -75)  return 5;
    if (dbm >= -85)  return 4;
    if (dbm >= -95)  return 3;
    if (dbm >= -105) return 2;
    return 1;
}

/* ------------------------------------------------------------------------- */
/* registration                                                              */
/* ------------------------------------------------------------------------- */

/* Extended form: also reports the access technology when the 5-parameter
 * CEREG form carries one.  *act is left untouched when absent. */
static bool parse_reg_full(const char *line, gsm_reg_state_t *stat, int *area, int *act)
{
    int vals[6];
    int n;
    const char *p = info_payload(line, "+CREG");

    if (p == NULL) {
        p = info_payload(line, "+CGREG");
    }
    if (p == NULL) {
        p = info_payload(line, "+CEREG");
    }
    if (p == NULL) {
        return false;
    }

    n = collect_ints(p, vals, 6);
    if (n == 0) {
        return false;
    }

    if (stat != NULL) {
        /* One parameter => unsolicited "+CREG: <stat>"; two or more =>
         * "+CREG: <n>,<stat>[,<lac/tac>,<ci>[,<act>]]". */
        *stat = (gsm_reg_state_t)((n == 1) ? vals[0] : vals[1]);
    }
    if (area != NULL) {
        *area = (n >= 3) ? vals[2] : -1;
    }
    if (act != NULL && n >= 5) {
        *act = vals[4];
    }
    return true;
}

bool gsm_parse_reg(const char *line, gsm_reg_state_t *stat, int *area)
{
    return parse_reg_full(line, stat, area, NULL);
}

bool gsm_reg_is_registered(gsm_reg_state_t s)
{
    return s == GSM_REG_HOME || s == GSM_REG_ROAMING;
}

const char *gsm_reg_state_name(gsm_reg_state_t s)
{
    switch (s) {
    case GSM_REG_NOT_REGISTERED:      return "not registered";
    case GSM_REG_HOME:                return "registered (home)";
    case GSM_REG_SEARCHING:           return "searching";
    case GSM_REG_DENIED:              return "denied";
    case GSM_REG_UNKNOWN:             return "unknown";
    case GSM_REG_ROAMING:             return "registered (roaming)";
    case GSM_REG_HOME_SMS_ONLY:       return "home (SMS only)";
    case GSM_REG_ROAMING_SMS_ONLY:    return "roaming (SMS only)";
    case GSM_REG_EMERGENCY_ONLY:      return "emergency only";
    default:                          return "?";
    }
}

/* ------------------------------------------------------------------------- */
/* operator + access technology                                              */
/* ------------------------------------------------------------------------- */

bool gsm_parse_cops(const char *line, char *oper, size_t oper_len, int *act)
{
    const char *p = info_payload(line, "+COPS");
    int mode, format;
    char *end = NULL;

    if (oper != NULL && oper_len > 0) {
        oper[0] = '\0';
    }
    if (p == NULL) {
        return false;
    }
    if (!isdigit((unsigned char)*p)) {
        return false;
    }
    mode = (int)strtol(p, &end, 10);
    if (end == p || *end != ',') {
        return false;   /* "+COPS: <mode>" only */
    }
    p = end + 1;
    if (!isdigit((unsigned char)*p)) {
        return false;
    }
    format = (int)strtol(p, &end, 10);
    if (end == p) {
        return false;
    }
    p = end;
    if (*p == ',') {
        p++;
    }
    (void)mode;
    (void)format;

    if (*p == '"') {
        const char *e = strchr(p + 1, '"');
        if (e == NULL) {
            return false;
        }
        if (oper != NULL && oper_len > 0) {
            size_t n = (size_t)(e - (p + 1));
            if (n > oper_len - 1) {
                n = oper_len - 1;
            }
            memcpy(oper, p + 1, n);
            oper[n] = '\0';
        }
        p = e + 1;
    } else {
        /* Numeric format (0/1/2 depending on modem): MCC-MNC digits with no
         * quotes.  Step over the field so the act value behind it is not
         * mistaken for it. */
        while (*p != '\0' && *p != ',') {
            p++;
        }
    }

    if (act != NULL) {
        if (*p == ',') {
            p++;
        }
        if (isdigit((unsigned char)*p)) {
            *act = (int)strtol(p, NULL, 10);
        } else {
            *act = -1;
        }
    }
    return true;
}

bool gsm_act_is_2g(int act)
{
    return act == GSM_ACT_GSM || act == GSM_ACT_GSM_COMPACT || act == GSM_ACT_EDGE;
}

const char *gsm_act_name(int act)
{
    switch (act) {
    case GSM_ACT_GSM:            return "GSM (2G)";
    case GSM_ACT_GSM_COMPACT:    return "GSM compact (2G)";
    case GSM_ACT_EDGE:           return "EDGE (2G)";
    case GSM_ACT_UTRAN:          return "UMTS (3G)";
    case GSM_ACT_UTRAN_HSDPA:    return "HSDPA (3G)";
    case GSM_ACT_UTRAN_HSUPA:    return "HSUPA (3G)";
    case GSM_ACT_UTRAN_HSPA:     return "HSPA (3G)";
    case GSM_ACT_EUTRAN:         return "LTE (4G)";
    case GSM_ACT_EUTRAN_NB:      return "NB-IoT";
    case GSM_ACT_EUTRAN_CAT_M1:  return "LTE Cat-M1";
    default:                     return "unknown";
    }
}

/* ------------------------------------------------------------------------- */
/* APN + PDP                                                                 */
/* ------------------------------------------------------------------------- */

bool gsm_apn_valid(const char *apn)
{
    size_t len;
    bool all_digits = true;

    if (apn == NULL) {
        return false;
    }
    len = strlen(apn);
    if (len == 0 || len >= GSM_APN_MAX) {
        return false;
    }
    if (apn[0] == '.' || apn[len - 1] == '.') {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)apn[i];
        if (isalpha(c)) {
            all_digits = false;
        } else if (isdigit(c)) {
            /* still maybe all-digits */
        } else if (c == '.' || c == '-' || c == '_') {
            all_digits = false;
        } else {
            return false;
        }
    }
    return !all_digits;
}

size_t gsm_build_cgdcont(int cid, const char *apn, char *out, size_t out_len)
{
    int n;

    if (out == NULL || out_len == 0 || cid <= 0 || !gsm_apn_valid(apn)) {
        if (out != NULL && out_len > 0) {
            out[0] = '\0';
        }
        return 0;
    }
    n = snprintf(out, out_len, "AT+CGDCONT=%d,\"IP\",\"%s\"", cid, apn);
    if (n < 0 || (size_t)n >= out_len) {
        out[0] = '\0';
        return 0;
    }
    return (size_t)n;
}

const char *gsm_dial_command(int cid)
{
    static char buf[32];

    if (cid <= 0) {
        cid = GSM_DEFAULT_CID;
    }
    snprintf(buf, sizeof buf, "ATD*99***%d#", cid);
    return buf;
}

/* ------------------------------------------------------------------------- */
/* failure diagnosis                                                         */
/* ------------------------------------------------------------------------- */

const char *gsm_failure_name(gsm_failure_t f)
{
    switch (f) {
    case GSM_FAIL_NONE:               return "none";
    case GSM_FAIL_NO_RESPONSE:        return "modem did not respond to AT";
    case GSM_FAIL_SIM_NOT_INSERTED:   return "SIM not inserted / not detected";
    case GSM_FAIL_SIM_PIN:            return "SIM locked (PIN/PUK required)";
    case GSM_FAIL_REG_DENIED:         return "network denied registration";
    case GSM_FAIL_NO_COVERAGE:        return "no coverage / not registered";
    case GSM_FAIL_PDP_REJECTED:       return "PDP context rejected (APN or data plan)";
    case GSM_FAIL_AUTH_FAILED:        return "PDP authentication failed (PAP/CHAP)";
    case GSM_FAIL_DIAL_FAILED:        return "dial failed (no CONNECT)";
    case GSM_FAIL_2G_ONLY:            return "registered on 2G only (EU sunset risk)";
    case GSM_FAIL_OPERATOR_REFUSED:   return "operator refused attach (CME 3)";
    default:                          return "?";
    }
}

gsm_failure_t gsm_diagnose(const char *transcript)
{
    bool saw_ok = false;
    bool saw_connect = false;
    bool saw_cgdcont = false;
    bool saw_error = false;
    bool saw_auth = false;

    if (transcript == NULL) {
        return GSM_FAIL_NO_RESPONSE;
    }

    if (strstr_ci(transcript, "OK") != NULL) {
        saw_ok = true;
    }
    if (strstr_ci(transcript, "CONNECT") != NULL) {
        saw_connect = true;
    }
    if (strstr_ci(transcript, "CGDCONT") != NULL) {
        saw_cgdcont = true;
    }
    if (strstr_ci(transcript, "ERROR") != NULL) {
        saw_error = true;
    }
    if (strstr_ci(transcript, "CHAP") != NULL || strstr_ci(transcript, "PAP") != NULL) {
        saw_auth = true;
    }

    /* Fatal conditions, most specific first. */
    if (strstr_ci(transcript, "+CME ERROR: 10") != NULL ||
        strstr_ci(transcript, "SIM NOT INSERTED") != NULL) {
        return GSM_FAIL_SIM_NOT_INSERTED;
    }
    if (strstr_ci(transcript, "SIM PIN") != NULL ||
        strstr_ci(transcript, "+CME ERROR: 11") != NULL ||
        strstr_ci(transcript, "+CME ERROR: 12") != NULL) {
        return GSM_FAIL_SIM_PIN;
    }
    if (strstr_ci(transcript, ",3") != NULL &&
        (strstr_ci(transcript, "+CREG") != NULL || strstr_ci(transcript, "+CGREG") != NULL ||
         strstr_ci(transcript, "+CEREG") != NULL)) {
        return GSM_FAIL_REG_DENIED;
    }
    if (saw_auth && saw_error) {
        return GSM_FAIL_AUTH_FAILED;
    }
    if (strstr_ci(transcript, "NO CARRIER") != NULL) {
        return GSM_FAIL_DIAL_FAILED;
    }
    if (strstr_ci(transcript, "+CME ERROR: 3") != NULL) {
        /* CME 3 (operation not allowed) is reported by the operator during
         * PDP setup, so it has to be checked before the generic "CGDCONT +
         * ERROR" rule below, which would otherwise mask it. */
        return GSM_FAIL_OPERATOR_REFUSED;
    }
    if (saw_cgdcont && saw_error) {
        return GSM_FAIL_PDP_REJECTED;
    }
    if (saw_connect) {
        /* Data mode reached.  Warn if the operator line says 2G. */
        char oper[32];
        int act = -1;
        if (gsm_parse_cops(transcript, oper, sizeof oper, &act) && gsm_act_is_2g(act)) {
            return GSM_FAIL_2G_ONLY;
        }
        return GSM_FAIL_NONE;
    }
    if (!saw_ok && strstr_ci(transcript, "+CSQ") == NULL) {
        return GSM_FAIL_NO_RESPONSE;
    }
    if (strstr_ci(transcript, "SEARCHING") != NULL ||
        strstr_ci(transcript, "+CREG: 0,2") != NULL ||
        strstr_ci(transcript, "+CEREG: 0,2") != NULL) {
        return GSM_FAIL_NO_COVERAGE;
    }
    if (saw_error) {
        return GSM_FAIL_DIAL_FAILED;
    }
    return GSM_FAIL_NO_COVERAGE;
}

/* ------------------------------------------------------------------------- */
/* bring-up state machine                                                    */
/* ------------------------------------------------------------------------- */

const char *gsm_state_name(gsm_state_t s)
{
    switch (s) {
    case GSM_ST_IDLE:      return "IDLE";
    case GSM_ST_AT_PROBE:  return "AT_PROBE";
    case GSM_ST_SIM_CHECK: return "SIM_CHECK";
    case GSM_ST_SIGNAL:    return "SIGNAL";
    case GSM_ST_REG_CREG:  return "REG_CREG";
    case GSM_ST_REG_CEREG: return "REG_CEREG";
    case GSM_ST_PDP_SETUP: return "PDP_SETUP";
    case GSM_ST_DIAL:      return "DIAL";
    case GSM_ST_PPP_UP:    return "PPP_UP";
    case GSM_ST_ERROR:     return "ERROR";
    default:               return "?";
    }
}

static bool gsm_enter(gsm_ctx_t *ctx, gsm_state_t s)
{
    ctx->state = s;
    ctx->attempts = 0;
    return true;
}

static bool gsm_fail(gsm_ctx_t *ctx, gsm_failure_t f)
{
    ctx->failure = f;
    ctx->state = GSM_ST_ERROR;
    ctx->attempts = 0;
    return true;
}

void gsm_ctx_init(gsm_ctx_t *ctx, const char *apn, int cid)
{
    if (ctx == NULL) {
        return;
    }
    memset(ctx, 0, sizeof *ctx);
    ctx->state = GSM_ST_IDLE;
    ctx->failure = GSM_FAIL_NONE;
    ctx->sim = GSM_SIM_UNKNOWN;
    ctx->reg_2g = GSM_REG_UNKNOWN;
    ctx->reg_lte = GSM_REG_UNKNOWN;
    ctx->rssi = 99;
    ctx->ber = 99;
    ctx->act = -1;
    ctx->cid = (cid > 0) ? cid : GSM_DEFAULT_CID;

    const char *a = (apn != NULL && apn[0] != '\0') ? apn : GSM_SILENTLINK_APN;
    snprintf(ctx->apn, sizeof ctx->apn, "%s", a);

    if (!gsm_apn_valid(ctx->apn)) {
        ctx->failure = GSM_FAIL_PDP_REJECTED;
        ctx->state = GSM_ST_ERROR;
    }
}

bool gsm_is_up(const gsm_ctx_t *ctx)
{
    return ctx != NULL && ctx->state == GSM_ST_PPP_UP;
}

const char *gsm_next_command(gsm_ctx_t *ctx)
{
    const char *cmd = NULL;
    char built[80];

    if (ctx == NULL || ctx->state == GSM_ST_PPP_UP || ctx->state == GSM_ST_ERROR) {
        return NULL;
    }

    /* Timeout/retry budget.  The target layer re-calls this for the same state
     * when the modem stays silent, so an exhausted budget means "no answer". */
    if (ctx->max_attempts > 0 && ctx->attempts >= ctx->max_attempts) {
        switch (ctx->state) {
        case GSM_ST_DIAL:
        case GSM_ST_PDP_SETUP:
            gsm_fail(ctx, GSM_FAIL_DIAL_FAILED);
            break;
        case GSM_ST_REG_CREG:
        case GSM_ST_REG_CEREG:
            gsm_fail(ctx, GSM_FAIL_NO_COVERAGE);
            break;
        default:
            gsm_fail(ctx, GSM_FAIL_NO_RESPONSE);
            break;
        }
        return NULL;
    }

    switch (ctx->state) {
    case GSM_ST_IDLE:
    case GSM_ST_AT_PROBE:
        cmd = "AT";
        break;
    case GSM_ST_SIM_CHECK:
        cmd = "AT+CPIN?";
        break;
    case GSM_ST_SIGNAL:
        cmd = "AT+CSQ";
        break;
    case GSM_ST_REG_CREG:
        cmd = "AT+CREG?";
        break;
    case GSM_ST_REG_CEREG:
        cmd = "AT+CEREG?";
        break;
    case GSM_ST_PDP_SETUP:
        if (gsm_build_cgdcont(ctx->cid, ctx->apn, built, sizeof built) == 0) {
            gsm_fail(ctx, GSM_FAIL_PDP_REJECTED);
            return NULL;
        }
        cmd = built;
        break;
    case GSM_ST_DIAL:
        cmd = gsm_dial_command(ctx->cid);
        break;
    default:
        return NULL;
    }

    ctx->attempts++;
    if (ctx->state == GSM_ST_IDLE) {
        /* Issuing the probe *is* the transition into AT_PROBE; keeping IDLE
         * here would make the machine's state lag its own command. */
        ctx->state = GSM_ST_AT_PROBE;
    }
    snprintf(ctx->last_command, sizeof ctx->last_command, "%s", cmd);
    return ctx->last_command;
}

bool gsm_feed_response(gsm_ctx_t *ctx, const char *line)
{
    char buf[160];
    gsm_resp_t r;
    gsm_state_t before;

    if (ctx == NULL) {
        return false;
    }
    trim_copy(line, buf, sizeof buf);
    if (buf[0] == '\0') {
        return false;
    }
    snprintf(ctx->last_response, sizeof ctx->last_response, "%s", buf);

    before = ctx->state;
    r = gsm_classify_response(buf);

    switch (ctx->state) {
    case GSM_ST_IDLE:
    case GSM_ST_AT_PROBE:
        if (r == GSM_RESP_OK) {
            gsm_enter(ctx, GSM_ST_SIM_CHECK);
        } else if (r == GSM_RESP_ERROR || r == GSM_RESP_CME_ERROR) {
            gsm_fail(ctx, GSM_FAIL_NO_RESPONSE);
        }
        break;

    case GSM_ST_SIM_CHECK: {
        gsm_sim_state_t s = GSM_SIM_UNKNOWN;
        if (gsm_parse_cpin(buf, &s)) {
            ctx->sim = s;
            if (s == GSM_SIM_READY) {
                gsm_enter(ctx, GSM_ST_SIGNAL);
            } else if (s == GSM_SIM_PIN_REQUIRED || s == GSM_SIM_PUK_REQUIRED) {
                gsm_fail(ctx, GSM_FAIL_SIM_PIN);
            } else if (s == GSM_SIM_NOT_INSERTED) {
                gsm_fail(ctx, GSM_FAIL_SIM_NOT_INSERTED);
            }
            /* BUSY / UNKNOWN: stay and let the retry budget run. */
        }
        break;
    }

    case GSM_ST_SIGNAL: {
        int rssi = 99, ber = 99;
        if (gsm_parse_csq(buf, &rssi, &ber)) {
            ctx->rssi = rssi;
            ctx->ber = ber;
            gsm_enter(ctx, GSM_ST_REG_CREG);
        }
        break;
    }

    case GSM_ST_REG_CREG: {
        gsm_reg_state_t st = GSM_REG_UNKNOWN;
        int area = -1;
        if (parse_reg_full(buf, &st, &area, &ctx->act)) {
            ctx->reg_2g = st;
            if (gsm_reg_is_registered(st)) {
                if (ctx->lte_reg_unsupported) {
                    gsm_enter(ctx, GSM_ST_PDP_SETUP);
                } else {
                    gsm_enter(ctx, GSM_ST_REG_CEREG);
                }
            } else if (st == GSM_REG_DENIED) {
                gsm_fail(ctx, GSM_FAIL_REG_DENIED);
            }
            /* SEARCHING / NOT_REGISTERED: stay, retry budget applies. */
        }
        break;
    }

    case GSM_ST_REG_CEREG: {
        gsm_reg_state_t st = GSM_REG_UNKNOWN;
        int area = -1;
        if (parse_reg_full(buf, &st, &area, &ctx->act)) {
            ctx->reg_lte = st;
            if (gsm_reg_is_registered(st)) {
                gsm_enter(ctx, GSM_ST_PDP_SETUP);
            } else if (st == GSM_REG_DENIED) {
                gsm_fail(ctx, GSM_FAIL_REG_DENIED);
            }
        } else if (r == GSM_RESP_ERROR || r == GSM_RESP_CME_ERROR) {
            /* 2G-only modem (SIM800 class) has no CEREG — carry on. */
            ctx->lte_reg_unsupported = true;
            gsm_enter(ctx, GSM_ST_PDP_SETUP);
        }
        break;
    }

    case GSM_ST_PDP_SETUP:
        if (r == GSM_RESP_OK) {
            gsm_enter(ctx, GSM_ST_DIAL);
        } else if (r == GSM_RESP_CONNECT) {
            gsm_enter(ctx, GSM_ST_PPP_UP);
        } else if (r == GSM_RESP_ERROR || r == GSM_RESP_CME_ERROR) {
            gsm_fail(ctx,
                     gsm_response_error_code(buf) == 3 ? GSM_FAIL_OPERATOR_REFUSED
                                                       : GSM_FAIL_PDP_REJECTED);
        }
        break;

    case GSM_ST_DIAL:
        if (r == GSM_RESP_CONNECT) {
            gsm_enter(ctx, GSM_ST_PPP_UP);
        } else if (r == GSM_RESP_NO_CARRIER) {
            gsm_fail(ctx, GSM_FAIL_DIAL_FAILED);
        } else if (r == GSM_RESP_ERROR || r == GSM_RESP_CME_ERROR) {
            gsm_fail(ctx,
                     gsm_response_error_code(buf) == 3 ? GSM_FAIL_OPERATOR_REFUSED
                                                       : GSM_FAIL_DIAL_FAILED);
        }
        break;

    case GSM_ST_PPP_UP:
    case GSM_ST_ERROR:
    default:
        break;
    }

    return ctx->state != before;
}

/* ------------------------------------------------------------------------- */
/* target-only glue: UART + PPPoS netif                                      */
/* ------------------------------------------------------------------------- */

#ifndef TEST_HOST

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "gsm";

static gsm_ctx_t s_ctx;

static void gsm_pwrkey_pulse(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << GSM_PWRKEY_PIN,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    gpio_set_level(GSM_PWRKEY_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(1200));      /* datasheet: >1 s LOW to toggle */
    gpio_set_level(GSM_PWRKEY_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(GSM_MODEM_BOOT_MS));
}

static void gsm_uart_init(int baud)
{
    const uart_config_t cfg = {
        .baud_rate = baud,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_driver_install(GSM_UART_NUM, 1024, 0, 0, NULL, 0);
    uart_param_config(GSM_UART_NUM, &cfg);
    uart_set_pin(GSM_UART_NUM, GSM_UART_TX_PIN, GSM_UART_RX_PIN,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

/* Read one CR/LF-terminated line.  Returns length, or -1 on timeout. */
static int gsm_read_line(char *out, size_t out_len, int timeout_ms)
{
    size_t n = 0;
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);

    while (xTaskGetTickCount() < deadline) {
        uint8_t c;
        if (uart_read_bytes(GSM_UART_NUM, &c, 1, pdMS_TO_TICKS(20)) != 1) {
            continue;
        }
        if (c == '\n') {
            if (n == 0) {
                continue;   /* swallow blank keep-alive lines */
            }
            out[n] = '\0';
            return (int)n;
        }
        if (c == '\r') {
            continue;
        }
        if (n < out_len - 1) {
            out[n++] = (char)c;
        }
    }
    return -1;
}

/* Send one AT command, feed every line the modem prints into the state machine,
 * and return the first *final* result code seen (UNKNOWN on timeout, which the
 * caller treats as "no answer, retry this state"). */
static gsm_resp_t gsm_at(gsm_ctx_t *ctx, const char *cmd, int timeout_ms)
{
    char line[160];

    uart_write_bytes(GSM_UART_NUM, cmd, strlen(cmd));
    uart_write_bytes(GSM_UART_NUM, "\r", 1);

    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
    while (xTaskGetTickCount() < deadline) {
        if (gsm_read_line(line, sizeof line, 500) < 0) {
            continue;
        }
        ESP_LOGD(TAG, "< %s", line);
        gsm_resp_t r = gsm_classify_response(line);
        gsm_feed_response(ctx, line);
        if (r != GSM_RESP_UNKNOWN) {
            return r;
        }
    }
    return GSM_RESP_UNKNOWN;
}

/* ------------------------------------------------------------------------- */
/* target-only glue: power, UART, AT bring-up                              */
/* ------------------------------------------------------------------------- */

static void gsm_transcript_append(char *t, size_t tlen, size_t *off, const char *line)
{
    if (t == NULL || tlen == 0) {
        return;
    }
    int n = snprintf(t + *off, tlen - *off, "%s\n", line);
    if (n < 0 || (size_t)n >= tlen - *off) {
        *off = tlen - 1;   /* truncated; diagnose() still sees the prefix */
        return;
    }
    *off += (size_t)n;
}

bool gsm_bring_up(const char *apn, int cid, int baud, char *transcript, size_t transcript_len)
{
    size_t off = 0;

    if (transcript != NULL && transcript_len > 0) {
        transcript[0] = '\0';
    }

    gsm_ctx_init(&s_ctx, apn, cid);
    s_ctx.max_attempts = 5;

    if (s_ctx.state == GSM_ST_ERROR) {
        ESP_LOGE(TAG, "invalid APN \"%s\" — refusing to dial", s_ctx.apn);
        return false;
    }

    gsm_pwrkey_pulse();
    gsm_uart_init(baud > 0 ? baud : GSM_UART_BAUD);

    while (!gsm_is_up(&s_ctx)) {
        const char *cmd = gsm_next_command(&s_ctx);
        if (cmd == NULL) {
            break;   /* entered GSM_ST_ERROR */
        }
        ESP_LOGD(TAG, "> %s", cmd);
        gsm_transcript_append(transcript, transcript_len, &off, cmd);
        gsm_at(&s_ctx, cmd, 10000);
    }

    if (!gsm_is_up(&s_ctx)) {
        ESP_LOGE(TAG, "bring-up failed at %s: %s (sim=%s reg=%s creg=%s rssi=%d)",
                 gsm_state_name(s_ctx.state), gsm_failure_name(s_ctx.failure),
                 gsm_sim_state_name(s_ctx.sim), gsm_reg_state_name(s_ctx.reg_2g),
                 gsm_reg_state_name(s_ctx.reg_lte), s_ctx.rssi);
        if (transcript != NULL) {
            ESP_LOGE(TAG, "diagnosis: %s",
                     gsm_failure_name(gsm_diagnose(transcript)));
        }
        return false;
    }

    ESP_LOGI(TAG, "modem in data mode — apn=%s cid=%d sim=%s reg=%s rssi=%d dBm (%d bars)",
             s_ctx.apn, s_ctx.cid, gsm_sim_state_name(s_ctx.sim),
             gsm_reg_state_name(s_ctx.reg_2g), gsm_rssi_dbm(s_ctx.rssi),
             gsm_rssi_bars(s_ctx.rssi));
    return true;
}

const gsm_ctx_t *gsm_last_ctx(void)
{
    return &s_ctx;
}

bool gsm_start(const char *apn, int cid, int baud)
{
    char transcript[2048];

    if (!gsm_bring_up(apn, cid, baud, transcript, sizeof transcript)) {
        return false;
    }

    /* The modem is in data mode.  Handing the UART to LwIP PPPoS is the one
     * step that cannot be validated from here: it needs the pppos_create()
     * input/output driver wiring, and a wrong guess would break the target
     * build.  It is deliberately left out and documented, not faked. */
    ESP_LOGW(TAG, "PPP attach not implemented — bring-up OK, no IP layer yet "
                  "(see docs/GSM-UPSTREAM-INTEGRATION.md §PPP attach)");
    return false;
}

void gsm_stop(void)
{
    uart_driver_delete(GSM_UART_NUM);
    s_ctx.state = GSM_ST_IDLE;
}

#endif /* !TEST_HOST */
