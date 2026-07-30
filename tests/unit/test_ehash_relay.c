/*
 * test_ehash_relay.c — Unit tests for the E-Hash L7 relay handler.
 *
 * Covers:
 *   1. Pack/unpack round-trip for all 3 message types.
 *   2. CRC16 validation: correct CRC passes, corrupted CRC fails.
 *   3. Hop count tracking: forward() increments hop_count.
 *   4. Max hops exceeded: forward() returns EHASH_ERR_MAX_HOPS.
 *   5. Malformed packet: truncated buffer returns error.
 *   6. Payload size boundaries: 0 bytes (min) and 255 bytes.
 *   7. Template message: 80–120 byte payload (realistic block template).
 *   8. Nonce message: exactly 16 bytes payload.
 *   9. Payment message: 50–80 byte payload (Cashu token).
 *  10. Error paths: NULL args, bad msg type, tiny output buffer.
 */
#include "test_framework.h"
#include "../../components/ehash_relay/include/ehash_relay.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

/* Build a deterministic non-zero payload so round-trips are checkable. */
static void fill_payload(uint8_t *buf, uint16_t len, uint8_t seed)
{
    for (uint16_t i = 0; i < len; i++) {
        buf[i] = (uint8_t)(seed + i);
    }
}

/* ------------------------------------------------------------------ */
/* 1. Round-trip for all three message types                          */
/* ------------------------------------------------------------------ */

static void test_roundtrip_template(void)
{
    printf("\n--- round-trip: TEMPLATE (msg_type 0x%02x) ---\n", EHASH_MSG_TEMPLATE);
    uint8_t payload[100];
    fill_payload(payload, sizeof(payload), 0x10);

    uint8_t wire[256];
    int packed = ehash_packet_pack(EHASH_MSG_TEMPLATE, 0, EHASH_DEFAULT_MAX_HOPS,
                                    0xCAFEBABE, payload, sizeof(payload),
                                    wire, sizeof(wire));
    ASSERT(packed > 0, "pack TEMPLATE succeeds");
    ASSERT_EQ_INT((int)(EHASH_HEADER_SIZE + sizeof(payload) + EHASH_CRC_SIZE), packed,
                  "packed size = header + payload + crc");

    ehash_packet_t pkt;
    int rc = ehash_packet_unpack(wire, (size_t)packed, &pkt);
    ASSERT_EQ_INT(EHASH_OK, rc, "unpack TEMPLATE ok");
    ASSERT_EQ_INT(EHASH_MSG_TEMPLATE, (int)pkt.msg_type, "msg_type preserved");
    ASSERT_EQ_INT(0, (int)pkt.hop_count, "hop_count preserved");
    ASSERT_EQ_INT(EHASH_DEFAULT_MAX_HOPS, (int)pkt.max_hops, "max_hops preserved");
    ASSERT_EQ_INT((int)0xCAFEBABE, (int)pkt.sender_id, "sender_id preserved");
    ASSERT_EQ_INT((int)sizeof(payload), (int)pkt.payload_len, "payload_len preserved");
    ASSERT_MEM_EQ(payload, pkt.payload, sizeof(payload), "payload bytes preserved");
}

