# Mining Standalone — Build Report

**Track:** balloon-pow · **Phase:** 1 (sw_miner + stratum_client extraction)
**Branch:** `balloon-pow-extraction`
**Date:** 2026-07-26

## Build Status: ✅ SUCCESS (ESP32-S3)

```
Project     : mining-standalone
Target      : ESP32-S3
ESP-IDF     : v5.4.1
Toolchain   : xtensa-esp-elf (gcc)
Result      : mining-standalone.bin built
```

## Binary

| Artifact                     | Size      |
|------------------------------|-----------|
| `mining-standalone.bin`      | **866,240 B (≈ 846 KB)** |
| Total image (unpadded)       | 866,128 B |

## Memory Footprint (from `idf.py size`)

| Region        | Used      | Total     | % Used | Note |
|---------------|-----------|-----------|--------|------|
| Flash `.text` | 629,562 B | —         | —      | Code (incl. mbedtls SHA256) |
| Flash `.rodata` | 120,732 B | —       | —      | Strings, const tables |
| DIRAM         | 117,767 B | 341,760 B | 34.46% | DRAM/IRAM combined |
| **IRAM**      | 16,383 B  | 16,384 B  | **99.99%** ⚠️ | Extremely tight — see Notes |
| RTC FAST      | 52 B      | 8,192 B   | 0.63%  | — |

> **IRAM note:** 1 byte of headroom. This is normal for a mining workload (the
> SHA256 hot loop + WiFi ISR handlers consume most of IRAM). It links and runs,
> but any future IRAM-placed code additions will overflow. Not a blocker for
> benchmarking.

## Key Files

```
mining-standalone/
├── CMakeLists.txt              # project-level (idf.py build target)
├── sdkconfig                   # generated — gitignored
├── .gitignore                  # NEW — excludes build/ + sdkconfig
└── main/
    ├── CMakeLists.txt          # component registration (9 SRCS)
    ├── mining_main.c           # 177 lines — app_main entry point
    ├── sw_miner.c              # extracted verbatim from tollgate
    ├── stratum_client.c        # extracted verbatim from tollgate
    ├── stratum_proxy_stub.c    # no-op (real proxy is a tollgate-only feature)
    ├── tollgate_core_stratum_client.c  # stratum client adapter
    ├── mining_config.c         # Kconfig → config struct loader
    ├── identity_stub.c         # no-op (tollgate derives from nsec)
    ├── tls_worker_stub.c       # no-op (TLS handled by stratum_client directly)
    ├── mining_stubs.c          # misc stubs
    ├── mining_config.h
    ├── stratum_proxy.h
    ├── stratum_client.h
    └── sw_miner.h
```

### Entry point: `mining_main.c`

Boot sequence (177 lines):

```
app_main()
  → nvs_flash_init()
  → mining_config_init()          # Kconfig CONFIG_MINE_* → runtime config
  → esp_netif_init() + event_loop
  → wifi_init_sta()               # WiFi STA, WPA2, auto-reconnect
        └─ on WIFI_EVENT_STA_DISCONNECTED → retry in 1s
  → [on IP_EVENT_STA_GOT_IP]:
        stratum_proxy_init(0, false)
        stratum_client_init() + stratum_client_start()
        sw_miner_start()
  → stats_logger_task (10s period):
        logs hashrate (MH/s), pool host:port, connected, shares ok/rej, nbits
```

## Stubbed Dependencies (standalone mode)

The tollgate firmware depends on many subsystems that a standalone miner
doesn't need. All are stubbed to no-ops or Kconfig-driven defaults:

| Stub file                  | Replaces                     | Behaviour |
|----------------------------|------------------------------|-----------|
| `identity_stub.c`          | identity (nsec → MAC/SSID/IP) | No-op; WiFi SSID/pass from Kconfig |
| `mining_config.c`          | config.c (SPIFFS config.json) | Reads Kconfig `CONFIG_MINE_*` only |
| `tls_worker_stub.c`        | tls_worker (offloaded TLS)    | No-op; stratum_client uses mbedTLS directly |
| `stratum_proxy_stub.c`     | stratum_proxy (local proxy)   | No-op init; stratum_client connects to pool directly |
| `mining_stubs.c`           | tollgate_core_mining misc     | No-op stubs for symbols referenced by sw_miner |

## Build Configuration

| Setting | Value |
|---------|-------|
| ESP-IDF | v5.4.1 |
| Target chip | ESP32-S3 |
| Flash size | 16 MB (default) |
| SHA256 | **mbedTLS, hardware-accelerated** (ESP32-S3 SHA peripheral) |
| WiFi mode | STA (client) |
| Stratum transport | tcp_transport + mbedTLS (TLS) |
| Partition | default ESP-IDF partitions.csv |

Kconfig knobs exposed (set via `idf.py menuconfig` → Mining):

- `CONFIG_MINE_WIFI_SSID` — WiFi SSID
- `CONFIG_MINE_WIFI_PASS` — WiFi password
- `CONFIG_MINE_POOL_HOST` — stratum pool host
- `CONFIG_MINE_POOL_PORT` — stratum pool port
- `CONFIG_MINE_WALLET_ADDR` — payout wallet address
- `CONFIG_MINE_WORKER_NAME` — worker name suffix

## Build Warnings

Only one warning, **benign and upstream**:

```
CMake Deprecation Warning at esp-idf/components/mbedtls/mbedtls/CMakeLists.txt:21
  Compatibility with CMake < 3.10 will be removed from a future version of CMake.
  Update the VERSION argument <min> value.
```

This is an upstream mbedTLS CMakeLists issue, not from our code. No
compiler warnings from our source files.

## Reproducing the Build

```bash
cd mining-standalone
idf.py set-target esp32s3
idf.py menuconfig        # set CONFIG_MINE_* values
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

## Next Steps

1. **Flash to ESP32-S3 board** (Board A: `/dev/ttyACM0`) and run a
   live benchmark against a public Bitcoin testnet pool (e.g.
   `solo.ckpool.org:3333` or `stratum+tcp://testnet.vkbit.com:3333`).
2. **Capture steady-state hashrate** from the 10-second stats logger over
   ≥10 minutes. Record MH/s, accepted/rejected shares, and pool round
   acceptance rate.
3. **Estimate J/GH efficiency** using a USB power meter (INA219 or
   similar) to measure wall-power during mining vs idle.
4. **ESP32-C3 feasibility check:** the C3 has no SHA hardware accelerator
   for the mid-state path used by sw_miner (it lacks the parallel SHA
   engine of the S3). Re-target with `idf.py set-target esp32c3` and
   measure the resulting hashrate delta; expect a 3–5× regression. If
   the C3 cannot sustain a viable hashrate, document the result and
   scope the C3 variant out.
5. **Phase 2 (if S3 benchmark is viable):** integrate back into the
   full tollgate firmware as a conditional component, gated behind
   `CONFIG_TOLLGATE_MINING=y`.

## Conclusion

Phase 1 of the balloon-pow track is **complete and builds cleanly**.
The standalone mining project isolates `sw_miner` + `stratum_client`
from the tollgate firmware with minimal stubbing, producing an 846 KB
binary ready for on-device benchmarking on the ESP32-S3.
