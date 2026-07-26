/*
 * tls_worker.h — Minimal stub for standalone mining build.
 *
 * In the tollgate firmware, tls_worker_submit() enqueues a token for
 * asynchronous TLS submission to a Nostr relay.  In standalone mining
 * we have no Nostr relay, so it's a no-op.
 */
#ifndef TLS_WORKER_H
#define TLS_WORKER_H

void tls_worker_submit(const char *token);

#endif /* TLS_WORKER_H */
