# Outdoor Irrigation Controller

WiFi-controlled outdoor irrigation firmware for LilyGo T3-S3 e-paper boards:

- **wifi_station** — pump station timer, VFD control, remote pump input, VFD fault alerting, InfluxDB telemetry
- **wifi_solenoid** — zone solenoid control over HTTP

Repository: [github.com/infinite-tree/outdoor-irrigation-controller](https://github.com/infinite-tree/outdoor-irrigation-controller)

## Setup

1. Copy `lib/Config/Config.h.example` to `lib/Config/Config.h` and set WiFi, static IPs, InfluxDB credentials (station), optional `VFD_ALERT_URL`, and BLE pressure sensor settings (solenoid).
2. Build and upload with PlatformIO from the repo root:
   - Station: `pio run -d wifi_station -t upload`
   - Solenoid: `pio run -d wifi_solenoid -t upload`

### Config (`lib/Config/Config.h`)

| Define | Used by | Purpose |
|--------|---------|---------|
| `STATION_STATIC_IP_*` | wifi_station | This board’s static IP |
| `SOLENOID_STATIC_IP_*` | wifi_solenoid | Solenoid board’s static IP |
| `SOLENOID_HTTP_HOST` | wifi_station | Hostname/IP for solenoid HTTP API |
| `LAN_GATEWAY_*`, `LAN_SUBNET_*`, `LAN_DNS_PRIMARY_*` | both | Shared network settings |
| `WIFI_SSID`, `WIFI_PASSWORD` | both | WiFi credentials |
| `INFLUX_*` | both | InfluxDB telemetry (solenoid: pressure; station: pump/zones) |
| `VFD_ALERT_URL` | wifi_station | POST target when Frenic fault input activates (empty = disabled) |
| `VFD_ERROR_SUMMARY` | wifi_station | `error_summary` field in that POST body |
| `VFD_ERROR_DEBOUNCE_MS` | wifi_station | Fault must stay active this long before alarm/webhook (default 2 s) |
| `VFD_ERROR_CLEAR_DEBOUNCE_MS` | wifi_station | Fault must stay inactive this long before UI clears (default 5 s) |
| `VFD_ALERT_COOLDOWN_MS` | wifi_station | Minimum interval between any alert POSTs (default 30 min) |
| `BLE_PRESSURE_*` | wifi_solenoid | BLE MAC, GATT UUIDs, and linear pressure scale; see `lib/BlePressureSensor` |
| `PRESSURE_*` | wifi_station | Poll intervals (idle/active), thresholds, low/high alarm durations, battery floor; alerts use `VFD_ALERT_URL` |
| `STATION_WATCHDOG_TIMEOUT_MS` | wifi_station | Main-loop watchdog; auto-restart if the loop stalls (default 2 min) |
| `STATION_WIFI_RESTART_MS` | wifi_station | Restart after prolonged WiFi loss (default 1 h; `0` disables) |

### BLE pressure sensor (wifi_solenoid + wifi_station)

The solenoid board reads a BLE pressure sensor on a timer (`BLE_PRESSURE_REFRESH_MS`, default 10 minutes) with retries and automatic NimBLE stack reset after repeated failures (`BLE_PRESSURE_STACK_RESET_FAILURES`). Failed reads retry sooner (`BLE_PRESSURE_FAIL_RETRY_MS`, default 1 minute). Last-good PSI is kept for the e-paper display and `/status` (`pressure_stale: true` when aged); InfluxDB only receives fresh successful reads. The station polls solenoid `/status` every `STATION_PRESSURE_POLL_INTERVAL_MS` while the pump/VFD is off (default 5 minutes) and every `STATION_PRESSURE_POLL_ACTIVE_MS` while the pump is on, including remote-only operation (default 1 minute). A poll runs immediately when the VFD transitions from off to on. The station web UI also triggers a solenoid refresh at most every `STATION_PRESSURE_STATUS_REFRESH_MS` (default 15 seconds) when serving `/status`, so the displayed PSI tracks the solenoid without waiting for the background poll. Failed polls keep the last-known PSI as stale instead of clearing the display, and retry sooner (`STATION_PRESSURE_FETCH_RETRY_MS`) when no reading is cached yet.

If the pump is on (`vfd` > 0) and pressure stays below `PRESSURE_LOW_PSI` for `PRESSURE_LOW_ALARM_DURATION_MS`, or above `PRESSURE_HIGH_PSI` for `PRESSURE_HIGH_ALARM_DURATION_MS`, or if battery drops below `PRESSURE_BATTERY_LOW_PCT`, the station POSTs to `VFD_ALERT_URL` with the configured summary strings (same JSON shape as the VFD fault alert).

Raw values use **signed int16 big-endian tenths of a psi** by default (`BLE_PRESSURE_SCALE_TENTHS`, `BLE_PRESSURE_BIG_ENDIAN`): `psi = raw / 10` as two's-complement, MSB first (e.g. nRF `03-A8` → `0x03A8` → 94 psi, `FF-F4` → `0xFFF4` → −1.2 psi). Set `BLE_PRESSURE_BIG_ENDIAN` to `0` for LSB-first sensors, or `BLE_PRESSURE_SCALE_TENTHS` to `0` for legacy two-point linear scaling.

### VFD fault input (wifi_station)

GPIO **40** (`VFD_ERROR_INPUT_PIN`) — `INPUT_PULLUP`, active **LOW** when the PC817 pulls low (Frenic Mini fault relay on terminals **30A–30C**, normal logic).

On a sustained fault (active for `VFD_ERROR_DEBOUNCE_MS`, default 2 s) the station updates the e-paper **VFD %:** line to **ERROR**, sets `vfd_error` in `/status`, and POSTs JSON:

```json
{"error_summary":"…","datetime":"2025-05-27T14:30:00-0700"}
```

`datetime` uses local time from NTP (`MY_TZ` in `wifi_station.ino`). The fault input is majority-sampled over ~160 ms (same approach as the remote pump input). Alert POSTs are rate-limited to one per `VFD_ALERT_COOLDOWN_MS` (default 30 minutes) across all alert types sharing `VFD_ALERT_URL`. The UI clears only after the fault input stays inactive for `VFD_ERROR_CLEAR_DEBOUNCE_MS` (default 5 s); a real Frenic fault stays latched until reset on the drive.

### Station watchdog (wifi_station)

The station enables the ESP32 task watchdog on boot (`STATION_WATCHDOG_TIMEOUT_MS`, default 2 minutes). The main loop feeds the watchdog each iteration and during long operations (remote input sampling, e-paper refresh). If the firmware hangs without returning to the loop, the board restarts automatically.

If WiFi stays disconnected longer than `STATION_WIFI_RESTART_MS` (default 1 hour, `0` to disable), the station restarts to recover from a stuck network stack.

`GET /health` returns uptime, free heap, WiFi status, pump/remote state, and pressure poll age for external monitoring.

## Custom logo

Source image: `logo-200-blk.png` (converted to `lib/Logo/Logo.h`).

To regenerate the bitmap:

1. Use [image2cpp](https://javl.github.io/image2cpp/)
2. Canvas size 250×128 for full screen
3. Match background color to the image
4. Scale and center; flip horizontally when using `display.setRotation(3)`
5. Arduino code, horizontal, 1 bit per pixel
6. Paste into `lib/Logo/Logo.h`, remove `PROGMEM` and trailing assignments
7. Draw with: `display.drawExampleBitmap(logo_200_blk, 0, 0, 72, 128, GxEPD_BLACK);`

## Web UI

Edit files under each project’s `web/` folder (`index.html`, `style.css`, `app.js`). A pre-build step embeds them into `web_assets.h` as PROGMEM. HTTP routes and JSON live in `web_ui.cpp`; hardware and irrigation logic stay in the `.ino` files.

- **wifi_station** — timer, zone status, watering history
- **wifi_solenoid** — zone 1/2 on/off (`GET /status` includes pressure, `POST /set_zone` with `zone1` and `zone2`)

## Libraries

Vendored under `lib/`:

- `GxEPD` — e-paper display
- `Adafruit-GFX-Library` — fonts and graphics
- `BlePressureSensor` — reusable BLE pressure sensor client (NimBLE, ESP32)
- `RootCert` — ISRG Root X1 for InfluxDB HTTPS
- `Logo` — custom bitmap
- `Config` — local settings and secrets (gitignored)
