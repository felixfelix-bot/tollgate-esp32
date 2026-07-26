/*
 * identity.h — Minimal stub for standalone mining build.
 *
 * In the tollgate firmware, identity_init() derives MAC/SSID/IP from the
 * Nostr nsec and populates locking_pubkey_hex.  The stratum_client.c only
 * checks id->initialized and id->locking_pubkey_hex to optionally append
 * the pubkey to the stratum password.  In standalone we leave it empty.
 */
#ifndef IDENTITY_H
#define IDENTITY_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    char locking_pubkey_hex[67];
    bool initialized;
} tollgate_identity_t;

const tollgate_identity_t *identity_get(void);

#endif /* IDENTITY_H */