static void test_roundtrip_nonce(void)
{
    printf("\n--- round-trip: NONCE (msg_type 0x%02x) ---\n", EHASH_MSG_NONCE);

    /* job_id(4) + extranonce2(4) + ntime(4) + nonce(4) = 16 bytes. */
    uint8_t payload[EHASH_NONCE_PAYLOAD_LEN] = {
        0x00, 0x00, 0x00, 0x7B,   /* job_id   = 123 */
        0xDE, 0xAD, 0xBE, 0xEF,   /* extranonce2   */
        0x65, 0xA3, 0xB2, 0xC1,   /* ntime         */
        0x12, 0x34, 0x56, 0x78    /* nonce         */
    };

    uint8_t wire[64];
    int packed = ehash_packet_pack(EHASH_MSG_NONCE, 0, 2,
                                    0x11112222, payload, sizeof(payload),
                                    wire, sizeof(wire));
    ASSERT(packed > 0, "pack NONCE succeeds");
    ASSERT_EQ_INT(EHASH_HEADER_SIZE + EHASH_NONCE_PAYLOAD_LEN + EHASH_CRC_SIZE, packed,
                  "packed size for 16-B nonce payload");

    ehash_packet_t pkt;
    ASSERT_EQ_INT(EHASH_OK, ehash_packet_unpack(wire, (size_t)packed, &pkt),
                  "unpack NONCE ok");
    ASSERT_EQ_INT(EHASH_MSG_NONCE, (int)pkt.msg_type, "msg_type NONCE");
    ASSERT_EQ_INT(EHASH_NONCE_PAYLOAD_LEN, (int)pkt.payload_len, "nonce payload_len == 16");
    ASSERT_MEM_EQ(payload, pkt.payload, sizeof(payload), "nonce payload bytes preserved");
}

static void test_roundtrip_payment(void)
{
    printf("\n--- round-trip: PAYMENT (msg_type 0x%02x) ---\n", EHASH_MSG_PAYMENT);

    /* Simulated Cashu token (~64 bytes). */
    uint8_t payload[64];
    fill_payload(payload, sizeof(payload), 0x99);

    uint8_t wire[128];
    int packed = ehash_packet_pack(EHASH_MSG_PAYMENT, 0, 1,
                                    0x0BADF00D, payload, sizeof(payload),
                                    wire, sizeof(wire));
    ASSERT(packed > 0, "pack PAYMENT succeeds");

    ehash_packet_t pkt;
    ASSERT_EQ_INT(EHASH_OK, ehash_packet_unpack(wire, (size_t)packed, &pkt),
                  "unpack PAYMENT ok");
    ASSERT_EQ_INT(EHASH_MSG_PAYMENT, (int)pkt.msg_type, "msg_type PAYMENT");
    ASSERT_MEM_EQ(payload, pkt.payload, sizeof(payload), "payment payload preserved");
}

/* ------------------------------------------------------------------ */
/* 2. CRC validation                                                  */
/* ------------------------------------------------------------------ */

static void test_crc_correct_passes(void)
{
    printf("\n--- CRC: correct CRC passes validate_crc ---\n");
    uint8_t payload[32];
    fill_payload(payload, sizeof(payload), 0x40);

    uint8_t wire[64];
    int packed = ehash_packet_pack(EHASH_MSG_TEMPLATE, 0, 4, 0x1, payload, sizeof(payload),
                                    wire, sizeof(wire));
    ASSERT(packed > 0, "pack for CRC test");
    ASSERT(ehash_packet_validate_crc(wire, (size_t)packed), "validate_crc returns true");
}

static void test_crc_corrupted_fails(void)
{
    printf("\n--- CRC: corrupted CRC fails validate_crc ---\n");
    uint8_t payload[32];
    fill_payload(payload, sizeof(payload), 0x40);

    uint8_t wire[64];
    int packed = ehash_packet_pack(EHASH_MSG_TEMPLATE, 0, 4, 0x1, payload, sizeof(payload),
                                    wire, sizeof(wire));
    ASSERT(packed > 0, "pack for corruption test");

    /* Flip a single payload byte and expect CRC failure. */
    wire[EHASH_HEADER_SIZE + 5] ^= 0xFF;
    ASSERT(!ehash_packet_validate_crc(wire, (size_t)packed), "corrupted payload CRC fails");

    /* Flip the CRC bytes directly and expect failure. */
    wire[packed - 1] ^= 0xFF;
    ASSERT(!ehash_packet_validate_crc(wire, (size_t)packed), "corrupted CRC field fails");
}

