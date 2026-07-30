/*
 * test_relay_simulation.c — 3-hop relay chain simulation.
 *
 * Topology:
 *
 *   ground_origin  →  balloon1  →  balloon2  →  ground_destination
 *      (hop 0)         (hop 1)       (hop 2)        (hop 3)
 *
 * Exercises the full ehash relay stack: pack → unpack → forward → repack
 * at each hop, verifying integrity, hop-count tracking, payload
 * preservation, and max_hops enforcement.
 *
 * Runs 1000 iterations for TEMPLATE (100 B), NONCE (16 B), and
 * PAYMENT (64 B) messages, asserting zero packet loss.
 */
#include "test_framework.h"
#include "../../components/ehash_relay/include/ehash_relay.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ================================================================== */
/* Constants                                                          */
/* ================================================================== */

#define GROUND_ORIGIN_ID      0x10000001u
#define BALLOON1_ID           0x20000002u
#define BALLOON2_ID           0x30000003u
#define GROUND_DESTINATION_ID 0x40000004u

#define CHAIN_MAX_HOPS        4
#define ITERATIONS            1000

/* Wire buffer must hold header (9) + max payload + CRC (2). */
#define WIRE_BUF_SIZE         300

/* ================================================================== */
/* Helpers                                                            */
/* ================================================================== */

static void fill_payload(uint8_t *buf, uint16_t len, uint8_t seed)
{
    for (uint16_t i = 0; i < len; i++)
        buf[i] = (uint8_t)(seed + i);
}

/*
 * Run one complete 3-hop relay simulation for a single message.
 *
 *   ground_origin  --pack--> wire_a
 *   balloon1       <--unpack-- wire_a, forward, --pack--> wire_b
 *   balloon2       <--unpack-- wire_b, forward, --pack--> wire_a
 *   ground_dest    <--unpack-- wire_a, forward (in-place)
 *
 * Two alternating wire buffers prevent aliasing between the payload
 * pointer (which references the old buffer) and the repack destination.
 *
 * @return 0 on success; a positive error code on any failure.
 */
static int simulate_3hop(uint8_t msg_type,
                          const uint8_t *origin_payload,
                          uint16_t payload_len)
{
    uint8_t wire_a[WIRE_BUF_SIZE];
    uint8_t wire_b[WIRE_BUF_SIZE];

    /* --- Origin: pack the packet --- */
    int packed = ehash_packet_pack(msg_type, 0, CHAIN_MAX_HOPS,
                                    GROUND_ORIGIN_ID,
                                    origin_payload, payload_len,
                                    wire_a, sizeof(wire_a));
    if (packed <= 0) return 100;

    /* --- Hop 1: ground_origin → balloon1 --- */
    ehash_packet_t pkt;
    if (ehash_packet_unpack(wire_a, (size_t)packed, &pkt) != EHASH_OK)
        return 101;
    if (pkt.msg_type != msg_type)           return 102;
    if (pkt.hop_count != 0)                 return 103;
    if (pkt.sender_id != GROUND_ORIGIN_ID)  return 104;
    if (ehash_relay_forward(&pkt, BALLOON1_ID) != EHASH_OK)
        return 105;
    /* Repack into wire_b for the next link. */
    packed = ehash_packet_pack(pkt.msg_type, pkt.hop_count, pkt.max_hops,
                                pkt.sender_id, pkt.payload, pkt.payload_len,
                                wire_b, sizeof(wire_b));
    if (packed <= 0) return 106;

    /* --- Hop 2: balloon1 → balloon2 --- */
    if (ehash_packet_unpack(wire_b, (size_t)packed, &pkt) != EHASH_OK)
        return 107;
    if (pkt.msg_type != msg_type)      return 108;
    if (pkt.hop_count != 1)            return 109;
    if (pkt.sender_id != BALLOON1_ID)  return 110;
    if (ehash_relay_forward(&pkt, BALLOON2_ID) != EHASH_OK)
        return 111;
    packed = ehash_packet_pack(pkt.msg_type, pkt.hop_count, pkt.max_hops,
                                pkt.sender_id, pkt.payload, pkt.payload_len,
                                wire_a, sizeof(wire_a));
    if (packed <= 0) return 112;

    /* --- Hop 3: balloon2 → ground_destination --- */
    if (ehash_packet_unpack(wire_a, (size_t)packed, &pkt) != EHASH_OK)
        return 113;
    if (pkt.msg_type != msg_type)      return 114;
    if (pkt.hop_count != 2)            return 115;
    if (pkt.sender_id != BALLOON2_ID)  return 116;
    if (ehash_relay_forward(&pkt, GROUND_DESTINATION_ID) != EHASH_OK)
        return 117;

    /* --- Destination assertions --- */

    /* hop_count == 3 at destination. */
    if (pkt.hop_count != 3)                 return 118;
    if (pkt.sender_id != GROUND_DESTINATION_ID) return 119;

    /* Payload identical — 0 corruption. */
    if (payload_len > 0) {
        if (pkt.payload_len != payload_len) return 120;
        if (memcmp(pkt.payload, origin_payload, payload_len) != 0)
            return 121;
    }

    /* msg_type preserved through all hops. */
    if (pkt.msg_type != msg_type) return 122;

    return 0;  /* success */
}

/*
 * Test that forwarding beyond max_hops returns EHASH_ERR_MAX_HOPS.
 *
 * With CHAIN_MAX_HOPS = 4:
 *   hops 1-4 succeed (hop_count 1..4, all ≤ max_hops).
 *   hop  5  fails    (hop_count 5 > max_hops 4).
 */
