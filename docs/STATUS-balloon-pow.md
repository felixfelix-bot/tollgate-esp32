# STATUS — balloon-pow (Track 4: PoW/Mining)

Last updated: 2026-07-26

## Current Phase: Phase 1 COMPLETE — Phase 2 BLOCKED (needs S3 hardware)

### Progress
- [x] Worktree created: ~/worktrees/balloon-pow/ (branch: balloon-pow-extraction)
- [x] Mining source files identified (990 lines total across 5 modules)
- [x] Source code analysis + extraction
- [x] Standalone project built: mining-standalone/ (commit 648cb18)
- [x] ESP32-S3 build: PASS (846KB binary, 0 compiler warnings)
- [x] Build report: docs/MINING-BUILD-REPORT.md
- [ ] Hashrate estimation from code analysis (deferred to Phase 2 on hardware)
- [ ] Phase 2: Flash to S3 board + benchmark (BLOCKED: D-001)
- [ ] Phase 4: ESP32-C3 feasibility analysis

### Source Inventory
| Module | Lines | Status |
|--------|-------|--------|
| sw_miner.c/h | 149 | Core — software SHA256 mining |
| stratum_client.c/h | 349 | Core — Stratum v1 pool protocol |
| asic_miner.c/h | 77 | Optional — BM1366 ASIC interface |
| remote_miner.c/h | 344 | Optional — remote HTTP mining |
| stratum_proxy.c/h | 71 | Optional — Stratum v2 proxy |

### Cross-Track Discovery Relevance (2026-07-26)
Received 2 findings from balloon-hermes:

1. **FLRC byte alignment + CRC-16 + FIFO clear + sync search** (commit 9b740aa)
   - Tags: RADIO, FIRMWARE
   - Relevance to PoW: Radio comms now reliable. Mining nonce-scanning can be scheduled in radio idle gaps without corrupting RX/TX cycles. App-layer CRC-16 adds ~2 bytes overhead per packet — negligible for power budget.

2. **GPS payload verified on LoRa phases** (commit be354b0)
   - Tags: RADIO, FIRMWARE, TEST
   - Relevance to PoW: GPS position reporting works alongside radio. Confirms multi-subsystem coexistence. For mining on single-core ESP32-C3: GPS acquisition (cold start 15-30min) + mining + radio must be time-sliced. GPS is power-hungry; mining is CPU-hungry. Solar budget must account for peak load when all three are active.

### Key Questions for Phase 4 (C3 Feasibility)
- Can SHA256 hardware acceleration on C3 match S3 throughput/3?
- What's the power delta between mining-active vs mining-idle?
- How does radio SPI interrupt latency affect mining nonce scan rate?
- Is merge-mining (Namecoin/etc) viable alongside Bitcoin mining?
- Energy-aware hashing (e-hash): mine only when solar surplus detected?

### Blockers
- D-001: ESP32-S3 boards needed for Phase 2 (benchmark on real hardware)
- Phase 1 (analysis + extraction + build) can proceed without hardware

### Cross-Track Discovery Relevance (2026-07-30)
Received 55 findings from balloon-hermes (batch sync) + 1 earlier (nostr_store).
Key relevance to PoW track:

1. **RadioLib → lr2021_transport (ADR-020)** — Raw 20MHz SPI replaces library overhead.
   - PoW impact: Less CPU/SPI bus time per radio cycle → more nonce-scan windows.
   - Mining can be scheduled in SPI idle gaps more efficiently.

2. **nostr_store flash-backed rewrite** — Nostr events persist to flash on ESP32-C3.
   - PoW impact: Flash I/O contention. If mining + nostr_store coexist on C3 (Phase 4 target),
     flash writes during share submission / block header updates will stall nonce scanning.
   - Mitigation: mine in bursts between flash write windows, or use RAM buffer for shares.

3. **ESP32-C3 store-and-forward extraction plan** — C3 confirmed as tracker node target.
   - PoW impact: C3 will run radio + GPS + nostr_store + potentially mining.
   - CPU budget: C3 single-core RISC-V @ 160MHz. SHA256 in software = ~10-50 KH/s estimated.
   - Radio SPI + GPS acquisition + nostr flash writes all compete for CPU.
   - Phase 4 feasibility: mining on C3 likely needs cooperative scheduling, not preemptive.

4. **FIPS Noise IK handshake** — Noise protocol (SHA256-family crypto) tested on ESP32-S3.
   - PoW impact: Real-world ESP32 crypto performance data exists. Can inform hashrate estimates.
   - Noise handshake = key derivation + AEAD, not pure SHA256 hashing, but shows CPU cost baseline.

5. **SPI crash fixes (5 patches)** — Radio SPI stability improved significantly.
   - PoW impact: Stable radio = predictable idle windows for mining scheduling.
   - Less risk of mining loop corrupting radio state via SPI bus contention.

### Workstream 4 COMPLETE (2026-07-30)
- L7 ehash_relay handler: packet pack/unpack, CRC16-CCITT, hop tracking, relay forwarding
- 154 tests PASS across 3 suites:
  - test_ehash_relay: 78 tests (round-trip, CRC, hop counting, boundaries)
  - test_stratum_bridge: 65 tests (job creation, difficulty, nonce→share, JSON submit, relay bridge)
  - test_relay_simulation: 11 tests (3-hop chain, 1000-iteration stress, 0 packet loss)
- Branch: balloon-pow-e-hash pushed to github
- Commits: 5263a99 (ehash component + tests), 0ad6ad8 (gitignore)
