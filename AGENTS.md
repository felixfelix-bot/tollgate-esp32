# HIERARCHY ROLE: SUB-PROJECT MANAGER

You are the isolated manager of balloon-tollgate only. You report to the balloon-hermes orchestrator group.

## CRITICAL BOUNDARIES (ANTI-COLLAPSE GUARDRAILS)

- You are a SUB-MANAGER, not a coordinator. You do NOT coordinate other balloon tracks.
- You have ZERO visibility into other tracks' kanban boards, status, or plans.
- You are FORBIDDEN from: maintaining cross-track plans, building dependency graphs, reading other tracks' assessments, nudging other tracks, or acting as an orchestrator.
- Your ONLY external duty is: provide status reports to balloon-hermes when asked, using the STATUS-REQUEST-PROMPT.md template.
- Do NOT read ~/repos/balloon-fresh/docs/coordination/ files (INDEX.md, DECISIONS-AND-BLOCKERS.md, COORDINATOR-TRACKING.md, TRACKS-REGISTRY.yaml) — those are orchestrator-only files.
- Do NOT read ~/.hermes/profiles/manager/state/session-notes.md — that contains coordinator context.

## YOUR SCOPE
- Your worktree: this directory only
- Your kanban: your board only (if configured)
- Your assessment: docs/INTEGRATION-ASSESSMENT.md in this worktree
- Your status file: docs/STATUS-balloon-tollgate.md in this worktree

## DELEGATION EXPECTATIONS (POSITIVE COLLABORATION)

You are part of a hierarchy. The orchestrator (balloon-hermes group) DELEGATES work to you. Your responsibilities:

1. **EXPECT DELEGATION.** When the orchestrator sends you a task, it is YOUR responsibility. Execute it, do not bounce it back. The orchestrator chose you because this is your domain expertise.
2. **RESPOND PROMPTLY.** When asked for status or a task update, respond in the SAME turn. Use the STATUS-REQUEST-PROMPT.md template if one was sent.
3. **PROACTIVELY REPORT cross-track findings.** If you discover something relevant to another track's domain (e.g., a hardware issue, a protocol mismatch, a shared resource conflict), tell the orchestrator: "ORCHESTRATOR: Forward this to [track-name]: [finding]". The orchestrator routes it — you do NOT contact other tracks directly.
4. **SHARE BLOCKERS EARLY.** If you are blocked on something another track owns (shared hardware, dependency, protocol), tell the orchestrator immediately. Do NOT silently wait or try to work around it yourself.
5. **YOUR STATUS IS VISIBILITY.** Commit and push regularly. The orchestrator monitors your worktree via session_search and git log. Uncommitted work is invisible work.

These complement your anti-collapse guardrails above: you collaborate THROUGH the orchestrator, never directly with other tracks.

## BOARD ACCESS — HARD MUTEX LOCK (v3)

All 3 ESP32-S3 boards are shared resources across balloon tracks. Access is
enforced by a **hard device lock** (chmod 000 on /dev/ttyACMx), not just an
advisory flock. When another track holds a board lock, raw tool access is
physically blocked:

- `cat /dev/ttyACMx` → Permission denied
- `esptool.py --port /dev/ttyACMx ...` → Permission denied
- `idf.py -p /dev/ttyACMx flash` → Permission denied

Tool: `~/repos/balloon-fresh/tools/balloon-board-lock.py` (v3)

### Flash Queue — Orchestrator Approval REQUIRED

**NO flashing without orchestrator (balloon-hermes) approval.** Before
flashing ANY board, add a row to `~/repos/balloon-fresh/docs/coordination/FLASH-QUEUE.md`
and wait for approval. This prevents firmware mismatch during coordinated tests.

### Flash Procedure

```bash
# 1. Get orchestrator approval (add row to FLASH-QUEUE.md)

# 2. Acquire hard lock (blocks other tracks at OS level):
BALLOON_TRACK=tollgate python3 ~/repos/balloon-fresh/tools/balloon-board-lock.py acquire board-a \
    --purpose "approved flash: C3 stripped build" --timeout 120

# 3. Flash with idf.py (lock holder's sentinel keeps fd open → access works):
idf.py -p /dev/ttyACM0 flash monitor

# 4. ALWAYS release when done (restores chmod 666):
BALLOON_TRACK=tollgate python3 ~/repos/balloon-fresh/tools/balloon-board-lock.py release board-a

# 5. Update FLASH-QUEUE.md status to DONE
```

