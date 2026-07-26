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
