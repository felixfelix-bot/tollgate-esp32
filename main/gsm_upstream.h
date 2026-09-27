/*
 * gsm_upstream.h — GSM/LTE mobile-data upstream for the car TollGate gateway.
 *
 * CTG-1: an ESP32-S3 drives a SIM800L/SIM7600-class modem over UART, brings up
 * a PPPoS interface with cellular data (SilentLink IoT SIM, APN "silent.link"),
 * and uses it as the upstream that the TollGate hotspot resells.
 *
 * The module is split in two halves on purpose:
 *
 *   1. The AT protocol core (everything declared below and implemented outside
 *      the `#ifndef TEST_HOST` guard in gsm_upstream.c).  It is pure C with no
 *      ESP-IDF dependencies, so `make -C tests/unit test_gsm_upstream` exercises
 *      it on the host.  This is where all parsing, signal mapping, APN
 *      validation, the bring-up state machine, and failure diagnosis live.
 *
 *   2. The hardware glue (UART + PPP netif), compiled only on target
 *      (`!#ifndef TEST_HOST`), which feeds raw modem lines into the core above.
 *
 * Nothing in the core touches the radio: a line of text in, a decision out.
 * That is what makes the bring-up logic testable without the shield attached.
 */
#ifndef GSM_UPSTREAM_H
#define GSM_UPSTREAM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------- */
/* Response classification                                                   */
/* ------------------------------------------------------------------------- */

typedef enum {
    GSM_RESP_UNKNOWN = 0,   /* not a final result code */
    GSM_RESP_OK,            /* "OK" */
    GSM_RESP_ERROR,         /* bare "ERROR" */
    GSM_RESP_CONNECT,       /* "CONNECT" — modem entered data mode */
    GSM_RESP_NO_CARRIER,    /* "NO CARRIER" */
    GSM_RESP_BUSY,          /* "BUSY" */
    GSM_RESP_NO_DIALTONE,   /* "NO DIALTONE" */
    GSM_RESP_CME_ERROR,     /* "+CME ERROR: <n>" (equipment) */
    GSM_RESP_CMS_ERROR,     /* "+CMS ERROR: <n>" (SMS) */
} gsm_resp_t;

/* Classify a single modem line (CR/LF, leading/trailing blanks tolerated).
 * Returns GSM_RESP_UNKNOWN for an intermediate/unsolicited line such as
 * "+CSQ: 17,0" or "RDY". */
gsm_resp_t gsm_classify_response(const char *line);

/* Numeric payload of a "+CME ERROR: <n>" / "+CMS ERROR: <n>" line, or -1. */
int gsm_response_error_code(const char *line);

const char *gsm_response_name(gsm_resp_t r);

/* ------------------------------------------------------------------------- */
/* SIM state (AT+CPIN?)                                                      */
/* ------------------------------------------------------------------------- */

typedef enum {
    GSM_SIM_UNKNOWN = 0,
    GSM_SIM_READY,
    GSM_SIM_PIN_REQUIRED,     /* "+CPIN: SIM PIN" */
    GSM_SIM_PUK_REQUIRED,     /* "+CPIN: SIM PUK" */
    GSM_SIM_NOT_INSERTED,     /* CME 10 / "+CME ERROR: 10" */
    GSM_SIM_BUSY,             /* CME 14 */
} gsm_sim_state_t;

/* Parse an AT+CPIN? reply.  Returns true when the line carried a SIM state
 * (either a +CPIN: state or a CME error that maps to one). */
bool gsm_parse_cpin(const char *line, gsm_sim_state_t *out);
const char *gsm_sim_state_name(gsm_sim_state_t s);

/* ------------------------------------------------------------------------- */
/* Signal quality (AT+CSQ)                                                   */
/* ------------------------------------------------------------------------- */

/* Parse "+CSQ: <rssi>,<ber>".  rssi 0..31 valid, 99 unknown; ber 0..7, 99
 * unknown.  Returns true when the line matched. */
bool gsm_parse_csq(const char *line, int *rssi, int *ber);

/* Convert the AT+CSQ rssi index to dBm.  0 -> -113 dBm, 1 -> -111 ... 31 ->
 * -51 dBm, 99 (and anything out of range) -> GSM_RSSI_UNKNOWN_DBM. */
#define GSM_RSSI_UNKNOWN_DBM 0
int gsm_rssi_dbm(int rssi);

/* Map the rssi index to 0..5 "bars" (0 when unknown). */
int gsm_rssi_bars(int rssi);

/* ------------------------------------------------------------------------- */
/* Registration (AT+CREG? / AT+CGREG? / AT+CEREG?)                           */
/* ------------------------------------------------------------------------- */