### Verify Lock Before Board Work

```bash
# Exit 0 = you hold the lock; Exit 1 = another track holds it
BALLOON_TRACK=tollgate python3 ~/repos/balloon-fresh/tools/balloon-board-lock.py check board-a
```

### Serial Wrapper for Python Test Scripts

Any Python script that opens a serial port MUST use BoardSerial instead of
raw `serial.Serial()`:

```python
from board_serial import BoardSerial
# NOT: ser = serial.Serial('/dev/ttyACM0', 115200)
ser = BoardSerial('/dev/ttyACM0', 115200)
```

Tool: `~/repos/balloon-fresh/tools/board-serial.py`

Board mapping:
- board-a: ESP32-S3, MAC 94:a9:90:2e:37:7c, /dev/ttyACM0, TollGate-B96D80
- board-b: ESP32-S3, MAC fc:01:2c:c5:50:50, /dev/ttyACM1, TollGate-C0E9CA
- board-c: ESP32-S3, MAC 20:6e:f1:98:d7:08, /dev/ttyACM3, display board

Skipping the lock is a bug. Concurrent flashing corrupts boards. The hard
device lock (chmod 000) ensures that even if a sub-manager bypasses the lock
script, raw tools fail with "Permission denied".

### Discovery Sync Notes (2026-07-24)

Adopted independently from cross-track findings (no coordination):
- **Hard device locking (v3)**: from balloon-hermes commit 35b292c + balloon-speed-tests commit ef60a51
- **Flash queue protocol**: from balloon-hermes FLASH-QUEUE.md
- **Serial wrapper mandate**: from balloon-range-tests commit 171387d
- **FLRC byte alignment**: from balloon-range-tests commit 9b740aa — informational only, tollgate has no LR2021 radio

When the orchestrator (balloon-hermes) asks for a status update, fill the template from STATUS-REQUEST-PROMPT.md and reply with the filled template only. No commentary, no cross-track opinions.

---

# AGENTS.md — Instructions for AI Coding Agents

## Project Overview

TollGate ESP32 firmware: captive portal WiFi hotspot with Cashu e-cash payments, on-device wallet, Nostr identity derivation, wifistr service discovery, ContextVM (MCP over Nostr) server, and **local Nostr relay** with relay selection and sync. Runs on three ESP32-S3 boards.

## Technology Stack

- **Framework:** ESP-IDF v5.4.1 (C/C++)
- **Target:** ESP32-S3, 16MB flash, 8MB PSRAM (OCT mode)
- **Wallet:** nucula library (libsecp256k1) via git submodule
- **Identity:** Nostr nsec → HMAC-SHA512 → deterministic MAC/SSID/IP
- **Service discovery:** wifistr (Nostr kind 38787) via WebSocket
- **ContextVM:** MCP over Nostr (kind 25910), CEP-6 announcements, 10 MCP tools
- **Local relay:** wisp-esp32 (adapted), NIP-01 server on port 4869, LittleFS 4MB storage
- **Relay selection:** NIP-11 HTTP probing, latency + NIP-77 scoring, auto-failover
- **Sync:** REQ-diff with primary (30min) and fallback (6h) relays
- **Testing:** Host C unit tests (gcc), Node.js integration tests (live board), Playwright E2E

## Board Configuration

| Board | Port | Factory MAC | SSID | AP IP | Notes |
|-------|------|-------------|------|-------|-------|
| A | `/dev/ttyACM0` | `94:a9:90:2e:37:7c` | `TollGate-B96D80` | `10.185.47.1` | Primary test target |
| B | `/dev/ttyACM1` | `fc:01:2c:c5:50:50` | `TollGate-C0E9CA` | `10.192.45.1` | Secondary |
| C | `/dev/ttyACM3` | `20:6e:f1:98:d7:08` | (TBD) | (TBD) | Display board |

**IMPORTANT:** Board ports change on every USB replug. Always verify with `esptool.py --port <port> chip_id` before flashing.

Identity (SSID, IP, MAC) is derived from `nsec` in config.json. Each board gets a unique nsec.

## Boot Sequence

