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
| 2. SSD1680 e-paper driver + LVGL | Next |
| 3. Wi-Fi, SNTP, Spotify token refresh | Planned |
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
| `restart` | Reboot |

## Layout

| Path | Contents |
|---|---|
| `main/` | Boot sequence and app loop; `Kconfig.projbuild` project options |
| `components/board/` | Pin map, charger/USB/battery (`power.c`), buttons and wheel (`input.c`) |
| `components/settings/` | NVS-backed settings |
| `components/app_console/` | USB console commands |
| `partitions.csv` | NVS plus two 3 MB OTA slots |
