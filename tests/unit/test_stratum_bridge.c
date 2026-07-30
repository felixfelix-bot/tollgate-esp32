/*
 * test_stratum_bridge.c — Stratum bridge protocol tests.
 *
 * Verifies the mapping between E-Hash L7 relay NONCE messages and
 * Stratum mining-protocol share/job fields. Exercises:
 *
 *   1. Stratum job creation with realistic values.
 *   2. NONCE payload ↔ stratum share field mapping (big-endian).
 *   3. Difficulty target validation (share hash below/above target).
 *   4. Job lifecycle (set / get round-trip).
 *   5. Stats counters (shares accepted / rejected).
 *   6. stratum submit JSON built from decoded share fields.
 *
 * The proxy lifecycle functions (set_job / get_current_job / get_stats)
 * live in tollgate_core_stratum_proxy.c which requires many ESP-IDF
 * stubs to link on-host.  This test therefore simulates those state
 * holders locally while exercising the real ehash relay and stratum
 * client code paths.
 */
#include "test_framework.h"
#include "../../components/ehash_relay/include/ehash_relay.h"
#include "../../components/tollgate_core/src/tollgate_core_stratum_client.h"
#include <cjson/cJSON.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/* ================================================================== */
/* Helpers                                                            */
/* ================================================================== */

/* Big-endian 32-bit writers/readers — match the ehash wire format.    */
static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)(v);
}

static uint32_t get_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |
           (uint32_t)p[3];
}

/*
 * Difficulty target check — returns true when @p share_hash is
 * numerically ≤ @p target (i.e. the share meets the difficulty).
 *
 * Comparison is big-endian / most-significant-byte first over the
 * leading @p target_len bytes.
 */
static bool check_share_target(const uint8_t share_hash[32],
                                const uint8_t target[32],
                                int target_len)
{
    for (int i = 0; i < target_len; i++) {
        if (share_hash[i] < target[i]) return true;   /* strictly below  */
        if (share_hash[i] > target[i]) return false;   /* strictly above  */
        /* equal so far → continue to next byte */
    }
    return true; /* exactly equal counts as meeting the target */
}

/* Convert a 32-bit value to an 8-char hex string (for build_submit).  */
static void u32_to_hex(uint32_t v, char *out)
{
    snprintf(out, 9, "%08x", v);
}

/* ================================================================== */
/* Simulated stratum proxy state                                       */
/* ================================================================== */

/* Local stand-ins for tollgate_core_stratum_proxy_{set_job,get_current_job}. */
static tollgate_stratum_job_t g_current_job;
static bool g_job_valid = false;

static void sim_set_job(const tollgate_stratum_job_t *job)
{
    if (!job) { g_job_valid = false; return; }
    memcpy(&g_current_job, job, sizeof(g_current_job));
    g_job_valid = true;
}

static const tollgate_stratum_job_t *sim_get_current_job(void)
{
    return g_job_valid ? &g_current_job : NULL;
}

/* Local stand-in for share/stat counters. */
static uint64_t g_total_shares   = 0;
static uint64_t g_total_accepted = 0;
static uint64_t g_total_rejected = 0;

static void sim_submit_share(bool accepted)
{
    g_total_shares++;
    if (accepted) g_total_accepted++;
    else          g_total_rejected++;
}

/* ================================================================== */
/* 1. Stratum job creation                                             */
/* ================================================================== */

static void test_job_creation(void)
{
    printf("\n--- stratum job creation ---\n");

    tollgate_stratum_job_t job;
    memset(&job, 0, sizeof(job));
    job.job_id  = 42;
    job.nbits   = 0x1d00ffff;                       /* Bitcoin difficulty-1 */
    job.ntime   = (uint32_t)time(NULL);
    job.version = 0x20000000;
    memset(job.prevhash, 0, 32);                     /* 32 zero bytes       */
    memset(job.merkle_root, 0xAA, 32);
    job.target_len = 32;
    memset(job.target, 0xFF, 32);
    job.target[0] = 0x00;                            /* easy target         */
    job.valid = true;

    ASSERT_EQ_INT(42, (int)job.job_id, "job_id == 42");
    ASSERT_EQ_INT((int)0x1d00ffff, (int)job.nbits, "nbits == 0x1d00ffff");
    ASSERT(job.ntime > 0, "ntime non-zero (current time)");
    ASSERT(job.valid, "job.valid == true");

    /* Verify prevhash is exactly 32 zero bytes. */
    bool all_zero = true;
    for (int i = 0; i < 32; i++) {
        if (job.prevhash[i] != 0) { all_zero = false; break; }
    }
    ASSERT(all_zero, "prevhash is 32 zero bytes");

    /* Target: first byte 0x00, remaining 31 bytes 0xFF. */
    ASSERT_EQ_INT(0x00, (int)job.target[0], "target[0] == 0x00 (easy)");
    bool rest_ff = true;
    for (int i = 1; i < 32; i++) {
        if (job.target[i] != 0xFF) { rest_ff = false; break; }
    }
    ASSERT(rest_ff, "target[1..31] all 0xFF");
    ASSERT_EQ_INT(32, job.target_len, "target_len == 32");
}

