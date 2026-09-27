# GSM Upstream Integration — Technical Reference (CTG-1)

Status line: the **AT bring-up core is implemented and host-tested**; the
**PPPoS attach and the on-hardware validation are not done** and are gated on
hardware that is not in hand. Details below.

## Overview

Mobile-data upstream for the car TollGate gateway: an ESP32-S3 drives a
cellular modem over UART and uses it as the resold upstream instead of (or
alongside) WiFi STA. SilentLink non-KYC eSIM profiles are the intended SIM.

## Hardware: what is actually in hand (2026-07-06 decision, unchanged)

From `docs/HARDWARE-INVENTORY.md` §"Cellular Upgrade Path":

| Role | Module | State |
|------|--------|-------|
| PRIMARY | **SIM7600E-H** (SIMCom, Cat-4 LTE, EU bands B1/B3/B7/B20/B28) | **TO SOURCE** — not in hand |
| Alternative | A7670E (Cat-1, cheaper, 10/5 Mbps) | not in hand |
| In hand | SIM800L EVB, SIM800C dual-relay | **2G only — deprecated for data** (relay control only) |
| In hand | GL.iNet Mudi V2 (GL-E750V2, Quectel EM060K-G LTE, OpenWrt) | can be the gateway itself |

Consequences for this card:

* **Nothing here can validate mobile data today.** There is no LTE modem in
  hand and no SilentLink SIM has been reported as fitted to a physical SIM. The
  2G SIM800 boards *might* still attach in the lab (Germany has 2G left) but the
  inventory explicitly deprecates them for data, so a 2G result is a lab
  curiosity, not acceptance.
* No `/dev/ttyUSB*` or `/dev/ttyACM*` device was present on this machine, so not
  even an AT session could be attempted.
* If sourcing the SIM7600E-H is not wanted, the cheaper correct path is
  CTG-5-style: use the GL-E750V2 (already in hand, OpenWrt, built-in LTE) as the
  gateway and keep the ESP32 out of the PPP business.

## Firmware: `main/gsm_upstream.{c,h}` (implemented)

Split deliberately in two halves.

### 1. AT protocol core — host-testable, no ESP-IDF dependencies

Compiled with `-DTEST_HOST` into `tests/unit/test_gsm_upstream`
(190 assertions, green). It contains, and nothing else:

| Function group | Purpose |
|----------------|---------|
| `gsm_classify_response`, `gsm_response_error_code` | OK / ERROR / CONNECT / NO CARRIER / BUSY / NO DIALTONE / `+CME`/`+CMS ERROR` |
| `gsm_parse_cpin`, `gsm_sim_state_name` | SIM state, incl. `+CME ERROR` 10/11/12/13/14/15 mapping |
| `gsm_parse_csq`, `gsm_rssi_dbm`, `gsm_rssi_bars` | index → dBm (`-113 + 2·rssi`) → 0..5 bars |
| `gsm_parse_reg`, `gsm_reg_is_registered` | `+CREG?`/`+CGREG?`/`+CEREG?`, 1- and 2-param forms, lac/tac |
| `gsm_parse_cops`, `gsm_act_is_2g`, `gsm_act_name` | operator name + AcT, 2G detection |
| `gsm_apn_valid`, `gsm_build_cgdcont`, `gsm_dial_command` | APN validation (also an AT-injection guard) and command builders |
| `gsm_ctx_init`, `gsm_next_command`, `gsm_feed_response` | the bring-up state machine |
| `gsm_diagnose`, `gsm_failure_name` | transcript → one failure reason |

The state machine is `IDLE → AT_PROBE → SIM_CHECK → SIGNAL → REG_CREG →
REG_CEREG → PDP_SETUP → DIAL → PPP_UP`, with `ERROR` terminal. `REG_CEREG` is
tolerant: a 2G-only modem answers `ERROR` to `AT+CEREG?` and the machine carries
on to PDP setup (flag `lte_reg_unsupported`). Each state has a retry budget
(`max_attempts`); exhausting it maps to a specific reason (no answer, no
coverage, dial failed) rather than a generic timeout.

### 2. Target glue — power, UART, AT transport

PWRKEY pulse (GPIO4), UART1 TX GPIO44 / RX GPIO43, CR/LF line reader, and a
transport that feeds every line into the core. Baud: 115200 for the SIM7600
class, 9600 for SIM800 (`GSM_UART_BAUD` / `--modem sim800`).

**The UART has to be handed to LwIP PPPoS after `CONNECT`.** That attach is
**not implemented**: `gsm_start()` runs bring-up and then logs
`PPP attach not implemented` and returns false. Reason: it needs the
`pppos_create()` / `pppos_input()` input/output driver wiring, which cannot be
validated here without a modem, and guessing the IDF API would risk breaking the
target build. `gsm_bring_up()` — everything up to and including `CONNECT` — is
complete and is what the bench harness exercises.

### Remaining work for PPP attach (do this when a modem is on the bench)