typedef enum {
    GSM_REG_NOT_REGISTERED = 0,
    GSM_REG_HOME = 1,
    GSM_REG_SEARCHING = 2,
    GSM_REG_DENIED = 3,
    GSM_REG_UNKNOWN = 4,
    GSM_REG_ROAMING = 5,
    GSM_REG_HOME_SMS_ONLY = 6,
    GSM_REG_ROAMING_SMS_ONLY = 7,
    GSM_REG_EMERGENCY_ONLY = 8,
} gsm_reg_state_t;

/* Parse "+CREG: <n>[,<stat>[,<lac>,<ci>]]" and the CGREG/CEREG variants.  When
 * the reply carries only one parameter it is the status (unsolicited form
 * "+CREG: 1").  Returns true on a match; *stat gets the status enum, *area the
 * lac/tac when present (else -1). */
bool gsm_parse_reg(const char *line, gsm_reg_state_t *stat, int *area);

/* True for HOME or ROAMING (the states that permit a PDP context). */
bool gsm_reg_is_registered(gsm_reg_state_t s);
const char *gsm_reg_state_name(gsm_reg_state_t s);

/* ------------------------------------------------------------------------- */
/* Operator + access technology (AT+COPS?)                                   */
/* ------------------------------------------------------------------------- */

/* Parse "+COPS: <mode>,<format>,"<oper>",<act>".  Writes the operator name
 * (empty string when format is numeric) and the access technology; either
 * out-pointer may be NULL.  Returns true on a match. */
bool gsm_parse_cops(const char *line, char *oper, size_t oper_len, int *act);

/* Access technology codes from 3GPP TS 27.007 <AcT>. */
#define GSM_ACT_GSM       0
#define GSM_ACT_GSM_COMPACT 1
#define GSM_ACT_UTRAN     2
#define GSM_ACT_EDGE      3
#define GSM_ACT_UTRAN_HSDPA 4
#define GSM_ACT_UTRAN_HSUPA 5
#define GSM_ACT_UTRAN_HSPA 6
#define GSM_ACT_EUTRAN    7   /* LTE */
#define GSM_ACT_EUTRAN_NB 9
#define GSM_ACT_EUTRAN_CAT_M1 10

/* True for the 2G technologies (GSM/GPRS/EDGE) — the ones with no EU future. */
bool gsm_act_is_2g(int act);
const char *gsm_act_name(int act);

/* ------------------------------------------------------------------------- */
/* APN + PDP context (AT+CGDCONT / ATD*99***<cid>#)                          */
/* ------------------------------------------------------------------------- */

#define GSM_APN_MAX 64

/* Validate an APN before it is templated into an AT command: 1..GSM_APN_MAX
 * chars, [A-Za-z0-9.-_] only, no leading/trailing dot, not all-numeric.
 * This is an injection guard as much as a sanity check — the APN goes into a
 * string that is handed to the modem verbatim. */
bool gsm_apn_valid(const char *apn);

/* Build `AT+CGDCONT=<cid>,"IP","<apn>"` into out.  Returns the written length,
 * or 0 when the inputs are rejected (bad cid or invalid APN). */
size_t gsm_build_cgdcont(int cid, const char *apn, char *out, size_t out_len);

/* The PPP dial string for a context: `ATD*99***<cid>#`.  Static storage. */
const char *gsm_dial_command(int cid);

/* Default context id used by the SilentLink profile. */
#define GSM_DEFAULT_CID 1

/* The SilentLink IoT APN (see docs/GSM-UPSTREAM-INTEGRATION.md). */
#define GSM_SILENTLINK_APN "silent.link"

/* ------------------------------------------------------------------------- */
/* Failure diagnosis                                                         */
/* ------------------------------------------------------------------------- */

typedef enum {
    GSM_FAIL_NONE = 0,
    GSM_FAIL_NO_RESPONSE,     /* modem never answered AT */
    GSM_FAIL_SIM_NOT_INSERTED,
    GSM_FAIL_SIM_PIN,         /* SIM is locked */
    GSM_FAIL_REG_DENIED,      /* network refused the SIM */
    GSM_FAIL_NO_COVERAGE,     /* searching / not registered */
    GSM_FAIL_PDP_REJECTED,    /* +CGDCONT refused (bad APN / no data plan) */
    GSM_FAIL_AUTH_FAILED,     /* PAP/CHAP rejected */
    GSM_FAIL_DIAL_FAILED,     /* no CONNECT after ATD */
    GSM_FAIL_2G_ONLY,         /* registered, but only on a 2G cell (EU sunset) */
    GSM_FAIL_OPERATOR_REFUSED /* +CME ERROR during attach */
} gsm_failure_t;

const char *gsm_failure_name(gsm_failure_t f);

/* Scan a captured AT transcript for the first recognisable fatal condition.
 * Used both by the firmware's error path and by the operator-side validation
 * harness, so a failed bring-up reports the same reason on both sides. */
gsm_failure_t gsm_diagnose(const char *transcript);

/* ------------------------------------------------------------------------- */
/* Bring-up state machine                                                    */
/* ------------------------------------------------------------------------- */

