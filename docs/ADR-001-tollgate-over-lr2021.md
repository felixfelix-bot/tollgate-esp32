# ADR-001: TollGate Balloon Uses LR2021 Radio as Data Link

**Date:** 2026-07-29
**Status:** Proposed
**Decision Maker:** Felix (operator)
**Supersedes:** Implicit assumption that balloon TollGate uses WiFi captive portal

## Context

The original ESP32 TollGate firmware (tollgate-esp32 repo) is a WiFi captive portal hotspot. Clients connect via WiFi, get DNS-hijacked to a captive portal HTML page, pay with Cashu e-cash tokens, and receive internet access via NAPT.

The balloon project uses LR2021 LoRa radios for long-range communication between the balloon payload and ground stations. WiFi is not the data link — LR2021 is.

## Decision

**The balloon TollGate variant will use LR2021 radio as its data link, NOT WiFi.**

The original ESP32 TollGate keeps its WiFi + BitChat + captive portal stack for other contexts (ground-based, car tollgate, etc.). The balloon variant extracts only the **business logic layer** and adapts it for LR2021 transport.

### What This Means

| Layer | Original TollGate | Balloon TollGate |
|-------|-------------------|-----------------|
| Data link | WiFi AP+STA | LR2021 LoRa radio |
| Client connection | WiFi AP join + DHCP | Radio message exchange |
| Payment UI | HTML captive portal in browser | Protocol-level (no browser) |
| DNS hijack | Yes (redirect to portal) | No (no DNS over LoRa) |
| NAPT/forwarding | Yes (share STA internet over AP) | N/A (radio relay, different model) |
| Cashu wallet | nucula library, swap tokens | SAME — reuse |
| Identity | nsec → MAC/SSID/IP | nsec → radio identity (adapt) |
| Nostr signing | nostr_event.c | SAME — reuse |
| Mint health | mint_health.c | SAME — reuse |

### What Stays (Business Logic — Radio-Agnostic)

1. **Cashu payment logic** — token receive, validate, swap via nucula wallet
2. **Identity derivation** — nsec → deterministic identity (adapt output format for radio)
3. **Nostr event signing** — event creation + signature
4. **Mint health checking** — mint status, accepted mints list
5. **Geohash** — location encoding for balloon position
6. **Config** — config.json, nsec loading

### What Changes (Transport Layer)

1. **No WiFi AP/STA** — replaced by LR2021 radio init + TX/RX
2. **No captive portal HTML** — replaced by radio protocol for payment exchange
3. **No DNS server** — no DNS concept over LoRa
4. **No HTTP server** — payment API becomes radio message handlers, not HTTP endpoints
5. **No NAPT** — radio relay model instead of NAT

### What's Dropped Entirely (Not Needed)

- display.c, font.c, touch.c, keyboard.c (no display on balloon)
- All mining components
- All marketplace/CVM/MCP components
- wifistr (balloon IS the service)
- dns_server.c (no DNS over LoRa)
- captive_portal.c HTML (no browser over LoRa — protocol replaces it)

## Implications

1. **The C3 WiFi build we just did is a stepping stone, not the final architecture.** It proves the business logic compiles and runs on C3. The transport layer will be swapped.

2. **Payment protocol needs design.** Over WiFi, clients paste Cashu tokens into an HTML form. Over LR2021, we need a lightweight message protocol: client sends token → ESP32 validates/swaps → grants access (to what? the balloon's radio relay? local Nostr relay?).

3. **"Internet access" concept changes.** Over WiFi, NAPT shares the STA uplink. Over radio, the balloon might relay messages to/from ground — payment gates radio relay time or bandwidth.

4. **Local Nostr relay still valuable.** Clients within radio range can publish/read events via the balloon's local relay, even without internet. Payment gates access to this relay.

5. **Cashu model unchanged.** Online = swap tokens against real mint. Offline = roadmap items R1/R2/R3 from BALLOON-SCOPE-DECISIONS.md.

## Consequences

- The captive_portal.c and dns_server.c files we kept are NOT needed for balloon. They remain useful for the original WiFi TollGate but should not be ported to the balloon radio variant.
- A new **radio payment protocol** needs to be designed (ADR-002 candidate).
- The C3 build is still useful — it validates the business logic layer compiles and the nucula wallet works on C3 hardware.
- Cross-track coordination needed: balloon-range-tests / balloon-firmware tracks own LR2021 driver. TollGate track consumes their radio API.

## Open Questions

1. What exactly does payment grant over radio? Relay time? Message quota? Bandwidth?
2. Is there a session concept, or per-message payment?
3. How does a client "discover" the balloon TollGate over LR2021? Beacon?
4. Does the balloon relay Nostr events to ground, or is it purely local?

## Related

- BALLOON-SCOPE-DECISIONS.md (component keep/drop)
- ADR-017 (from balloon-fresh repo — LR2021 throughput)
- balloon-range-tests track: LR2021 driver
- balloon-firmware track: RP2040/ESP32 FLRC board firmware
