# Status — balloon-tollgate

## Track
balloon-tollgate

## Current Phase
Execution (C3 port in progress)

## Working On Right Now
Branch: balloon-tollgate-c3-port (clean, no uncommitted changes)
Last commit: 58e65a3 (docs: anti-coordination guardrails in AGENTS.md)
Integration assessment complete (2026-07-18). Stripped C3 build configured, 86/86 host unit tests pass.
Not currently building — idle, awaiting orchestrator direction on next steps.

## Blockers
1. sdkconfig not regenerated for C3 — still has S3 settings (CONFIG_IDF_TARGET="esp32s3", CONFIG_SPIRAM=y). Clean `idf.py set-target esp32c3` + reconfigure needed before any C3 build can succeed.
2. No C3 hardware build or flash verified — binary never produced for C3 target.
3. Local relay partition decision — C3 partition table has no relay_store partition. Must decide: is local Nostr relay needed for balloon, or drop it?
4. STATUS-REQUEST-PROMPT.md template referenced in AGENTS.md does not exist in worktree.

## Next 3 Deliverables
1. Regenerate sdkconfig for esp32c3 target (`idf.py set-target esp32c3`) and attempt clean C3 build
2. Flash stripped C3 binary to ESP32-C3 board, verify WiFi AP + captive portal boots
3. Test Cashu wallet operations (receive/send) on C3 hardware against test mint