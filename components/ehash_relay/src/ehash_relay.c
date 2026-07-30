/**
 * @file ehash_relay.c
 * @brief E-Hash L7 LoRa relay protocol handler — implementation.
 *
 * Pure C, no ESP-IDF dependencies. Compiles with host gcc.
 *
 * @see ehash_relay.h for the wire-format specification.
 */
#include "ehash_relay.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/* Internal helpers                                                   */
/* ------------------------------------------------------------------ */

/* Big-endian byte writers. */

static void put_u16_be(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFF);
}

static void put_u32_be(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)(v & 0xFF);
}

/* Big-endian byte readers. */

static uint16_t get_u16_be(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static uint32_t get_u32_be(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |
           ((uint32_t)p[3]);
}

/* ------------------------------------------------------------------ */
/* CRC16-CCITT (poly 0x1021, init 0xFFFF, no final XOR, no reflection)*/
/* ------------------------------------------------------------------ */

uint16_t ehash_packet_calc_crc(const uint8_t *buf, size_t len)
{
    /*
     * Classic table-less CRC16-CCITT implementation.
     * Polynomial: x^16 + x^12 + x^5 + 1  →  0x1021
     * Initial value: 0xFFFF
     * Input/output: not reflected, no final XOR.
     */
    uint16_t crc = 0xFFFF;

    if (buf == NULL || len == 0) {
        return crc;
    }

    for (size_t i = 0; i < len; i++) {
        crc ^= ((uint16_t)buf[i]) << 8;
        for (int b = 0; b < 8; b++) {
            if (crc & 0x8000) {
                crc = (uint16_t)((crc << 1) ^ 0x1021);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }

    return crc;
}

/* ------------------------------------------------------------------ */
/* Pack                                                                */
/* ------------------------------------------------------------------ */

int ehash_packet_pack(uint8_t msg_type, uint8_t hop_count, uint8_t max_hops,
                      uint32_t sender_id,
                      const uint8_t *payload, uint16_t payload_len,
                      uint8_t *out_buf, size_t buf_size)
{
    /* Argument validation. */
    if (out_buf == NULL) {
        return EHASH_ERR_INVALID_ARG;
    }
    if (payload_len > 0 && payload == NULL) {
        return EHASH_ERR_INVALID_ARG;
    }
    if (!ehash_msg_type_is_valid(msg_type)) {
        return EHASH_ERR_BAD_MSG_TYPE;
    }

    /* Total bytes needed: header + payload + CRC16. */
    size_t needed = (size_t)EHASH_HEADER_SIZE + (size_t)payload_len + (size_t)EHASH_CRC_SIZE;
    if (buf_size < needed) {
        return EHASH_ERR_BUFFER_TOO_SMALL;
    }

    uint8_t *p = out_buf;

    /* Header (9 bytes). */
    p[0] = msg_type;
    p[1] = hop_count;
    p[2] = max_hops;
    put_u32_be(p + 3, sender_id);
    put_u16_be(p + 7, payload_len);

    /* Payload. */
    if (payload_len > 0) {
        memcpy(p + EHASH_HEADER_SIZE, payload, payload_len);
    }

    /* CRC16 over header + payload (everything except the CRC field). */
    size_t crc_region_len = (size_t)EHASH_HEADER_SIZE + (size_t)payload_len;
    uint16_t crc = ehash_packet_calc_crc(out_buf, crc_region_len);
    put_u16_be(out_buf + crc_region_len, crc);

    return (int)needed;
}

/* ------------------------------------------------------------------ */
/* Unpack                                                              */
/* ------------------------------------------------------------------ */

int ehash_packet_unpack(const uint8_t *buf, size_t buf_len,
                        ehash_packet_t *out_packet)
{
    if (buf == NULL || out_packet == NULL) {
        return EHASH_ERR_INVALID_ARG;
    }

    /* Need at least the header to read the length field. */
    if (buf_len < EHASH_HEADER_SIZE) {
        return EHASH_ERR_INVALID_PACKET;
    }

    /* Parse header fields first. */
    uint8_t  msg_type    = buf[0];
    uint8_t  hop_count   = buf[1];
    uint8_t  max_hops    = buf[2];
    uint32_t sender_id   = get_u32_be(buf + 3);
    uint16_t payload_len = get_u16_be(buf + 7);

    /* Validate message type. */
    if (!ehash_msg_type_is_valid(msg_type)) {
        return EHASH_ERR_BAD_MSG_TYPE;
    }

    /* Full packet size including CRC. */
    size_t total = (size_t)EHASH_HEADER_SIZE + (size_t)payload_len + (size_t)EHASH_CRC_SIZE;
    if (buf_len < total) {
        return EHASH_ERR_INVALID_PACKET;
    }

    /* Validate CRC before trusting the payload. */
    if (!ehash_packet_validate_crc(buf, total)) {
        return EHASH_ERR_CRC_MISMATCH;
    }

    /* Populate output struct. payload aliases into buf (no copy). */
    out_packet->msg_type    = msg_type;
    out_packet->hop_count   = hop_count;
    out_packet->max_hops    = max_hops;
    out_packet->sender_id   = sender_id;
    out_packet->payload_len = payload_len;
    out_packet->payload     = (payload_len > 0) ? (buf + EHASH_HEADER_SIZE) : NULL;

    return EHASH_OK;
}

/* ------------------------------------------------------------------ */
/* CRC validation                                                      */
/* ------------------------------------------------------------------ */

bool ehash_packet_validate_crc(const uint8_t *buf, size_t buf_len)
{
    if (buf == NULL || buf_len < EHASH_MIN_PACKET_SIZE) {
        return false;
    }

    /* Read the declared payload length from the header. */
    uint16_t payload_len = get_u16_be(buf + 7);

    /* Compute the full packet size the buffer claims to represent. */
    size_t total = (size_t)EHASH_HEADER_SIZE + (size_t)payload_len + (size_t)EHASH_CRC_SIZE;
    if (buf_len < total) {
        return false;
    }

    /* Recompute CRC over header + payload and compare to the stored value. */
    size_t crc_region_len = (size_t)EHASH_HEADER_SIZE + (size_t)payload_len;
    uint16_t computed = ehash_packet_calc_crc(buf, crc_region_len);
    uint16_t stored   = get_u16_be(buf + crc_region_len);

    return computed == stored;
}

/* ------------------------------------------------------------------ */
/* Relay forward                                                       */
/* ------------------------------------------------------------------ */

int ehash_relay_forward(ehash_packet_t *packet, uint32_t next_hop_id)
{
    if (packet == NULL) {
        return EHASH_ERR_INVALID_ARG;
    }

    /*
     * Increment the hop count first, then check whether the new count
     * exceeds the configured maximum.  hop_count == max_hops is still
     * deliverable (the packet has reached its final hop); only
     * hop_count > max_hops is dropped.
     */
    packet->hop_count++;
    packet->sender_id = next_hop_id;

    if (packet->hop_count > packet->max_hops) {
        return EHASH_ERR_MAX_HOPS;
    }

    return EHASH_OK;
}

/* ------------------------------------------------------------------ */
/* Message-type helper                                                 */
/* ------------------------------------------------------------------ */

bool ehash_msg_type_is_valid(uint8_t msg_type)
{
    return msg_type >= EHASH_MSG_TEMPLATE && msg_type < EHASH_MSG_MAX;
}