/* ================================================================== */
/* 2. NONCE payload ↔ stratum share field mapping                     */
/* ================================================================== */

static void test_nonce_payload_mapping(void)
{
    printf("\n--- NONCE payload maps to stratum share fields (big-endian) ---\n");

    const uint32_t job_id      = 42;
    const uint32_t extranonce2 = 0xDEADBEEF;
    const uint32_t ntime       = 0x65A3B2C1;
    const uint32_t nonce       = 0x12345678;

    /* Build the fixed 16-byte NONCE payload (big-endian fields). */
    uint8_t payload[EHASH_NONCE_PAYLOAD_LEN];
    put_be32(&payload[0],  job_id);
    put_be32(&payload[4],  extranonce2);
    put_be32(&payload[8],  ntime);
    put_be32(&payload[12], nonce);

    /* Pack as an ehash NONCE message at the origin. */
    uint8_t wire[64];
    int packed = ehash_packet_pack(EHASH_MSG_NONCE, 0, EHASH_DEFAULT_MAX_HOPS,
                                    0x10000001, payload, EHASH_NONCE_PAYLOAD_LEN,
                                    wire, sizeof(wire));
    ASSERT(packed > 0, "pack NONCE succeeds");
    ASSERT_EQ_INT(EHASH_HEADER_SIZE + EHASH_NONCE_PAYLOAD_LEN + EHASH_CRC_SIZE,
                  packed, "packed size = header + 16 + crc");

    /* Unpack at the relay / pool side. */
    ehash_packet_t pkt;
    int rc = ehash_packet_unpack(wire, (size_t)packed, &pkt);
    ASSERT_EQ_INT(EHASH_OK, rc, "unpack NONCE ok");
    ASSERT_EQ_INT(EHASH_MSG_NONCE, (int)pkt.msg_type, "msg_type == NONCE");
    ASSERT_EQ_INT(EHASH_NONCE_PAYLOAD_LEN, (int)pkt.payload_len,
                  "payload_len == 16");

    /* Extract stratum share fields from the NONCE payload. */
    uint32_t dec_job_id      = get_be32(&pkt.payload[0]);
    uint32_t dec_extranonce2 = get_be32(&pkt.payload[4]);
    uint32_t dec_ntime       = get_be32(&pkt.payload[8]);
    uint32_t dec_nonce       = get_be32(&pkt.payload[12]);

    ASSERT_EQ_INT((int)job_id,      (int)dec_job_id,      "bytes 0-3: job_id");
    ASSERT_EQ_INT((int)extranonce2, (int)dec_extranonce2, "bytes 4-7: extranonce2");
    ASSERT_EQ_INT((int)ntime,       (int)dec_ntime,       "bytes 8-11: ntime");
    ASSERT_EQ_INT((int)nonce,       (int)dec_nonce,       "bytes 12-15: nonce");
}

/* ================================================================== */
/* 3. Difficulty / target validation                                  */
/* ================================================================== */

static void test_difficulty_below_target(void)
{
    printf("\n--- difficulty: share hash below target → PASS ---\n");

    uint8_t target[32];
    memset(target, 0xFF, 32);
    target[0] = 0x00;                     /* easy: MSB must be 0x00 */

    /* A share hash whose first byte is 0x00 and second byte < 0xFF. */
    uint8_t share[32];
    memset(share, 0, 32);
    share[1] = 0x7F;

    ASSERT(check_share_target(share, target, 32),
           "hash {0x00, 0x7F, ...} below target {0x00, 0xFF, ...}");
}