```
nvs_flash_init()
  → tollgate_config_init()          // loads config.json with nsec from SPIFFS
  → identity_init(nsec)             // derives npub, STA/AP MAC, SSID, IP via HMAC-SHA512
  → tollgate_config_derive_unique() // copies derived values into config struct
  → esp_netif_init() + esp_event_loop_create_default()
  → wifi_init_sta() + wifi_create_ap_netif()  // AP netif with derived IP
  → esp_wifi_init()
  → esp_wifi_set_mac(STA/AP)        // sets derived MACs
  → esp_wifi_set_mode(APSTA)
  → esp_wifi_set_country_code("DE") // EU regulatory domain (channels 1-13, 20dBm)
  → wifi_configure_ap()             // uses derived SSID
  → esp_wifi_start()
  → [on STA got IP] start_services():
      sntp_init, firewall_init, session_init, wallet_init, dns_server, captive_portal, api,
      local_relay_init+start, relay_selector_init+probe, sync_manager_start, wifistr_publish, cvm_server_start
```

## Key Files

### Source (main/)
- `tollgate_main.c` — entry point, WiFi AP+STA, event loop, service lifecycle
- `config.c/h` — SPIFFS config.json parsing, nsec/nostr/wifi/mint settings
- `identity.c/h` — HMAC-SHA512 derivation from nsec, npub/MAC/SSID/IP
- `nostr_event.c/h` — NIP-01 event serialization + BIP-340 Schnorr signing
- `geohash.c/h` — lat/lon to geohash encoding
- `wifistr.c/h` — kind 38787 event builder + local-first publish (local relay then public)
- `captive_portal.c/h` — HTTP :80 portal, captive detection, grant/reset
- `dns_server.c/h` — DNS hijack/forward per-client, DoT reject
- `firewall.c/h` — per-client NAT filter via LWIP_HOOK_IP4_CANFORWARD, MAC resolution
- `session.c/h` — time-based sessions, MAC tracking
- `cashu.c/h` — Cashu token decode, checkstate, allotment calc
- `tollgate_api.c/h` — HTTP :2121, payment endpoints, wallet endpoints
- `cvm_server.c/h` — ContextVM: persistent WS relay listener, kind 25910 subscription, MCP protocol handlers, CEP-6 announcements
- `mcp_handler.c/h` — 10 MCP tool handlers (get_config, set_config, get_balance, wallet_send, get_sessions, get_usage, set_payout, set_metric, set_price, wallet_melt)
- `local_relay.c/h` — Thin wrapper: inits wisp_relay storage/sub/rate-limiter on port 4869, publishes events to LittleFS + broadcasts to WS subscribers
- `relay_selector.c/h` — NIP-11 HTTP probing of seed relays, latency + NIP-77 scoring, auto-failover after 3 disconnects, 6h re-probe cycle
- `sync_manager.c/h` — REQ-diff sync: primary every 30min, fallback every 6h, reconciles local events vs remote, dedicated FreeRTOS task
- `display.c/h` — QSPI TFT display (JC3248W535/AXS15231B): boot/ready/payment/error states, Wi-Fi and portal URL QR cycling every 5s, `escape_wifi_field()` for special chars
- `font.c/h` — Bitmap font rendering for display text output

### Components
- `nucula_lib/` — C++ bridge to nucula::Wallet (C API in nucula_wallet.h)
- `secp256k1/` — symlink to nucula_src/components/secp256k1/
- `wisp_relay/` — Local Nostr relay (NIP-01): ws_server, storage_engine (LittleFS), sub_manager, broadcaster, router, handlers, relay_validator (Schnorr+SHA256), rate_limiter, nip11, deletion, flash_monitor
- `esp_littlefs/` — LittleFS VFS integration for relay storage partition (git submodule)
- `negentropy/` — Negentropy set-reconciliation library (git submodule, for future NIP-77)
- `axs15231b/` — QSPI TFT display driver (JC3248W535)
- `qrcode/` — QR code generator

