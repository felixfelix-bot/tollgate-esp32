/**
 * @file ehash_relay.h
 * @brief E-Hash L7 LoRa relay protocol handler.
 *
 * Implements the LoRa message protocol layer described in
 * ADR-XXX (Balloon as Stratum Relay — E-Hash Mining Bridge).
 *
 * The balloon acts as a thin relay between a mining pool (internet side)
 * and ground-based ASIC miners (LoRa side). Three message types are
 * defined:
 *
 *   - EHASH_MSG_TEMPLATE : downlink broadcast, binary block template (~80-120 B)
 *   - EHASH_MSG_NONCE    : uplink unicast, share submission (exactly 16 B)
 *   - EHASH_MSG_PAYMENT  : uplink unicast, Cashu Ecash token (~50-80 B)
 *
 * Wire format (all fields big-endian):
 *
 *   +----------+-----------+----------+------------+--------------+---------+--------+
 *   | MSG_TYPE | HOP_COUNT | MAX_HOPS | SENDER_ID  | PAYLOAD_LEN  | PAYLOAD | CRC16  |
 *   |   1 B    |    1 B    |   1 B    |    4 B     |     2 B      |   N B   |  2 B   |
 *   +----------+-----------+----------+------------+--------------+---------+--------+
 *
 *   Header = 9 B, Trailer (CRC16) = 2 B  →  minimum packet size (N=0) = 11 B.
 *
 * CRC16 uses the CCITT polynomial (0x1021) with initial value 0xFFFF,
 * computed over all bytes excluding the trailing CRC field.
 *
 * Pure C, no ESP-IDF dependencies — compiles with host gcc for unit tests.
 */
#ifndef EHASH_RELAY_H
#define EHASH_RELAY_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Constants                                                          */
/* ------------------------------------------------------------------ */

/** Maximum payload length (fits in the 2-byte PAYLOAD_LEN field). */
#define EHASH_MAX_PAYLOAD       65535

/** Wire header size in bytes (MSG_TYPE + HOP_COUNT + MAX_HOPS + SENDER_ID + PAYLOAD_LEN). */
#define EHASH_HEADER_SIZE       9

/** CRC16 trailer size in bytes. */
#define EHASH_CRC_SIZE          2

/** Minimum packet size on the wire (header + empty payload + CRC). */
#define EHASH_MIN_PACKET_SIZE   (EHASH_HEADER_SIZE + EHASH_CRC_SIZE)

/** Nonce message payload is fixed at 16 bytes: job_id(4) + extranonce2(4) + ntime(4) + nonce(4). */
#define EHASH_NONCE_PAYLOAD_LEN 16

/** Default maximum hop count for the relay chain. */
#define EHASH_DEFAULT_MAX_HOPS  4

/* ------------------------------------------------------------------ */
/* Message types                                                      */
/* ------------------------------------------------------------------ */

/** Downlink broadcast: binary block template fields (~80-120 B). */
#define EHASH_MSG_TEMPLATE      0x01

/** Uplink unicast: nonce submission (16 B fixed payload). */
#define EHASH_MSG_NONCE         0x02

/** Uplink unicast: Cashu Ecash token for bandwidth payment (~50-80 B). */
#define EHASH_MSG_PAYMENT       0x03

/** Sentinel — first invalid message type. */
#define EHASH_MSG_MAX           0x04

/* ------------------------------------------------------------------ */
/* Return codes                                                       */
/* ------------------------------------------------------------------ */

#define EHASH_OK                    0
#define EHASH_ERR_INVALID_ARG       (-1)   /**< NULL pointer or out-of-range argument.       */
#define EHASH_ERR_BUFFER_TOO_SMALL  (-2)   /**< Output buffer too small for the packet.       */
#define EHASH_ERR_INVALID_PACKET    (-3)   /**< Malformed/truncated buffer on unpack.         */
#define EHASH_ERR_CRC_MISMATCH      (-4)   /**< CRC16 check failed.                           */
#define EHASH_ERR_MAX_HOPS          (-5)   /**< Hop count exceeded the configured maximum.    */
#define EHASH_ERR_PAYLOAD_TOO_LARGE (-6)   /**< Payload length exceeds EHASH_MAX_PAYLOAD.     */
#define EHASH_ERR_BAD_MSG_TYPE      (-7)   /**< Unknown message type byte.                    */

/* ------------------------------------------------------------------ */
/* Data structures                                                    */
/* ------------------------------------------------------------------ */

/**
 * @brief Parsed E-Hash relay packet.
 *
 * @note payload points into the caller's buffer on unpack(); it is NOT
 *       an owned copy. The caller must keep the wire buffer alive for
 *       the lifetime of this struct, or copy payload out before the
 *       buffer is released.
 */
