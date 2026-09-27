# GPS (NMEA) integration — CTG-8

Exposes the car's position over ContextVM/MCP so a phone or a relay-side agent
can ask the TollGate where it is.  Hardware is the GY-GPS6MV2 (u-blox NEO-6M)
module already listed in `docs/HARDWARE-INVENTORY.md` — UART, NMEA 0183,
9600 baud 8N1.

## Firmware module

New files:

- `main/gps.h` / `main/gps.c` — NMEA 0183 parser + fix state machine.
- `tests/unit/test_gps.c` — host unit tests (73 assertions).

Public API (`main/gps.h`):

| Function | Purpose |
|---|---|
| `int gps_nmea_checksum_ok(const char *sentence)` | Validate the `*XX` XOR checksum of a sentence. |
| `bool gps_parse_sentence(const char *s, gps_fix_t *fix)` | Parse one sentence into `*fix` (GGA + RMC only). |
| `bool gps_feed_line(const char *line)` | Feed one raw UART line into the module-wide fix. |
| `const gps_fix_t *gps_get_fix(void)` | Current fix snapshot (never NULL). |
| `void gps_reset_state(void)` | Clear the fix (config reload / tests). |
| `esp_err_t gps_start(int uart, int tx, int rx, int baud)` | Install UART + start the reader task. |

`gps_fix_t` carries `valid`, `latitude`, `longitude`, `altitude_m`, `hdop`,
`speed_knots`, `course_deg`, `satellites`, `fix_quality`, `utc_time`,
`utc_date`, `updated_ms` and a pre-computed `geohash` (9 chars, via the
existing `main/geohash.c`).

Behaviour details:

- Only the `$..GGA` and `$..RMC` sentence types are parsed; any talker ID
  (`$GP`, `$GN`, `$GL`, ...) is accepted by inspecting the last three
  characters of the sentence tag.  GSA/GSV/VTG/TXT are ignored, so a bad or
  unknown sentence never corrupts the fix.
- Sentences with a wrong/missing checksum are dropped without touching state.
- GGA and RMC **merge** into the same state: GGA contributes altitude, HDOP,
  satellite count and fix quality; RMC contributes status, speed, course and
  date.  A sentence that has not refreshed a field leaves the previous value
  intact.
- A fix is `valid` only when GGA reports `fix_quality > 0` (or RMC reports
  status `A`) **and** the sentence actually carried coordinates.  Empty
  ("no fix") sentences therefore flip `valid` to false.
- The geohash is recomputed from lat/lon on every accepted coordinate line.

## Config

`config.json` gains an optional `gps` object (all fields optional):

```json
{
  "gps": {
    "enabled": true,
    "uart_num": 1,
    "tx_pin": 8,
    "rx_pin": 9,
    "baud": 9600
  }
}
```

A top-level `"gps_enabled": true` is also honoured.  Defaults (`enabled`
false, UART1, TX GPIO8 / RX GPIO9, 9600 baud) come from the carrier-board plan
in `docs/PCB-DESIGN-PLAN.md`.  `enabled` is **false by default** so the
firmware never claims UART1 / GPIO8 / GPIO9 on a board where the module is not
wired — the reader task only starts when it is switched on.

`start_services()` in `main/tollgate_main.c` calls `gps_start()` right after
`cvm_server_start()` when `cfg->gps_enabled` is set, and logs the result.

## ContextVM / MCP tool

`get_location` is registered in the CEP-6 tool list (`main/cvm_server.c`) and
dispatched by `main/mcp_handler.c`:

```
MCP_TOOL_GET_LOCATION = 10
```

The tool takes no parameters and returns the current fix as JSON:

```json
{
  "valid": true,
  "latitude": 48.1173,
  "longitude": 11.5167,
  "geohash": "u281wp6pt",
  "altitude_m": 545.4,
  "satellites": 8,
  "hdop": 0.9,
  "fix_quality": 1,
  "speed_knots": 22.4,
  "course_deg": 84.4,
  "utc_time": "123519",
  "utc_date": "230394",
  "updated_ms": 1500
}
```

When no fix has been acquired yet, `valid` is `false`, `geohash` is `""` and
the numeric fields are zero.  `updated_ms` is the device uptime (ms) at which
the fix was last refreshed, so a client can age out stale positions.

## Tests

```sh
make -C tests/unit test_gps          # 73 assertions
make -C tests/unit test_mcp_handler  # 77 assertions (incl. get_location)
```

`tests/unit/test_gps.c` covers the checksum validator, lat/lon parsing for all
four hemispheres, the no-fix / void-RMC cases, sentence merging, CR/LF
tolerance, rejection of corrupted lines without state mutation, geohash
generation (checked against an independent Python reference — see
`evidence/t_d4b25b87/geohash_ref.py`), and reset.

`tests/unit/test_mcp_handler.c` asserts the `get_location` tool name maps to
`MCP_TOOL_GET_LOCATION`, that dispatch returns valid JSON both before and
after a fix, and that the JSON payload matches the parsed NMEA values.

## Known limitations / follow-ups

- No time synchronisation from NMEA (`utc_time`/`utc_date` are reported but
  not used to set the system clock).
- The geohash is exposed over MCP but is not yet published in the Nostr
  `nostr_geohash` beacon — `get_location` is read-only for now.
- The tool list fixture inside `tests/unit/test_cvm_server.c` is a
  hand-maintained copy of the tool names and was not updated to include
  `get_location`; it does not link `main/cvm_server.c`.
- `updated_ms` comes from `esp_timer_get_time()`; it resets on reboot.