typedef enum {
    GSM_ST_IDLE = 0,     /* nothing sent yet */
    GSM_ST_AT_PROBE,     /* "AT" sent */
    GSM_ST_SIM_CHECK,    /* AT+CPIN? sent */
    GSM_ST_SIGNAL,       /* AT+CSQ sent */
    GSM_ST_REG_CREG,     /* AT+CREG? sent */
    GSM_ST_REG_CEREG,    /* AT+CEREG? sent (LTE; optional on 2G-only modems) */
    GSM_ST_PDP_SETUP,    /* AT+CGDCONT sent */
    GSM_ST_DIAL,         /* ATD*99***1# sent */
    GSM_ST_PPP_UP,       /* CONNECT seen — PPP netif owns the UART now */
    GSM_ST_ERROR,        /* terminal failure; see ctx->failure */
} gsm_state_t;

const char *gsm_state_name(gsm_state_t s);

typedef struct {
    gsm_state_t state;
    gsm_failure_t failure;

    gsm_sim_state_t sim;
    gsm_reg_state_t reg_2g;
    gsm_reg_state_t reg_lte;
    bool lte_reg_unsupported;   /* CEREG answered ERROR (2G-only modem) */

    int rssi;
    int ber;
    char oper[32];
    int act;

    char apn[GSM_APN_MAX];
    int cid;

    unsigned attempts;      /* commands issued in the current state */
    unsigned max_attempts;  /* per-state retry budget (0 = fail on first miss) */

    char last_command[80];
    char last_response[160];
} gsm_ctx_t;

/* Initialise a context.  apn NULL or empty selects GSM_SILENTLINK_APN; a
 * non-positive cid selects GSM_DEFAULT_CID.  Invalid APNs leave the context in
 * GSM_ST_ERROR/GSM_FAIL_PDP_REJECTED immediately — refusing to dial with a
 * value that would corrupt the AT command. */
void gsm_ctx_init(gsm_ctx_t *ctx, const char *apn, int cid);

/* The next AT command for the current state, or NULL when none is due (PPP is
 * up, or the state machine has failed and is terminal).  The pointer is owned
 * by the context (ctx->last_command) and is overwritten on each call. */
const char *gsm_next_command(gsm_ctx_t *ctx);

/* Feed one modem line (already stripped of CR/LF by the caller; stripping is
 * tolerated).  Advances the state machine and returns true when the state
 * changed.  Unsolicited lines are ignored. */
bool gsm_feed_response(gsm_ctx_t *ctx, const char *line);

/* True when the PPP interface is up and the modem is carrying data. */
bool gsm_is_up(const gsm_ctx_t *ctx);

/* --- target-only glue ---------------------------------------------------- */

#ifndef TEST_HOST

/* Pins/UART for the default wiring (docs/GSM-UPSTREAM-INTEGRATION.md). */
#define GSM_UART_NUM      1
#define GSM_UART_TX_PIN   44
#define GSM_UART_RX_PIN   43
#define GSM_PWRKEY_PIN    4
#define GSM_UART_BAUD     115200   /* SIM7600 class; SIM800L needs 9600 */
#define GSM_MODEM_BOOT_MS 3000

/* Power on the modem (PWRKEY), install the UART and run the AT bring-up
 * sequence through to the modem's CONNECT (data mode).  Returns true when the
 * state machine reached GSM_ST_PPP_UP.  `baud` 0 selects GSM_UART_BAUD; the
 * optional `transcript` buffer (may be NULL) collects the raw AT dialogue so a
 * failed bring-up can be fed to gsm_diagnose() and logged.
 *
 * This is the function the operator-side validation harness mirrors: it is the
 * whole "does this SIM/shield give us mobile data" question minus the IP
 * layer. */
bool gsm_bring_up(const char *apn, int cid, int baud, char *transcript, size_t transcript_len);

/* The context from the most recent gsm_bring_up() — sim/reg/rssi/failure. */
const gsm_ctx_t *gsm_last_ctx(void);

/* Power on the modem, bring the AT session up, then hand the UART to a PPPoS
 * netif.  Returns true once the PPP netif is attached and started.
 *
 * NOT IMPLEMENTED YET: the UART<->PPPoS driver attach is deliberately left out
 * (see docs/GSM-UPSTREAM-INTEGRATION.md §"PPP attach").  Writing it requires
 * the pppos_create()/pppos_input() driver wiring, which cannot be validated
 * without a modem attached, so gsm_start() currently stops after bring-up and
 * returns false rather than shipping unverified glue.  Use gsm_bring_up() to
 * validate the radio/SIM today. */
bool gsm_start(const char *apn, int cid, int baud);

/* Release the UART/PPP resources (keeps the modem powered). */
void gsm_stop(void);

#endif /* !TEST_HOST */

#ifdef __cplusplus
}
#endif

#endif /* GSM_UPSTREAM_H */