static void test_crc_unpack_rejects_corrupt(void)
{
    printf("\n--- CRC: unpack rejects corrupted packet ---\n");
    uint8_t payload[16];
    fill_payload(payload, sizeof(payload), 0x05);

    uint8_t wire[64];
    int packed = ehash_packet_pack(EHASH_MSG_NONCE, 0, 3, 0x2, payload, sizeof(payload),
                                    wire, sizeof(wire));
    ASSERT(packed > 0, "pack for corrupt-unpack test");

    /* Corrupt a header byte. */
    wire[3] ^= 0x01;
    ehash_packet_t pkt;
    int rc = ehash_packet_unpack(wire, (size_t)packed, &pkt);
    ASSERT_EQ_INT(EHASH_ERR_CRC_MISMATCH, rc, "unpack returns CRC_MISMATCH for corrupt header");
}

/* ------------------------------------------------------------------ */
/* 3 & 4. Hop count tracking and max-hops enforcement                 */
/* ------------------------------------------------------------------ */

static void test_forward_increments_hops(void)
{
    printf("\n--- forward(): hop_count increments ---\n");

    ehash_packet_t pkt = {
        .msg_type   = EHASH_MSG_TEMPLATE,
        .hop_count  = 0,
        .max_hops   = 5,
        .sender_id  = 0xAABBCCDD,
        .payload_len = 0,
        .payload    = NULL,
    };

    ASSERT_EQ_INT(EHASH_OK, ehash_relay_forward(&pkt, 0x10000001), "forward hop 1 ok");
    ASSERT_EQ_INT(1, (int)pkt.hop_count, "hop_count == 1 after first forward");
    ASSERT_EQ_INT((int)0x10000001, (int)pkt.sender_id, "sender_id updated to next hop");

    ASSERT_EQ_INT(EHASH_OK, ehash_relay_forward(&pkt, 0x20000002), "forward hop 2 ok");
    ASSERT_EQ_INT(2, (int)pkt.hop_count, "hop_count == 2 after second forward");
    ASSERT_EQ_INT((int)0x20000002, (int)pkt.sender_id, "sender_id updated again");
}

static void test_forward_max_hops_exceeded(void)
{
    printf("\n--- forward(): max_hops exceeded returns EHASH_ERR_MAX_HOPS ---\n");

    ehash_packet_t pkt = {
        .msg_type    = EHASH_MSG_NONCE,
        .hop_count   = 2,
        .max_hops    = 3,
        .sender_id   = 0x1,
        .payload_len = 0,
        .payload     = NULL,
    };

    /* hop_count 2 → 3: equals max_hops, still deliverable. */
    ASSERT_EQ_INT(EHASH_OK, ehash_relay_forward(&pkt, 0x2), "forward to hop == max_hops is ok");
    ASSERT_EQ_INT(3, (int)pkt.hop_count, "hop_count == max_hops (3)");

    /* hop_count 3 → 4: exceeds max_hops, must be dropped. */
    ASSERT_EQ_INT(EHASH_ERR_MAX_HOPS, ehash_relay_forward(&pkt, 0x3),
                  "forward beyond max_hops returns error");
    ASSERT_EQ_INT(4, (int)pkt.hop_count, "hop_count still incremented to 4");
}

static void test_forward_null(void)
{
    printf("\n--- forward(): NULL packet returns INVALID_ARG ---\n");
    ASSERT_EQ_INT(EHASH_ERR_INVALID_ARG, ehash_relay_forward(NULL, 0), "NULL packet rejected");
}

/* ------------------------------------------------------------------ */
/* 5. Malformed / truncated packet                                    */
/* ------------------------------------------------------------------ */

static void test_unpack_truncated(void)
{
    printf("\n--- unpack(): truncated buffer returns INVALID_PACKET ---\n");

    uint8_t tiny[5] = { EHASH_MSG_TEMPLATE, 0, 4, 0, 0 };
    ehash_packet_t pkt;
    ASSERT_EQ_INT(EHASH_ERR_INVALID_PACKET, ehash_packet_unpack(tiny, sizeof(tiny), &pkt),
                  "buffer shorter than header rejected");
}

