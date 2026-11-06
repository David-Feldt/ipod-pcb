# E-ink Remote firmware

ESP-IDF application for the rev E board: a pocket Spotify remote that talks to
the Spotify Web API directly over Wi-Fi. The hardware requirements it implements
are in [`FIRMWARE-NOTES.md`](../../FIRMWARE-NOTES.md), and the pin map comes from
[`DESIGN.md`](../../DESIGN.md).

Target: ESP32-S3-MINI-1-N8 (8 MB flash, **no PSRAM**), ESP-IDF 6.0.2.

## Status

| Milestone | State |
|---|---|
| 1. Skeleton, power/charger policy, buttons, wheel, settings, USB console | Done, running on board 1 |
| 2. SSD1680 e-paper driver, LVGL, now-playing layout (mock data) | Done, on board 1 |
| 3. Wi-Fi, SNTP, Spotify token refresh | Next |
| 4. Now playing, transport controls, wheel volume | Planned |
| 5. Device picker | Planned |
| 6. Pairing: setup hotspot, on-device setup page, static redirect page | Planned |
| 7. Low battery → deep sleep, wake policy | Planned |

## Build and flash

```sh
cd firmware/remote
. ./env.sh                      # selects ESP-IDF 6.0.2 and its Python env
idf.py build
idf.py -p /dev/cu.usbmodem101 flash monitor
```

`env.sh` assumes ESP-IDF in `~/esp/v6.0.2/esp-idf` and its Python environment in
`~/.espressif/python_env/idf6.0_py3.14_env`; edit it if yours differ. Flashing
uses the native USB port. If the app is broken, hold BOOT, tap RESET and release
BOOT to enter ROM download mode.

The milestone 2 build shows mock tracks: PLAY toggles play/pause, PREV/NEXT change
track (full refresh), the wheel shows a volume overlay, holding SEL forces a full refresh.
Measured on board 1: full refresh 2.27 s, partial 0.46 s; a full clearing refresh
follows every 5 partials.

Project settings (Spotify client ID, provisional low-battery threshold, refresh
cadence) are under **E-ink Remote** in `idf.py menuconfig`.

## Console

The serial monitor gives a `remote>` prompt:

| Command | Purpose |
|---|---|
| `status` | Firmware, reset reason, USB/charger mode, battery, wheel, buttons, heap, settings |
| `calib wheel` | Sweep the wheel end to end within 8 s to store its end points |
| `charger usb100\|usb500\|suspend\|auto` | Override the charger mode (auto re-applies within 2 s) |
| `wifi set <ssid> <password>` / `wifi clear` | Store Wi-Fi credentials |
| `token set <refresh_token>` / `token clear` | Store a Spotify refresh token (development) |
| `erase yes` | Wipe all stored settings |
| `display calib` / `display done` | Show / leave the screen-fit pattern (frames every 3 px; thick = 15 px) |
| `display inset <l> <r> <t> <b>` | Save pixels hidden on each edge and restart |
| `display orient native\|rot180\|flipv\|fliph` | Save how the image maps onto the glass (default `rot180`) |
| `display stats` / `display full` | Refresh counters and timing / force a full refresh |
| `restart` | Reboot |

## Layout

| Path | Contents |
|---|---|
| `main/` | Boot sequence and app loop; `Kconfig.projbuild` project options |
| `components/board/` | Pin map, charger/USB/battery (`power.c`), buttons and wheel (`input.c`) |
| `components/settings/` | NVS-backed settings |
| `components/epd/` | SSD1680/GDEY0213B74 driver: full/partial refresh, bounded BUSY waits, orientation |
| `components/ui/` | LVGL 1-bit port, refresh scheduler, now-playing screen, calibration pattern |
| `components/app_console/` | USB console commands |
| `partitions.csv` | NVS plus two 3 MB OTA slots |