static void test_difficulty_above_target(void)
{
    printf("\n--- difficulty: share hash above target → FAIL ---\n");

    uint8_t target[32];
    memset(target, 0xFF, 32);
    target[0] = 0x00;

    /* A share hash whose first byte is 0x01 — already above 0x00. */
    uint8_t share[32];
    memset(share, 0, 32);
    share[0] = 0x01;

    ASSERT(!check_share_target(share, target, 32),
           "hash {0x01, ...} above target {0x00, ...} rejected");
}

static void test_difficulty_at_boundary(void)
{
    printf("\n--- difficulty: share hash exactly at target → PASS ---\n");

    uint8_t target[32];
    memset(target, 0xFF, 32);
    target[0] = 0x00;

    /* Equal to target byte-for-byte → meets difficulty. */
    uint8_t share[32];
    share[0] = 0x00;
    memset(&share[1], 0xFF, 31);

    ASSERT(check_share_target(share, target, 32),
           "hash exactly equal to target accepted");
}

static void test_difficulty_strict_target(void)
{
    printf("\n--- difficulty: stricter target (first 2 bytes must be 0) ---\n");

    uint8_t target[32];
    memset(target, 0xFF, 32);
    target[0] = 0x00;
    target[1] = 0x00;    /* harder: first two bytes must be zero */

    /* Hash with first byte 0x00 but second byte 0x01 → above target. */
    uint8_t share_fail[32];
    memset(share_fail, 0, 32);
    share_fail[1] = 0x01;
    ASSERT(!check_share_target(share_fail, target, 32),
           "hash {0x00, 0x01, ...} fails stricter target {0x00, 0x00, ...}");

    /* Hash with first two bytes 0x00 → meets target. */
    uint8_t share_pass[32];
    memset(share_pass, 0, 32);
    share_pass[2] = 0xFF;
    ASSERT(check_share_target(share_pass, target, 32),
           "hash {0x00, 0x00, 0xFF, ...} meets stricter target");
}

/* ================================================================== */
/* 4. Job lifecycle: set_job / get_current_job round-trip            */
/* ================================================================== */

static void test_job_lifecycle(void)
{
    printf("\n--- job lifecycle: set_job → get_current_job round-trip ---\n");

    tollgate_stratum_job_t job;
    memset(&job, 0, sizeof(job));
    job.job_id  = 42;
    job.nbits   = 0x1d00ffff;
    job.ntime   = 0x65A3B2C1;
    job.version = 0x20000000;
    job.target_len = 32;
    for (int i = 0; i < 32; i++) {
        job.prevhash[i]    = (uint8_t)(i + 1);
        job.merkle_root[i] = (uint8_t)(0xFF - i);
        job.target[i]      = (i < 2) ? 0x00 : 0xFF;
    }
    job.valid = true;

    /* Before set_job, get_current_job should be NULL. */
    sim_set_job(NULL); /* clear */
    ASSERT(sim_get_current_job() == NULL,
           "get_current_job NULL before set_job");

    sim_set_job(&job);
    const tollgate_stratum_job_t *cur = sim_get_current_job();
    ASSERT(cur != NULL, "get_current_job returns non-NULL after set_job");

    ASSERT_EQ_INT((int)job.job_id,  (int)cur->job_id,  "job_id matches");
    ASSERT_EQ_INT((int)job.nbits,   (int)cur->nbits,   "nbits matches");
    ASSERT_EQ_INT((int)job.ntime,   (int)cur->ntime,   "ntime matches");
    ASSERT_EQ_INT((int)job.version, (int)cur->version, "version matches");
    ASSERT_EQ_INT(job.target_len, cur->target_len, "target_len matches");
    ASSERT(cur->valid == job.valid, "valid flag matches");
    ASSERT_MEM_EQ(job.prevhash,    cur->prevhash,    32, "prevhash matches");
    ASSERT_MEM_EQ(job.merkle_root, cur->merkle_root, 32, "merkle_root matches");
    ASSERT_MEM_EQ(job.target,      cur->target,      32, "target matches");
}

/* ================================================================== */
/* 5. Stats tracking: share submission counters                       */
/* ================================================================== */