static void test_unpack_truncated_payload(void)
{
    printf("\n--- unpack(): declared payload longer than buffer ---\n");

    /* Pack a real packet, then truncate the tail. */
    uint8_t payload[32];
    fill_payload(payload, sizeof(payload), 0x20);

    uint8_t wire[64];
    int packed = ehash_packet_pack(EHASH_MSG_TEMPLATE, 0, 4, 0x1, payload, sizeof(payload),
                                    wire, sizeof(wire));
    ASSERT(packed > 0, "pack for truncation test");

    /* Hand the unpacker fewer bytes than the packet actually occupies. */
    ehash_packet_t pkt;
    ASSERT_EQ_INT(EHASH_ERR_INVALID_PACKET,
                  ehash_packet_unpack(wire, (size_t)packed - 5, &pkt),
                  "truncated-by-5 buffer rejected");
}

static void test_unpack_null(void)
{
    printf("\n--- unpack(): NULL args ---\n");
    ehash_packet_t pkt;
    ASSERT_EQ_INT(EHASH_ERR_INVALID_ARG, ehash_packet_unpack(NULL, 10, &pkt),
                  "NULL buf rejected");
    uint8_t wire[16] = {0};
    ASSERT_EQ_INT(EHASH_ERR_INVALID_ARG, ehash_packet_unpack(wire, sizeof(wire), NULL),
                  "NULL out_packet rejected");
}

static void test_unpack_bad_msg_type(void)
{
    printf("\n--- unpack(): unknown message type rejected ---\n");

    /* Build a packet with a valid CRC but an invalid message type. */
    uint8_t wire[EHASH_MIN_PACKET_SIZE];
    wire[0] = 0xFF;                 /* invalid msg type */
    wire[1] = 0;                    /* hop_count */
    wire[2] = 4;                    /* max_hops */
    wire[3] = wire[4] = wire[5] = wire[6] = 0; /* sender_id = 0 */
    wire[7] = 0; wire[8] = 0;       /* payload_len = 0 */

    uint16_t crc = ehash_packet_calc_crc(wire, EHASH_HEADER_SIZE);
    wire[9]  = (uint8_t)(crc >> 8);
    wire[10] = (uint8_t)(crc & 0xFF);

    ehash_packet_t pkt;
    ASSERT_EQ_INT(EHASH_ERR_BAD_MSG_TYPE, ehash_packet_unpack(wire, sizeof(wire), &pkt),
                  "unknown msg_type rejected");
}

/* ------------------------------------------------------------------ */
/* 6. Payload size boundaries                                         */
/* ------------------------------------------------------------------ */

static void test_payload_zero_bytes(void)
{
    printf("\n--- payload boundary: 0 bytes (minimum) ---\n");
    uint8_t wire[EHASH_MIN_PACKET_SIZE];
    int packed = ehash_packet_pack(EHASH_MSG_TEMPLATE, 0, 4, 0x7, NULL, 0,
                                    wire, sizeof(wire));
    ASSERT(packed > 0, "pack with 0-byte payload succeeds");
    ASSERT_EQ_INT(EHASH_MIN_PACKET_SIZE, packed, "0-byte payload => min packet size");

    ehash_packet_t pkt;
    ASSERT_EQ_INT(EHASH_OK, ehash_packet_unpack(wire, (size_t)packed, &pkt),
                  "unpack 0-byte payload ok");
    ASSERT_EQ_INT(0, (int)pkt.payload_len, "payload_len == 0");
    ASSERT(pkt.payload == NULL, "payload pointer NULL for 0-byte payload");
}

