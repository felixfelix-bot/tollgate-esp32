# Balloon TollGate — Scope Decisions (2026-07-29)

## Directive from Felix (operator)

Strip display + all balloon-irrelevant components. Extract ONLY business logic
relevant to balloon use case. Original tollgate repo retains everything.

## What Stays (11 source files)

| File | Purpose | Why balloon-relevant |
|------|---------|---------------------|
| tollgate_main.c | Boot + init sequence | Core |
| config.c | config.json, nsec loading | Core |
| captive_portal.c | WiFi AP + Cashu payment portal HTML | Core — this IS the product |
| dns_server.c | DNS hijack for captive portal redirect | Core — captive portal needs it |
| tollgate_api.c | HTTP endpoints (payment, wallet, mints, usage) | Core — payment API |
| identity.c | nsec → MAC/SSID/IP derivation | Core — board identity |
| nostr_event.c | Nostr event signing | Core — relay identity |
| geohash.c | Location encoding | Balloon position tracking |
| mint_health.c | Mint status checking | Dependency of tollgate_api (/mints endpoint) |
| local_relay.c | Local Nostr relay (wisp-esp32) | Operator decision: keep if space allows |

## What's Dropped (23 source files, preserved in repo)

**Display/UI**: display.c, font.c, touch.c, keyboard.c
**Mining**: asic_miner.c, sw_miner.c, stratum_client.c, stratum_proxy.c, remote_miner.c, faucet_client.c
**Marketplace**: market.c, beacon_price.c
**CVM/MCP**: cvm_server.c, mcp_handler.c
**Client mode**: tollgate_client.c (balloon is standalone AP)
**Service discovery**: wifistr.c (balloon IS the service)
**Heavy sync**: negentropy_adapter.c, relay_selector.c, sync_manager.c
**Lightning**: lightning_payout.c, lnurl_pay.c
**Misc**: wifi_setup.c, nip04.c, tls_worker.c

## Cashu Payment Model

**CURRENT (Phase 1): Online mode only.**
- Balloon is online when receiving payment.
- nucula wallet swaps incoming Cashu tokens against real mint.
- No blind acceptance — tokens must be swappable.
- No own mint — just the wallet.

## ROADMAP (not implemented now)

### R1: Offline Mode (Free Relay Access)
When balloon has no internet → can't swap notes → can't sell internet access.
Give users free access to local relay only. No payment needed.

### R2: Pre-loaded Wallet with npub-locked Notes
- Pre-load ESP32 wallet with e-cash notes locked to balloon's npub (NIP-60 style).
- Clients obtain notes elsewhere (minted specifically for this balloon).
- ESP32 validates notes are locked to its npub — no internet needed.
- Enables offline payment verification.

### R3: Own Local Mint (if space allows on C3)
- If flash/PSRAM sufficient, run lightweight Cashu mint on ESP32.
- Clients mint tokens directly from ESP32.
- Fully offline payment loop.
- Status: investigate feasibility after C3 build succeeds.

## Preserved State

- Tag `v-balloon-pre-strip` at commit 5b1518f — full tollgate before balloon strip.
- All dropped files remain in the repo, just not compiled into balloon build.
- Original tollgate (separate from balloon worktree) is untouched.