The old plan was to pull in `espressif/esp_modem` from the component registry.
That is **not required** by the current design and this repo has no
`main/idf_component.yml` at all: the AT core already speaks the protocol, and
the attach only needs LwIP PPPoS plus the esp_netif PPP driver. Prefer that over
adding a registry dependency that cannot be built offline.

1. Verify which Kconfig symbol gates the esp_netif PPP netif. In IDF 5.3,
   `ESP_NETIF_DEFAULT_PPP()` (`components/esp_netif/include/esp_netif_defaults.h`)
   is compiled only under `#ifdef CONFIG_PPP_SUPPORT`, while this repo sets
   `CONFIG_LWIP_PPP_SUPPORT` in `sdkconfig.defaults`. A grep of the IDF Kconfigs
   does not turn up a `config PPP_SUPPORT` definition, so confirm the real
   switch in `idf.py menuconfig` before writing code against it.
2. Implement the PPPoS driver: `pppos_create()` on a `netif` plus an
   esp_netif driver (`esp_netif_attach` with a `transmit` callback) that calls
   `pppos_input()` for bytes arriving on UART1. `esp_netif_ppp_set_params()`
   with `ppp_phase_event_enabled` / `ppp_error_event_enabled` is already
   available for logging PPP phase and error events.
3. Wire `gsm_start()` into `start_services()` as an alternative upstream, and
   decide route priority vs WiFi STA (see "Open design questions").
4. Add an integration test that asserts an IP is obtained
   (`tests/integration/`), which is the only thing that can accept CTG-1.

## Bench validation (operator runbook)

`tools/gsm-validate/validate_gsm.py` walks the same AT sequence from a laptop
with the shield on a USB-serial adapter — no firmware flash, no build:

```
# 1. wire the shield: TX/RX crossed to the adapter, GND common,
#    and a supply that can do 2 A peaks (USB alone is not enough)
# 2. run it (needs pyserial for live mode)
python3 tools/gsm-validate/validate_gsm.py --port /dev/ttyUSB0 --modem sim800 \
        --apn silent.link --out ~/ctg1-transcript.txt
#    SIM7600 class: --modem sim7600 (115200 baud default)

# 3. no hardware? the parser is self-testing:
python3 tools/gsm-validate/validate_gsm.py --self-test          # 4/4 fixtures
python3 tools/gsm-validate/validate_gsm.py --replay <file>      # parse a capture
```

Exit code 0 = the modem reached `CONNECT` (data mode); 2 = failed, with one
reason in the same vocabulary as the firmware's `gsm_failure_name()`. A
`TWO_G_ONLY` verdict passes but warns: registered on a 2G cell only.

**What a PASS does *not* prove.** `CONNECT` is the last AT step; after it the
UART carries PPP frames and the script cannot look inside. Radio, SIM,
registration and PDP context are proven — the IP layer is not. Only the
firmware PPPoS attach (above) can prove that.

## SilentLink (silent.link)

- Non-KYC eSIM provider, top-up over Lightning (BTCPay), global roaming.
- eSIM → physical SIM flashing is done by the operator, out of band.
- No programmatic API; ordering is web/QR.
- APN: `silent.link`.

## Open design questions (unchanged, still need the operator)

1. **Dual upstream or GSM-only?** GSM as primary vs failover alongside WiFi STA.
   `docs/HARDWARE-INVENTORY.md` currently leans "GL-E750V2 as primary gateway,
   ESP32 + LTE shield optional".
2. **Power.** 2 A peaks during TX; is the modem duty-cycled when no clients are
   attached, or always on? On a car this is the difference between a flat
   12 V battery and not.
3. **Data metering.** SilentLink plans have limits; `step_bytes` exists in
   config, so price-per-byte has to cover the cellular cost.

## Status

- [x] AT protocol core (`main/gsm_upstream.c`, 190 host assertions green)
- [x] APN validation + `AT+CGDCONT` builder, SilentLink default
- [x] Bring-up state machine with per-state retries and failure diagnosis
- [x] Operator bench harness + self-testing parser (`tools/gsm-validate/`)
- [x] Documented which module is the target (SIM7600E-H) and that it is unsourced
- [ ] **Source the SIM7600E-H shield (or approve the GL-E750V2 path)** — blocks everything below
- [ ] Fit the SilentLink profile to a physical SIM
- [ ] PPPoS attach in `gsm_start()` (see remaining work)
- [ ] 2 A-capable supply wiring (no USB-only power)
- [ ] Integration test: IP obtained through PPP
- [ ] Live bench run of `validate_gsm.py` against the real SIM, transcript kept as evidence

## References

- `main/gsm_upstream.h` — API, wiring constants, target-only glue contract
- `tests/unit/test_gsm_upstream.c` — 190 assertions
- `tools/gsm-validate/validate_gsm.py` — bench harness, `--self-test` fixtures
- `docs/HARDWARE-INVENTORY.md` §Cellular Upgrade Path, §For CTG-1
- `sdkconfig.defaults` — `CONFIG_LWIP_PPP_SUPPORT` and PAP/CHAP already enabled
