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
| 3. Wi-Fi, SNTP, Spotify token refresh | Done, on board 1 |
| 4. Now playing, transport controls, wheel volume, album art | Done, on board 1 |
| 5. Device picker | Dropped for now (user doesn't need it) |
| 6. Pairing: setup hotspot, on-device setup page, static redirect page | In progress: Spotify sign-in works on board 1 (local bounce page); HTTPS hosting and hotspot to do |
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

The screen shows live Spotify playback, polled every 2 s while playing and
every 10 s otherwise; the progress bar advances locally in between.

| Control | Action |
|---|---|
| Centre (SEL) | Play / pause |
| NEXT | Next track |
| PREV | Restart the track; previous track if within its first 3 s |
| Wheel | Volume of the playing device (after >3% deliberate movement) |
| Bottom (PLAY), hold | Force a full e-paper refresh |
| MENU (top) | Open the menu; cancels the sign-in screen |

Album art: the 300 px cover is decoded straight from the download with the
ESP32-S3 ROM JPEG decoder, scaled to the 110 px box, contrast-stretched and
Atkinson-dithered to black and white. A new track waits up to 2.5 s for its
cover so text and art land in one full refresh. The image host keeps a TLS
session ticket, so after the first cover (about 2 s of handshake) each one
takes about 0.3-0.5 s. The CPU runs at 240 MHz because TLS handshakes are
CPU-bound (no ECC accelerator); revisit for battery life in milestone 7.

Presses update the screen before Spotify confirms; a failed command shows a
short notice (for example "Volume not supported" on devices that refuse it).
Text is shown in ASCII: accents are stripped and unsupported scripts show "?".

Measured on board 1: full refresh 2.27 s, partial 0.46 s; a full clearing refresh
follows every 5 partials.

## Spotify sign-in (pairing)

Press MENU and choose **Sign in to Spotify** (or run `pair start`). In the
menu, MENU and PLAY (top and bottom of the cross) move up and down, the centre
button chooses and PREV goes back; it closes by itself after 20 s. The screen
shows a QR code and the board's address; open it on a phone or computer on the same Wi-Fi and press **Sign in
with Spotify**. The board also offers this screen by itself once per boot
when it has Wi-Fi but no working login. Pressing MENU cancels, and the
setup page shuts down after success or 10 idle minutes.

How it works: the board serves the setup page only while pairing is active.
`/login` starts a PKCE authorization (the verifier stays on the board) and
sends the browser to Spotify. Spotify only redirects to HTTPS or loopback
URLs, so it returns to the static bounce page [`pairing/index.html`](../../pairing/),
which forwards the code to `http://<board-ip>/callback` (the IP travels in
`state`). The board exchanges the code and saves the refresh token.

The redirect URI is `CONFIG_REMOTE_PAIRING_REDIRECT_URI` (menuconfig), by
default the GitHub Pages copy at `https://david-feldt.github.io/eink-remote-pair/` (see
`pairing/README.md`). Set it empty to use `http://127.0.0.1:8888/callback`
with `python3 tools/pairing_bounce.py`, from a browser on that same computer.

## First-time setup (development path)

Without the pairing hotspot (still to come in milestone 6), give the board
Wi-Fi from the console. A Spotify login can come from pairing (above) or
from your computer:

```sh
# in the console (idf.py -p /dev/cu.usbmodem101 monitor), 2.4 GHz network only
wifi set "YourSSID" "YourPassword"
# then close the monitor (Ctrl+]) and, from the repo root:
. firmware/remote/env.sh && python tools/spotify_token.py --device /dev/cu.usbmodem101
```

`spotify_token.py` opens the Spotify login in your browser and writes the
refresh token straight to the board (without `--device` it prints a
`token set ...` line to paste instead). The Spotify app must list
`http://127.0.0.1:8888/callback` as a redirect URI and your account under User
Management. Check with `spotify now` while something is playing.

Project settings (Spotify client ID, provisional low-battery threshold, refresh
cadence) are under **E-ink Remote** in `idf.py menuconfig`.

## Console

The serial monitor gives a `remote>` prompt:

| Command | Purpose |
|---|---|
| `status` | Firmware, reset reason, USB/charger mode, battery, wheel, buttons, heap, Wi-Fi, clock, Spotify sign-in, settings |
| `calib wheel` | Sweep the wheel end to end within 8 s to store its end points |
| `charger usb100\|usb500\|suspend\|auto` | Override the charger mode (auto re-applies within 2 s) |
| `wifi set <ssid> <password>` / `wifi clear` | Store Wi-Fi credentials and (re)connect |
| `token set <refresh_token>` / `token clear` | Store a Spotify refresh token (development) |
| `pair start` / `pair stop` / `pair status` | Spotify sign-in page on the LAN (same as the menu item) |
| `spotify refresh` | Get a new access token now (saves a rotated refresh token) |
| `spotify now` / `spotify devices` | Show current playback / list Spotify Connect devices |
| `spotify next` / `prev` / `play` | Same as the buttons (for testing without touching the board) |
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
| `components/ui/` | LVGL 1-bit port, refresh scheduler, now-playing, menu and sign-in screens, calibration pattern |
| `components/net/` | Wi-Fi station with reconnect backoff, SNTP |
| `components/player/` | Playback poller and command runner (play/pause/next/previous/seek/volume) |
| `components/art/` | Album art: streaming JPEG decode (ROM TJpgDec), scale, dither to 1-bit |
| `components/pairing/` | Spotify sign-in: setup page on port 80 while pairing, PKCE, code exchange via `spotify_exchange_code` |
| `components/spotify/` | Token refresh (rotating refresh token saved to NVS), authenticated Web API requests over one kept-alive TLS connection |
| `components/app_console/` | USB console commands |
| `../../tools/spotify_token.py` | PKCE login helper that gets a refresh token for the board |
| `partitions.csv` | NVS plus two 3 MB OTA slots |