static void test_stats_tracking(void)
{
    printf("\n--- stats tracking: share counters increment ---\n");

    /* Reset. */
    g_total_shares = g_total_accepted = g_total_rejected = 0;

    /* 10 accepted shares. */
    for (int i = 0; i < 10; i++) sim_submit_share(true);
    ASSERT_EQ_INT(10, (int)g_total_shares,   "total_shares == 10");
    ASSERT_EQ_INT(10, (int)g_total_accepted, "total_accepted == 10");
    ASSERT_EQ_INT(0,  (int)g_total_rejected, "total_rejected == 0");

    /* 3 rejected shares. */
    for (int i = 0; i < 3; i++) sim_submit_share(false);
    ASSERT_EQ_INT(13, (int)g_total_shares,   "total_shares == 13");
    ASSERT_EQ_INT(10, (int)g_total_accepted, "total_accepted still 10");
    ASSERT_EQ_INT(3,  (int)g_total_rejected, "total_rejected == 3");

    /* Mixed batch: 5 accepted + 2 rejected. */
    for (int i = 0; i < 5; i++) sim_submit_share(true);
    for (int i = 0; i < 2; i++) sim_submit_share(false);
    ASSERT_EQ_INT(20, (int)g_total_shares,   "total_shares == 20");
    ASSERT_EQ_INT(15, (int)g_total_accepted, "total_accepted == 15");
    ASSERT_EQ_INT(5,  (int)g_total_rejected, "total_rejected == 5");
}

/* ================================================================== */
/* 6. Build stratum submit JSON from decoded NONCE share fields       */
/* ================================================================== */

static void test_build_submit_from_nonce(void)
{
    printf("\n--- build stratum submit JSON from decoded NONCE fields ---\n");

    const uint32_t job_id      = 42;
    const uint32_t extranonce2 = 0xDEADBEEF;
    const uint32_t ntime       = 0x65A3B2C1;
    const uint32_t nonce       = 0x12345678;

    /* Simulate a NONCE message arriving over the relay. */
    uint8_t nonce_payload[EHASH_NONCE_PAYLOAD_LEN];
    put_be32(&nonce_payload[0],  job_id);
    put_be32(&nonce_payload[4],  extranonce2);
    put_be32(&nonce_payload[8],  ntime);
    put_be32(&nonce_payload[12], nonce);

    uint8_t wire[64];
    int packed = ehash_packet_pack(EHASH_MSG_NONCE, 2, EHASH_DEFAULT_MAX_HOPS,
                                    0x30000003,
                                    nonce_payload, EHASH_NONCE_PAYLOAD_LEN,
                                    wire, sizeof(wire));
    ASSERT(packed > 0, "pack NONCE at hop 2");

    ehash_packet_t pkt;
    ASSERT_EQ_INT(EHASH_OK,
                  ehash_packet_unpack(wire, (size_t)packed, &pkt),
                  "unpack NONCE");

    /* Decode the share fields exactly as the stratum bridge would. */
    uint32_t dec_job_id = get_be32(&pkt.payload[0]);
    uint32_t dec_extra2 = get_be32(&pkt.payload[4]);
    uint32_t dec_ntime  = get_be32(&pkt.payload[8]);
    uint32_t dec_nonce  = get_be32(&pkt.payload[12]);

    /* Convert extranonce2 to hex for the stratum submit call. */
    char extra2_hex[9];
    u32_to_hex(dec_extra2, extra2_hex);

    /* Build the mining.submit JSON the pool expects. */
    char buf[512];
    int json_len = tollgate_core_stratum_build_submit(buf, sizeof(buf), 7,
                                                       "balloon.worker",
                                                       dec_job_id,
                                                       extra2_hex,
                                                       dec_ntime, dec_nonce);
    ASSERT(json_len > 0, "build_submit returns positive length");

    /* Parse and validate the JSON. */
    cJSON *msg = cJSON_Parse(buf);
    ASSERT(msg != NULL, "build_submit output is valid JSON");
    if (msg) {
        cJSON *method = cJSON_GetObjectItem(msg, "method");
        ASSERT(method && cJSON_IsString(method),
               "JSON has string \"method\" field");
        if (method) {
            ASSERT_EQ_STR("mining.submit", method->valuestring,
                          "method == \"mining.submit\"");
        }

        cJSON *id = cJSON_GetObjectItem(msg, "id");
        ASSERT(id && cJSON_IsNumber(id), "JSON has numeric \"id\"");
        if (id) {
            ASSERT_EQ_INT(7, id->valueint, "id == 7");
        }

        cJSON *params = cJSON_GetObjectItem(msg, "params");
        ASSERT(params && cJSON_IsArray(params),
               "JSON has \"params\" array");
        if (params) {
            ASSERT_EQ_INT(5, cJSON_GetArraySize(params),
                          "params has 5 elements");
            cJSON *p0 = cJSON_GetArrayItem(params, 0);
            ASSERT(p0 && cJSON_IsString(p0), "params[0] is string (user)");
            if (p0) {
                ASSERT_EQ_STR("balloon.worker", p0->valuestring,
                              "params[0] == \"balloon.worker\"");
            }
        }

        cJSON_Delete(msg);
    }
}