typedef struct {
    uint8_t  msg_type;      /**< One of EHASH_MSG_TEMPLATE / NONCE / PAYMENT.   */
    uint8_t  hop_count;     /**< Number of hops the packet has traversed.       */
    uint8_t  max_hops;      /**< Maximum hops before the packet is dropped.     */
    uint32_t sender_id;     /**< 32-bit identifier of the most recent forwarder.*/
    uint16_t payload_len;   /**< Number of valid bytes in @p payload.           */
    const uint8_t *payload; /**< Pointer to payload bytes (not owned).          */
} ehash_packet_t;

/* ------------------------------------------------------------------ */
/* API functions                                                      */
/* ------------------------------------------------------------------ */

/**
 * @brief Compute CRC16-CCITT (poly 0x1021, init 0xFFFF) over @p buf.
 *
 * @param buf  Bytes to checksum (must not be NULL when len > 0).
 * @param len  Number of bytes to checksum.
 * @return     16-bit CRC value.
 */
uint16_t ehash_packet_calc_crc(const uint8_t *buf, size_t len);

/**
 * @brief Pack a relay packet into a wire-format buffer.
 *
 * Layout: [MSG_TYPE][HOP_COUNT][MAX_HOPS][SENDER_ID][PAYLOAD_LEN][PAYLOAD][CRC16].
 * All multi-byte fields are big-endian. The CRC16 covers every byte of the
 * packet except the trailing CRC field itself.
 *
 * @param msg_type     One of EHASH_MSG_* constants.
 * @param hop_count    Current hop count (typically 0 at origin).
 * @param max_hops     Maximum hops before the packet is dropped.
 * @param sender_id    32-bit identifier of the sending node.
 * @param payload      Payload bytes (may be NULL if payload_len == 0).
 * @param payload_len  Number of payload bytes (0 .. EHASH_MAX_PAYLOAD).
 * @param out_buf      Destination buffer (caller-allocated).
 * @param buf_size     Capacity of @p out_buf in bytes.
 *
 * @return >0  Total packed size in bytes on success.
 * @return <0  Negative EHASH_ERR_* code on failure.
 */
int ehash_packet_pack(uint8_t msg_type, uint8_t hop_count, uint8_t max_hops,
                      uint32_t sender_id,
                      const uint8_t *payload, uint16_t payload_len,
                      uint8_t *out_buf, size_t buf_size);

/**
 * @brief Unpack a wire-format buffer into an @ref ehash_packet_t.
 *
 * The @p out_packet->payload pointer aliases into @p buf — no copy is made.
 * The caller must keep @p buf alive while using the parsed struct.
 *
 * CRC is validated during unpack; a mismatch returns EHASH_ERR_CRC_MISMATCH.
 *
 * @param buf        Wire-format bytes (must not be NULL).
 * @param buf_len    Number of bytes available in @p buf.
 * @param out_packet Output parsed packet (must not be NULL).
 *
 * @return EHASH_OK on success.
 * @return <0  Negative EHASH_ERR_* code on failure.
 */
int ehash_packet_unpack(const uint8_t *buf, size_t buf_len,
                        ehash_packet_t *out_packet);

/**
 * @brief Validate the CRC16 trailer of a wire-format packet.
 *
 * @param buf     Wire-format bytes including the trailing CRC.
 * @param buf_len Total bytes in @p buf (must be >= EHASH_MIN_PACKET_SIZE).
 *
 * @return true  CRC matches.
 * @return false CRC mismatch or malformed buffer.
 */
bool ehash_packet_validate_crc(const uint8_t *buf, size_t buf_len);

/**
 * @brief Advance a packet one hop towards its destination.
 *
 * Increments @p packet->hop_count, updates @p packet->sender_id to
 * @p next_hop_id, and checks that the new hop count does not exceed
 * @p packet->max_hops.
 *
 * @param packet      Packet to forward (modified in place).
 * @param next_hop_id 32-bit identifier of the forwarding node.
 *
 * @return EHASH_OK            if the packet should be forwarded.
 * @return EHASH_ERR_MAX_HOPS  if hop_count would exceed max_hops (packet dropped).
 * @return EHASH_ERR_INVALID_ARG if @p packet is NULL.
 */
int ehash_relay_forward(ehash_packet_t *packet, uint32_t next_hop_id);

/**
 * @brief Helper: check whether a message type byte is valid.
 *
 * @param msg_type Message type byte.
 * @return true if @p msg_type is one of the known EHASH_MSG_* constants.
 */
bool ehash_msg_type_is_valid(uint8_t msg_type);

#ifdef __cplusplus
}
#endif

#endif /* EHASH_RELAY_H */