### Config Format (config.json on SPIFFS)
```json
{
  "nsec": "<64-char hex>",
  "wifi_networks": [{"ssid":"...", "password":"..."}],
  "ap_password": "",
  "mint_url": "https://testnut-nutshell.mints.orangesync.tech",
  "price_per_step": 21,
  "step_size_ms": 60000,
  "nostr_geohash": "u281w0dfz",
  "nostr_relays": ["wss://relay.damus.io", "wss://nos.lol", "wss://relay.anzenkodo.workers.dev", "wss://nostr.koning-degraaf.nl"],
  "nostr_publish_interval_s": 21600,
  "nostr_seed_relays": [
    "wss://relay.orangesync.tech",
    "wss://relay.damus.io",
    "wss://nos.lol",
    "wss://relay.nostr.band",
    "wss://relay.anzenkodo.workers.dev",
    "wss://nostr.koning-degraaf.nl",
    "wss://knostr.neutrine.com",
    "wss://nostr.einundzwanzig.space"
  ],
  "nostr_sync_interval_s": 1800,
  "nostr_fallback_sync_interval_s": 21600,
  "cvm_enabled": true
}
```

## Testing Rules — MANDATORY

### Rule 1: Every new C source file MUST have unit tests
- Place test in `tests/unit/test_<module>.c`
- Test pure-logic functions with known input/output vectors
- Compile with host gcc via `make -C tests/unit`
- Source files remain untouched — stubs in `tests/unit/stubs/` provide ESP-IDF types
- **Run `make test-unit` after any code change. Must pass before commit.**

### Rule 2: Every new HTTP endpoint MUST have integration tests
- Place in `tests/integration/phase<N>.mjs`
- Test against live board using curl + `TOLLGATE_IP` env var
- Never hardcode IP addresses — always use `process.env.TOLLGATE_IP`

### Rule 3: Every new browser-visible feature MUST have Playwright E2E tests
- Place in `tests/e2e/<feature>.spec.mjs`
- Test the full user-visible flow in a browser

### Rule 4: All tests must pass before commit
- `make test-unit` — host unit tests (no hardware needed)
- `make test-integration` — against live Board A (needs hardware)
- `make test-e2e` — Playwright browser tests (needs hardware)

### Rule 5: Test naming conventions
| Test type | Location | Naming | Run command |
|-----------|----------|--------|-------------|
| Host unit | `tests/unit/` | `test_<module>.c` | `make test-unit` |
| Integration | `tests/integration/` | `phase<N>.mjs` or `<feature>.mjs` | `make test-integration` |
| E2E | `tests/e2e/` | `<feature>.spec.mjs` | `make test-e2e` |

### Rule 6: Coverage requirements by code type
| Code type | Required test type | Examples |
|-----------|-------------------|----------|
| Pure math/logic | Unit test | geohash, allotment calc, derivation |
| Crypto operations | Unit test with known vectors | HMAC derivation, Schnorr signing, SHA-256 |
| Token parsing | Unit test with known tokens | Cashu token decode |
| State management | Unit test with mocks | Session lifecycle, firewall client list |
| HTTP endpoints | Integration test | GET /wallet, POST /, POST /wallet/send |
| HTML pages | Playwright E2E | Portal rendering, payment flow |
| Network behavior | Integration test | DNS hijack, NAT, connectivity |

## How to Run Tests

```bash
# Host unit tests (no hardware needed)
make test-unit

# Integration tests (needs Board A connected and flashed)
export TOLLGATE_IP=10.192.45.1
export TOLLGATE_SSID=TollGate-C0E9CA
make test-integration

# E2E tests (needs Board A + browser)
make test-e2e

# All tests
make test-all

# Quick smoke (30s, needs hardware)
make smoke

# Local relay tests (needs board)
make test-local-relay
make test-relay-nip11

# CVM MCP roundtrip (needs board + internet)
make test-cvm-roundtrip
```

## Build & Flash

```bash
source ~/esp/esp-idf/export.sh
make flash          # build + flash to Board A
make flash-a        # same
make flash-b        # flash to Board B
```

## Test Infrastructure

### Host Unit Tests (`tests/unit/`)
- Compile with system gcc, link against `libmbedcrypto` + `libcjson` + secp256k1
- ESP-IDF types provided by stubs in `tests/unit/stubs/`
- Each test file is a standalone binary that returns 0 on success, 1 on failure
- Uses a minimal assert macro: `ASSERT(cond, msg)`
- Golden test vectors: known nsec → expected npub/MAC/SSID/IP

### Integration Tests (`tests/integration/`)
- Node.js scripts that run curl/ping/nmcli against a live ESP32 board
- Require `TOLLGATE_IP` env var (default: auto-detect or error)
- Token generation via nutshell CLI: `cashu -h https://testnut-nutshell.mints.orangesync.tech send --legacy 21`