/* ================================================================== */
/* 7. Full bridge: NONCE over relay → decode → stratum submit         */
/* ================================================================== */

static void test_full_bridge_roundtrip(void)
{
    printf("\n--- full bridge: pack NONCE → relay forward → decode share ---\n");

    /* Miner finds a share: job 99, nonce 0xCAFEBABE. */
    uint32_t job_id = 99, extranonce2 = 0x00000001, ntime = 0x5F000000, nonce = 0xCAFEBABE;

    uint8_t payload[EHASH_NONCE_PAYLOAD_LEN];
    put_be32(&payload[0],  job_id);
    put_be32(&payload[4],  extranonce2);
    put_be32(&payload[8],  ntime);
    put_be32(&payload[12], nonce);

    /* Miner packs and sends via the relay chain (1 hop). */
    uint8_t wire_tx[64], wire_rx[64];
    int packed = ehash_packet_pack(EHASH_MSG_NONCE, 0, 4, 0x10000001,
                                    payload, sizeof(payload),
                                    wire_tx, sizeof(wire_tx));
    ASSERT(packed > 0, "miner packs NONCE");

    /* Balloon receives, forwards 1 hop, repacks. */
    ehash_packet_t pkt;
    ASSERT_EQ_INT(EHASH_OK,
                  ehash_packet_unpack(wire_tx, (size_t)packed, &pkt),
                  "balloon unpacks NONCE");
    ASSERT_EQ_INT(EHASH_OK,
                  ehash_relay_forward(&pkt, 0x20000002),
                  "balloon forwards NONCE");
    ASSERT_EQ_INT(1, (int)pkt.hop_count, "hop_count == 1 at balloon exit");

    int repacked = ehash_packet_pack(pkt.msg_type, pkt.hop_count, pkt.max_hops,
                                      pkt.sender_id, pkt.payload, pkt.payload_len,
                                      wire_rx, sizeof(wire_rx));
    ASSERT(repacked > 0, "balloon repacks NONCE");

    /* Pool / ground station receives and decodes. */
    ehash_packet_t pool_pkt;
    ASSERT_EQ_INT(EHASH_OK,
                  ehash_packet_unpack(wire_rx, (size_t)repacked, &pool_pkt),
                  "pool unpacks NONCE");
    ASSERT_EQ_INT(EHASH_MSG_NONCE, (int)pool_pkt.msg_type,
                  "pool sees NONCE msg_type");
    ASSERT_EQ_INT(1, (int)pool_pkt.hop_count, "pool sees hop_count == 1");

    /* Decode share fields at the pool. */
    uint32_t pool_job_id = get_be32(&pool_pkt.payload[0]);
    uint32_t pool_nonce  = get_be32(&pool_pkt.payload[12]);
    ASSERT_EQ_INT((int)job_id, (int)pool_job_id, "pool decodes job_id 99");
    ASSERT_EQ_INT((int)nonce, (int)pool_nonce, "pool decodes nonce 0xCAFEBABE");

    /* Payload integrity: 0 corruption through relay. */
    ASSERT_MEM_EQ(payload, pool_pkt.payload, EHASH_NONCE_PAYLOAD_LEN,
                  "payload identical after relay hop");
}

/* ================================================================== */
/* Main                                                               */
/* ================================================================== */

int main(void)
{
    printf("=== test_stratum_bridge ===\n");

    test_job_creation();

    test_nonce_payload_mapping();

    test_difficulty_below_target();
    test_difficulty_above_target();
    test_difficulty_at_boundary();
    test_difficulty_strict_target();

    test_job_lifecycle();

    test_stats_tracking();

    test_build_submit_from_nonce();

    test_full_bridge_roundtrip();

    TEST_SUMMARY();
}
