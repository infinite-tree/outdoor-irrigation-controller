# Outdoor Irrigation Controller

WiFi-controlled outdoor irrigation firmware for LilyGo T3-S3 e-paper boards:

- **wifi_station** — pump station timer, VFD control, remote pump input, VFD fault alerting, InfluxDB telemetry
- **wifi_solenoid** — zone solenoid control over HTTP

Repository: [github.com/infinite-tree/outdoor-irrigation-controller](https://github.com/infinite-tree/outdoor-irrigation-controller)

## Setup

1. Copy `lib/Config/Config.h.example` to `lib/Config/Config.h` and set WiFi, static IPs, InfluxDB credentials (station), and optional `VFD_ALERT_URL`.
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
| `INFLUX_*` | wifi_station | InfluxDB telemetry |
| `VFD_ALERT_URL` | wifi_station | POST target when Frenic fault input activates (empty = disabled) |
| `VFD_ERROR_SUMMARY` | wifi_station | `error_summary` field in that POST body |

### VFD fault input (wifi_station)

GPIO **40** (`VFD_ERROR_INPUT_PIN`) — `INPUT_PULLUP`, active **LOW** when the PC817 pulls low (Frenic Mini fault relay on terminals **30A–30C**, normal logic).

On a new fault edge the station updates the e-paper **VFD %:** line to **ERROR**, sets `vfd_error` in `/status`, and POSTs JSON:

```json
{"error_summary":"…","datetime":"2025-05-27T14:30:00-0700"}
```

`datetime` uses local time from NTP (`MY_TZ` in `wifi_station.ino`). One POST per fault transition (not repeated while the signal stays active).

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
- **wifi_solenoid** — zone 1/2 on/off (`GET /status`, `POST /set_zone` with `zone1` and `zone2`)

## Libraries

Vendored under `lib/`:

- `GxEPD` — e-paper display
- `Adafruit-GFX-Library` — fonts and graphics
- `Logo` — custom bitmap
- `Config` — local settings and secrets (gitignored)
