# ADR-XXX: Balloon as Stratum Relay — E-Hash Mining Bridge

## Status

Proposed

## Date

2026-07-29

## Context

The balloon project has a LoRa radio link (Semtech LR1121 or similar) between the
balloon (in the sky, with internet via STA WiFi) and ground-based stations.
Felix has identified a use case: the balloon can serve as a **stratum relay
bridge**, providing internet uplink for ground-based ASIC miners (Bitaxe, NerdMiner)
in areas where the miners have no direct internet connection.

The concept is called **E-Hash**: customers pay Ecash (via the existing TollGate
Cashu payment system) for the bandwidth required to relay block templates
(downlink) and nonce submissions (uplink) over LoRa. The balloon is NOT a hash
engine — it never computes SHA256. It is a thin relay + payment gateway.

### Existing Code Reuse

The TollGate firmware already provides:
- WiFi AP+STA, Cashu wallet, captive portal, HTTP API (`main/tollgate_api.c`)
- Nostr identity, session management, firewall
- The Phase 1 mining extraction (`mining-standalone/`) provides `stratum_client`
  and `stratum_proxy` modules that build clean on ESP32-S3 (846KB, 0 warnings)

### System Topology

```
  Mining Pool (Internet)
         ↕  Stratum (TCP/WSS)
    ┌────────────────┐
    │   BALLOON      │  (ESP32-S3, in sky)
    │  - stratum_client (upstream to pool)
    │  - Cashu payment gateway (metered access)
    │  - LoRa template broadcaster (downlink)
    │  - LoRa nonce collector (uplink)
    └───────┬────────┘
            ↕  LoRa radio
    ┌───────┴────────┐
    │ GROUND BASE    │  (balloon LoRa terminal, RP2040 or ESP32)
    │ STATION        │
    │  - LoRa receiver (template → local stratum job)
    │  - Local stratum server (serves Bitaxe)
    │  - Share filter + uplink queue
    └───────┬────────┘
            ↕  USB / serial / local WiFi
    ┌───────┴────────┐
    │ BITAXE / ASIC  │  (hash engine, stock firmware)
    │ MINER          │
    │  - connects to local stratum server
    │  - mines against relayed template
    │  - submits shares locally
    └────────────────┘
```

### Bandwidth Budget (LoRa)

| Payload | Direction | Size (binary) | Frequency |
|---------|-----------|---------------|-----------|
| Block template (Stratum notify) | Downlink (broadcast) | ~80-120 bytes | Every ~10 min (new block) |
| Nonce submission (valid share) | Uplink | ~20 bytes | Per valid share |
| Payment token (Ecash) | Uplink or side-channel | ~50-80 bytes | Per session/block |

At LoRa SF7 (~100 bps effective): template broadcast takes ~8-12 seconds.
At SF12 (~30 bps): ~30-40 seconds. Both fit within a 10-minute block window.

Template is broadcast one-to-many — all ground stations in range receive it
simultaneously. Nonce submissions are one-to-one uplink.

## Decision

The balloon implements a **Stratum Relay Service** with three components:

### 1. Stratum Client (Balloon → Pool)

Reuse the extracted `stratum_client` module from Phase 1. Balloon connects to
a configured mining pool via STA WiFi (TCP/WSS). It acts as a standard stratum
worker: receives `mining.notify` (block templates), submits `mining.submit`
(valid shares from ground miners).

The balloon registers ONE pool identity. All ground miners mine under the
balloon's pool account. Revenue flows to the balloon's pool wallet.

### 2. LoRa Template/Nonce Relay

Define a new LoRa message protocol layer (separate from existing balloon
telemetry packets):

- **MSG_TEMPLATE** (downlink, broadcast): Binary-encoded block template fields.
  Sent on every `mining.notify` from pool (new block or update).
  All paying ground stations receive it.

- **MSG_NONCE** (uplink, unicast): Binary nonce submission from ground station.
  Contains: job_id(4B) + extranonce2(4B) + ntime(4B) + nonce(4B) = 16 bytes.

- **MSG_PAYMENT** (uplink, unicast): Cashu token for bandwidth payment.
  Ground station submits Ecash token to unlock template access.

### 3. Cashu Payment Gateway (E-Hash)

Reuse the existing TollGate Cashu wallet + payment API. Payment model:

- **Per-block access**: Customer pays Ecash for N blocks of template delivery.
  Each block = ~10 min window. Balloon tracks per-station credit balance.
  Zero balance → template broadcast encrypted/keyed, station cannot decode.

- **Share relay**: Included in per-block fee. Ground station submits shares
  up to a rate cap (prevents flooding LoRa uplink with junk shares).

The balloon's existing session manager (`main/session.c`) already does
time-based MAC tracking and access control. Extending it to cover LoRa
identity (per-station keys) is a natural fit.

### Ground Base Station Role

The ground base station is the critical translator:

1. Receives LoRa template → decodes → constructs local Stratum `mining.notify`
2. Runs a **local Stratum V1 server** (tiny, ~200 lines) on localhost
3. Bitaxe connects to local server as if it were a normal pool
4. Bitaxe submits shares → base station filters by difficulty → queues best
   shares for LoRa uplink to balloon
5. Base station manages Ecash payment: requests/maintains credit with balloon

Bitaxe runs STOCK firmware — no modification needed. It sees a local stratum
pool. The base station is transparent.

## Invariants