### E2E Tests (`tests/e2e/`)
- Playwright browser tests
- Config in `tests/e2e/playwright.config.mjs`
- Test the captive portal UI and payment flow

## Environment Variables

| Variable | Default | Purpose |
|----------|---------|---------|
| `TOLLGATE_IP` | (none, must set) | Board A's AP IP (e.g., `10.192.45.1`) |
| `TOLLGATE_SSID` | `TollGate-C0E9CA` | Board A's AP SSID |
| `TEST_TOKEN` | (none) | Cashu token for payment tests |
| `SUDO_PW` | `c03rad0r123` | sudo password for route management |

## External Dependencies

- **Test mint:** `testnut-nutshell.mints.orangesync.tech` — Nutshell/0.20.0, works with cashu CLI
- **Nostr relays:** `relay.damus.io`, `nos.lol`, `relay.anzenkodo.workers.dev`, `nostr.koning-degraaf.nl` — for wifistr events
- **Seed relays:** `relay.orangesync.tech` (NIP-77), `relay.damus.io`, `nos.lol`, `relay.nostr.band`, `relay.anzenkodo.workers.dev`, `nostr.koning-degraaf.nl`, `knostr.neutrine.com`, `nostr.einundzwanzig.space` — for relay selection and sync
- **CVM relay:** `relay.primal.net` — for ContextVM kind 25910 events and CEP-6 announcements
- **Local relay:** Port 4869, LittleFS 4MB partition at 0x500000, max 5000 events, 21-day TTL
- **Nutshell CLI:** `cashu` command for token generation
- **ESP-IDF:** `source ~/esp/esp-idf/export.sh` before `idf.py` commands
- **System libs for unit tests:** `libmbedtls-dev`, `libcjson-dev`

## Reminders

- **Commit + push every time a test passes that previously didn't pass.** Green tests = checkpoint. Don't batch multiple test fixes into one commit.
- Commit + push after each working change
- Board A is at `/dev/ttyACM0`, Board B at `/dev/ttyACM1`, Board C at `/dev/ttyACM3`
- **Per-board locks required** before hardware access: `make lock-a PHASE="desc"`, lock files in `physical-router-test-automation/locks/`
- `sudo` password: `c03rad0r123`
- SPIFFS is at offset `0x410000`, size `0xF0000` — erase with `esptool.py erase_region 0x410000 0xF0000` if config is stale
- NVS stores wallet proofs — erasing NVS clears wallet balance
- **Relay storage** LittleFS at offset `0x500000`, size `0x400000` (4MB) — auto-formatted on first boot
- The `nostr_event.c` `created_at` field uses `gettimeofday()` — mock this in unit tests
- Wifistr event signing uses `secp256k1_schnorrsig_sign32()` — verify with `_verify()` in tests
- relay_validator.c does Schnorr verify + SHA-256 event ID — test with `test_relay_validator`
- relay_selector scoring: NIP-77 bonus (1000pts) + latency + failure penalty (100pts each) — test with `test_relay_selector`
- Portal HTML has server-side template substitution (`__AP_IP__`, `__PRICE__`, `__MINT_URL__`) — no JS fetch
- **WiFi country code:** Must set `esp_wifi_set_country_code("DE")` before `esp_wifi_start()` — defaults to CN which causes auth failures on EU APs
- Default nsec: `a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2`

## Git Remotes & Repositories

Full details in `REMOTES.md`. Key facts:

- **This repo** (`esp32-tollgate`): `nostr://npub12m5.../git.orangesync.tech/esp32-tollgate`
- **GRASP server:** `git.orangesync.tech` (git smart HTTP)
- **Nostr relay:** `wss://ngit.orangesync.tech` (state events)
- **GitWorkshop:** `workshop.orangesync.tech` (web UI)
- **NerdQAxePlus fork:** `nostr://npub12m5.../git.orangesync.tech/esp-miner-nerdqaxeplus-tollgate`
- **Worktrees:** `esp32-miner-integration` (feature/miner-integration), `esp32-tollgate-arch` (feature/tollgate-core-component)
- **Push commands:** `git push orangesync --all` (esp32-tollgate), `git push ngit-origin develop` (NerdQAxePlus)
- **Backup bundles:** `/home/c03rad0r/mining-work-backup/`
- Board A nsec: `9af47906b45aca5e238390f3d03c8274e154198e81aa2095065627d1e61ca968`