static void test_max_hops_exceeded(void)
{
    printf("\n--- max_hops exceeded: hop 5 returns EHASH_ERR_MAX_HOPS ---\n");

    ehash_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.msg_type    = EHASH_MSG_TEMPLATE;
    pkt.hop_count   = 0;
    pkt.max_hops    = CHAIN_MAX_HOPS;
    pkt.sender_id   = GROUND_ORIGIN_ID;
    pkt.payload_len = 0;
    pkt.payload     = NULL;

    /* Hops 1 through 4 should all succeed. */
    ASSERT_EQ_INT(EHASH_OK, ehash_relay_forward(&pkt, BALLOON1_ID),
                  "hop 1 OK");
    ASSERT_EQ_INT(EHASH_OK, ehash_relay_forward(&pkt, BALLOON2_ID),
                  "hop 2 OK");
    ASSERT_EQ_INT(EHASH_OK, ehash_relay_forward(&pkt, GROUND_DESTINATION_ID),
                  "hop 3 OK");
    ASSERT_EQ_INT(EHASH_OK, ehash_relay_forward(&pkt, 0x50000005),
                  "hop 4 == max_hops OK");
    ASSERT_EQ_INT(CHAIN_MAX_HOPS, (int)pkt.hop_count,
                  "hop_count == max_hops (4)");

    /* Hop 5 exceeds max_hops → error. */
    ASSERT_EQ_INT(EHASH_ERR_MAX_HOPS,
                  ehash_relay_forward(&pkt, 0x60000006),
                  "hop 5 > max_hops returns EHASH_ERR_MAX_HOPS");
}

/* ================================================================== */
/* Main                                                               */
/* ================================================================== */

int main(void)
{
    printf("=== test_relay_simulation ===\n");
    printf("Chain: ground_origin(0x%08X) → balloon1(0x%08X) "
           "→ balloon2(0x%08X) → ground_dest(0x%08X)\n",
           GROUND_ORIGIN_ID, BALLOON1_ID, BALLOON2_ID,
           GROUND_DESTINATION_ID);

    /* --- Single-run verbose tests --- */

    printf("\n--- single run: TEMPLATE (100 B) ---\n");
    {
        uint8_t payload[100];
        fill_payload(payload, sizeof(payload), 0x10);
        int rc = simulate_3hop(EHASH_MSG_TEMPLATE, payload, sizeof(payload));
        ASSERT_EQ_INT(0, rc, "TEMPLATE 3-hop clean (single run)");
    }

    printf("\n--- single run: NONCE (16 B) ---\n");
    {
        uint8_t payload[EHASH_NONCE_PAYLOAD_LEN];
        fill_payload(payload, sizeof(payload), 0x20);
        int rc = simulate_3hop(EHASH_MSG_NONCE, payload, sizeof(payload));
        ASSERT_EQ_INT(0, rc, "NONCE 3-hop clean (single run)");
    }

    printf("\n--- single run: PAYMENT (64 B) ---\n");
    {
        uint8_t payload[64];
        fill_payload(payload, sizeof(payload), 0x30);
        int rc = simulate_3hop(EHASH_MSG_PAYMENT, payload, sizeof(payload));
        ASSERT_EQ_INT(0, rc, "PAYMENT 3-hop clean (single run)");
    }

    /* --- Max hops enforcement --- */
    test_max_hops_exceeded();

    /* --- 1000-iteration stress loop --- */
    printf("\n--- stress loop: %d iterations × 3 message types ---\n",
           ITERATIONS);

    int failures      = 0;
    int total_packets = 0;

    for (int i = 0; i < ITERATIONS; i++) {
        /* TEMPLATE: 100 bytes. */
        {
            uint8_t payload[100];
            uint8_t seed = (uint8_t)(i & 0xFF);
            fill_payload(payload, sizeof(payload), seed);
            int rc = simulate_3hop(EHASH_MSG_TEMPLATE, payload, sizeof(payload));
            total_packets++;
            if (rc != 0) {
                failures++;
                if (failures <= 3) {
                    printf("  FAIL: iter %d TEMPLATE rc=%d\n", i, rc);
                }
            }
        }

        /* NONCE: 16 bytes. */
        {
            uint8_t payload[EHASH_NONCE_PAYLOAD_LEN];
            uint8_t seed = (uint8_t)((i + 1) & 0xFF);
            fill_payload(payload, sizeof(payload), seed);
            int rc = simulate_3hop(EHASH_MSG_NONCE, payload, sizeof(payload));
            total_packets++;
            if (rc != 0) {
                failures++;
                if (failures <= 3) {
                    printf("  FAIL: iter %d NONCE rc=%d\n", i, rc);
                }
            }
        }

        /* PAYMENT: 64 bytes. */
        {
            uint8_t payload[64];
            uint8_t seed = (uint8_t)((i + 2) & 0xFF);
            fill_payload(payload, sizeof(payload), seed);
            int rc = simulate_3hop(EHASH_MSG_PAYMENT, payload, sizeof(payload));
            total_packets++;
            if (rc != 0) {
                failures++;
                if (failures <= 3) {
                    printf("  FAIL: iter %d PAYMENT rc=%d\n", i, rc);
                }
            }
        }
    }

    int clean = total_packets - failures;
    printf("\n  Total packets sent: %d\n", total_packets);
    printf("  Clean deliveries:   %d\n", clean);
    printf("  Failures:           %d\n", failures);

    if (failures == 0) {
        printf("PASS: %d/%d iterations clean (0 packet loss)\n",
               ITERATIONS, ITERATIONS);
        g_tests_passed++;
    } else {
        printf("FAIL: %d/%d iterations had failures (%d packet loss)\n",
               failures, ITERATIONS, failures);
        g_tests_failed++;
    }

    ASSERT_EQ_INT(0, failures, "0 relay failures across all iterations");

    TEST_SUMMARY();
}
