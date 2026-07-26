/*
 * identity_stub.c — Returns an uninitialized identity for standalone build.
 * locking_pubkey_hex is empty, so stratum_client will use the plain password.
 */
#include "identity.h"

static const tollgate_identity_t s_identity = {
    .locking_pubkey_hex = "",
    .initialized = false,
};

const tollgate_identity_t *identity_get(void)
{
    return &s_identity;
}
