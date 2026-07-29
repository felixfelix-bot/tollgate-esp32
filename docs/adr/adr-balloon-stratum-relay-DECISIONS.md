# ADR: Balloon Stratum Relay (E-Hash) — LOCKED DECISIONS LOG

## RESOLVED DECISIONS (locked)

### D1. Balloon role = relay only, no hashing
Balloon never computes SHA256. Pure stratum relay + Ecash payment gateway.
(Felix, 2026-07-29)

### D2. Ground hardware = balloon base station + Bitaxe
Regular balloon LoRa base station on ground, connected locally to a Bitaxe
(or any stratum-compatible ASIC miner). Bitaxe runs stock firmware.
(Felix, 2026-07-29)

### D3. Full template broadcast per block (~10 min)
Full block template sent on every new block. Customer pays Ecash for
bandwidth cost. No incremental/diff optimization in initial design.
(Felix, 2026-07-29)

### D4. E-Hash payment model
Customer pays Ecash for bandwidth (template delivery + nonce uplink relay).
Reuses existing TollGate Cashu wallet + session system.
(Felix, 2026-07-29)

## STILL OPEN (awaiting decision)

### O1. Stratum V1 vs V2 upstream?
Recommendation: V1 (simpler, Bitaxe-compatible).

### O2. Pool account / revenue distribution?
Multi-worker pass-through vs single operator account?

### O3. Share difficulty filtering on ground?
Recommendation: local higher difficulty filter to conserve LoRa bandwidth.

### O4. Template encryption / access control?
Recommendation: per-session encryption key unlocked by Cashu payment.

### O5. Balloon internet loss behavior?
Recommendation: TTL-based template expiry + pause on ground.