static void test_payload_255_bytes(void)
{
    printf("\n--- payload boundary: 255 bytes ---\n");
    uint8_t payload[255];
    fill_payload(payload, sizeof(payload), 0x50);

    uint8_t wire[512];
    int packed = ehash_packet_pack(EHASH_MSG_PAYMENT, 0, 2, 0x8,
                                    payload, sizeof(payload),
                                    wire, sizeof(wire));
    ASSERT(packed > 0, "pack with 255-byte payload succeeds");
    ASSERT_EQ_INT(EHASH_HEADER_SIZE + 255 + EHASH_CRC_SIZE, packed, "255-byte payload size");

    ehash_packet_t pkt;
    ASSERT_EQ_INT(EHASH_OK, ehash_packet_unpack(wire, (size_t)packed, &pkt),
                  "unpack 255-byte payload ok");
    ASSERT_MEM_EQ(payload, pkt.payload, sizeof(payload), "255-byte payload preserved");
}

/* ------------------------------------------------------------------ */
/* 7. Template message: realistic block template (80–120 B)           */
/* ------------------------------------------------------------------ */

static void test_template_realistic(void)
{
    printf("\n--- template message: realistic 120-byte payload ---\n");

    /* prevhash(32) + merkle_root(32) + coinbase(8) + nbits/ntime/version(12)
     * + difficulty(8) + clean_jobs(1) + job_id(4) + extranonce1(4) + extra(19)
     *  ≈ 120 bytes — close to a compact binary stratum notify. */
    uint8_t payload[120];
    fill_payload(payload, sizeof(payload), 0x80);

    uint8_t wire[256];
    int packed = ehash_packet_pack(EHASH_MSG_TEMPLATE, 0, EHASH_DEFAULT_MAX_HOPS,
                                    0xBEEF1234, payload, sizeof(payload),
                                    wire, sizeof(wire));
    ASSERT(packed > 0, "pack 120-B template succeeds");

    ehash_packet_t pkt;
    ASSERT_EQ_INT(EHASH_OK, ehash_packet_unpack(wire, (size_t)packed, &pkt),
                  "unpack 120-B template ok");
    ASSERT_EQ_INT(120, (int)pkt.payload_len, "template payload_len == 120");
    ASSERT_MEM_EQ(payload, pkt.payload, 120, "template payload round-trip");
}

/* ------------------------------------------------------------------ */
/* 8. Nonce message: exactly 16 bytes (covered above, double-check)   */
/* ------------------------------------------------------------------ */

static void test_nonce_exactly_16(void)
{
    printf("\n--- nonce message: payload must be exactly 16 bytes ---\n");
    uint8_t payload[EHASH_NONCE_PAYLOAD_LEN];
    memset(payload, 0xAB, sizeof(payload));

    uint8_t wire[64];
    int packed = ehash_packet_pack(EHASH_MSG_NONCE, 0, 3, 0x9,
                                    payload, EHASH_NONCE_PAYLOAD_LEN,
                                    wire, sizeof(wire));
    ASSERT(packed > 0, "pack nonce with 16 B ok");
    ASSERT_EQ_INT(EHASH_HEADER_SIZE + EHASH_NONCE_PAYLOAD_LEN + EHASH_CRC_SIZE, packed,
                  "nonce packed size is header+16+crc");
}

/* ------------------------------------------------------------------ */
/* 9. Payment message: 50–80 byte Cashu token                         */
/* ------------------------------------------------------------------ */

static void test_payment_cashu_range(void)
{
    printf("\n--- payment message: 50–80 byte Cashu token range ---\n");

    /* Test the lower boundary (50 B) and upper boundary (80 B). */
    for (uint16_t len = 50; len <= 80; len += 15) {
        uint8_t payload[80];
        fill_payload(payload, len, (uint8_t)(len & 0xFF));

        uint8_t wire[128];
        int packed = ehash_packet_pack(EHASH_MSG_PAYMENT, 0, 1, 0xCAFE,
                                        payload, len, wire, sizeof(wire));
        ASSERT(packed > 0, "pack payment ok");

        ehash_packet_t pkt;
        ASSERT_EQ_INT(EHASH_OK, ehash_packet_unpack(wire, (size_t)packed, &pkt),
                      "unpack payment ok");
        ASSERT_EQ_INT((int)len, (int)pkt.payload_len, "payment payload_len matches");
        ASSERT_MEM_EQ(payload, pkt.payload, len, "payment payload bytes match");
    }
}