1. **Balloon never hashes.** No SHA256 computation on balloon. Pure relay.
2. **Bitaxe runs unmodified firmware.** All intelligence in base station + balloon.
3. **Template is broadcast, not unicast.** One LoRa transmission serves all stations.
4. **No template without payment.** Cashu credit required to decode templates.
5. **Only pool-difficulty shares go up.** Base station filters shares before uplink
   to conserve LoRa bandwidth. Sub-target shares stay local.

## Consequences

### Positive

- **Massive code reuse**: Cashu wallet, WiFi, identity, session management all
  exist in TollGate firmware. Stratum client extracted in Phase 1. Only new
  code is LoRa message encoding + ground base station stratum server.
- **Bitaxe compatibility**: Stock firmware, zero modification. Any stratum-compatible
  ASIC works (Bitaxe, NerdMiner, braiins OS miners).
- **Revenue model**: Balloon operator earns Ecash for bandwidth. Miners get
  internet-free pool access. Clean E-Hash economy.
- **Scalable**: One template broadcast serves N ground stations. Uplink is the
  only per-station bandwidth cost.

### Costs

- **Latency**: LoRa adds 8-40 seconds of template delivery latency vs ~100ms
  direct internet. Miners work on stale templates for that window. Acceptable
  for small miners (Bitaxe ~1-3 TH/s) — negligible revenue impact.
- **Uplink contention**: Multiple ground stations submitting shares compete for
  LoRa uplink airtime. Need backoff/Collision avoidance or TDM scheduling.
- **Pool account centralization**: All miners mine under balloon's pool account.
  Revenue trust model: balloon operator must distribute earnings honestly.
  Ecash tokens provide accounting transparency.
- **Ground base station complexity**: Needs a stratum server implementation.
  Target: ~200-300 lines C on RP2040 or ~150 lines Python on a Pi.

## Open Questions (for design session)

### O1. Stratum V1 vs V2 upstream?

V1 is simpler (JSON text protocol, widely supported, Bitaxe uses V1).
V2 has better template distribution (binary, designed for relay scenarios)
but adds protocol complexity. Recommendation: **V1 for initial implementation.**
V2 can be layered later since the LoRa payload format is protocol-agnostic.

### O2. Pool account / revenue distribution?

Whose pool account? Options:
- A) Balloon operator's account — all shares credited to operator, who
     distributes Ecash to miners manually/automatically.
- B) Per-miner pool accounts — balloon passes through stratum worker IDs.
     Requires pool to support multiple workers under one connection (most do).

Recommend B — standard stratum multi-worker. Balloon stratum_client uses
`<pool_user>.<ground_station_id>` worker naming.

### O3. Share difficulty filtering?

Pool sets a share difficulty. At LoRa bandwidth, we cannot relay every share.
Options:
- A) Ground base station runs its own higher difficulty filter. Only shares
     meeting a local threshold (e.g. 10x pool difficulty) go up. Fewer shares
     relayed but each is more valuable.
- B) Base station relays all pool-difficulty shares, accepts uplink contention.

Recommend A — conserve LoRa bandwidth. Trade-off: slightly reduced share
submission rate, but LoRa can't handle high share volume anyway.

### O4. Template encryption / access control?

Felix's model: customer pays Ecash for bandwidth. How do we enforce this?
- A) Template broadcast in plaintext, but base station needs Ecash token to
     authenticate uplink (nonce submission). Freeloader gets templates but
     can't submit shares. Weak — they could mine for their own pool.
- B) Template encrypted with per-session key. Base station gets decryption
     key only after Ecash payment. Strong — no template without payment.

Recommend B — use the existing session/key derivation (HMAC-SHA512 from nsec)
to generate per-station template keys. Cashu payment unlocks key delivery.

### O5. Balloon loses internet?

When balloon STA WiFi drops (no internet):
- Balloon cannot fetch new templates. Ground miners keep mining last known
  template. Shares will eventually be rejected by pool (stale).
- Balloon should cache last template + broadcast "stale" flag. Ground base
  stations can choose to continue (waste of hashpower) or pause.

Recommend: balloon broadcasts template with a TTL timestamp. Ground station
stops mining after TTL expires without new template. Resumes when new
template arrives.

## Rollout / Implementation Phases

TBD — implementation not yet started. Awaiting decisions on open questions O1-O5.

Proposed phase structure (subject to approval):

- Phase A: LoRa message protocol spec (template encoding, nonce format, payment)
- Phase B: Ground base station prototype (RP2040: LoRa RX → local stratum server)
- Phase C: Balloon stratum relay module (stratum_client → LoRa TX + Cashu gate)
- Phase D: Integration test (balloon + base station + Bitaxe, end-to-end)

## Notes

- Phase 1 mining extraction (commit 9e40143) directly supports this architecture.
  The `stratum_client` module is the balloon's upstream connection.
  The `sw_miner`/`asic_miner` modules are reference for ground-side understanding
  but are NOT deployed — Bitaxe runs its own firmware.
- The existing TollGate session manager (`main/session.c`) does time-based access
  control per MAC. Extending to LoRa station identity is a small addition.
- LoRa radio hardware details (modulation, SF, bandwidth) are tracked in the
  balloon-fresh repo's firmware track, not here. This ADR is protocol-level.
- Felix's vision: E-Hash = Ecash + hashpower. The balloon provides infrastructure
  (internet relay) as a metered Ecash service. Ground miners provide hashpower
  and pay for uplink. This is the TollGate model applied to mining relay instead
  of WiFi hotspot access.
