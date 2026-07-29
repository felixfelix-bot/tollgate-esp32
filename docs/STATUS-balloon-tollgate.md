# Status — balloon-tollgate

## Track
balloon-tollgate

## Current Phase
Execution (C3 port — scope stripped to balloon-relevant components)

## Working On Right Now
Branch: balloon-tollgate-c3-port
Last commit: (pending — scope decisions + CMakeLists fix)
Tag: v-balloon-pre-strip @ 5b1518f (preserves full tollgate before balloon strip)

Scope finalized with Felix (2026-07-29):
- 11 source files kept (captive portal, Cashu wallet, identity, relay, mint_health)
- 23 source files dropped from balloon build (display, mining, marketplace, CVM, client mode)
- All dropped files preserved in repo — original tollgate untouched
- Display permanently removed from balloon scope

Cashu model: Online-only for now. nucula wallet swaps tokens against real mint.
No blind acceptance. Roadmap: offline mode (R1), npub-locked notes (R2), local mint (R3).

## Blockers
1. sdkconfig not regenerated for C3 — still has S3 settings (CONFIG_IDF_TARGET="esp32s3", CONFIG_SPIRAM=y). Clean `idf.py set-target esp32c3` + reconfigure needed before any C3 build can succeed.
2. No C3 hardware build or flash verified — binary never produced for C3 target.
3. Flash queue approval required — can't flash without orchestrator OK.

## Resolved (2026-07-29)
- ~~Local relay partition decision~~ — KEEP local_relay. Felix confirmed.
- ~~Display scope~~ — DROP entirely. Not flying a display.
- ~~mint_health missing from CMakeLists~~ — FIXED. Re-added (dependency of tollgate_api).
- ~~What to port~~ — RESOLVED. 11 files kept, 23 dropped. See docs/BALLOON-SCOPE-DECISIONS.md.

## Next 3 Deliverables
1. Regenerate sdkconfig for esp32c3 target (`idf.py set-target esp32c3`) and attempt clean C3 build
2. Flash stripped C3 binary to ESP32-C3 board, verify WiFi AP + captive portal boots (via flash queue + hard lock)
3. Test Cashu wallet swap operations on C3 hardware against test mint

## Discovery Sync Applied (2026-07-24)
Hard board locking v3, flash queue protocol, board-serial.py wrapper, FLRC byte alignment (informational only).