/* ------------------------------------------------------------------ */
/* 10. Error paths                                                    */
/* ------------------------------------------------------------------ */

static void test_pack_errors(void)
{
    printf("\n--- pack(): error paths ---\n");

    uint8_t payload[8] = {0};
    uint8_t wire[64];

    /* NULL output buffer. */
    ASSERT_EQ_INT(EHASH_ERR_INVALID_ARG,
                  ehash_packet_pack(EHASH_MSG_TEMPLATE, 0, 4, 0, payload, 8, NULL, 64),
                  "NULL out_buf rejected");

    /* Non-zero payload_len with NULL payload. */
    ASSERT_EQ_INT(EHASH_ERR_INVALID_ARG,
                  ehash_packet_pack(EHASH_MSG_TEMPLATE, 0, 4, 0, NULL, 8, wire, sizeof(wire)),
                  "NULL payload with len>0 rejected");

    /* Bad message type. */
    ASSERT_EQ_INT(EHASH_ERR_BAD_MSG_TYPE,
                  ehash_packet_pack(0x00, 0, 4, 0, payload, 8, wire, sizeof(wire)),
                  "msg_type 0x00 rejected");
    ASSERT_EQ_INT(EHASH_ERR_BAD_MSG_TYPE,
                  ehash_packet_pack(0xFF, 0, 4, 0, payload, 8, wire, sizeof(wire)),
                  "msg_type 0xFF rejected");

    /* Output buffer too small. */
    ASSERT_EQ_INT(EHASH_ERR_BUFFER_TOO_SMALL,
                  ehash_packet_pack(EHASH_MSG_TEMPLATE, 0, 4, 0, payload, 8, wire, 5),
                  "buf_size < needed rejected");
}

static void test_crc_calc_known_vector(void)
{
    printf("\n--- CRC16-CCITT known vector (\"123456789\" => 0x29B1) ---\n");
    /* The string "123456789" is the canonical CRC-16/CCITT-FALSE check value. */
    const uint8_t *data = (const uint8_t *)"123456789";
    uint16_t crc = ehash_packet_calc_crc(data, 9);
    ASSERT_EQ_INT(0x29B1, (int)crc, "CRC16-CCITT-FALSE(\"123456789\") == 0x29B1");
}

static void test_crc_calc_null(void)
{
    printf("\n--- CRC16 calc: NULL / zero-length returns initial value 0xFFFF ---\n");
    ASSERT_EQ_INT(0xFFFF, (int)ehash_packet_calc_crc(NULL, 0), "NULL buffer CRC == 0xFFFF");
    ASSERT_EQ_INT(0xFFFF, (int)ehash_packet_calc_crc((const uint8_t *)"", 0),
                  "zero-length CRC == 0xFFFF");
}

/* ------------------------------------------------------------------ */
/* Main                                                               */
/* ------------------------------------------------------------------ */

int main(void)
{
    printf("=== test_ehash_relay ===\n");

    test_roundtrip_template();
    test_roundtrip_nonce();
    test_roundtrip_payment();

    test_crc_correct_passes();
    test_crc_corrupted_fails();
    test_crc_unpack_rejects_corrupt();

    test_forward_increments_hops();
    test_forward_max_hops_exceeded();
    test_forward_null();

    test_unpack_truncated();
    test_unpack_truncated_payload();
    test_unpack_null();
    test_unpack_bad_msg_type();

    test_payload_zero_bytes();
    test_payload_255_bytes();

    test_template_realistic();
    test_nonce_exactly_16();
    test_payment_cashu_range();

    test_pack_errors();
    test_crc_calc_known_vector();
    test_crc_calc_null();

    TEST_SUMMARY();
}
