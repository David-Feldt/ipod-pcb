# E-ink Remote (ipod-pcb)

A pocket **Spotify remote**: ESP32-S3-MINI-1-N8 (8 MB flash, **no PSRAM**),
2.13" GDEY0213B74 / SSD1680 e-paper (122×250 portrait), five buttons, an absolute
volume thumbwheel, USB-C and a 1S LiPo on a 42×98 mm rev E board. Playback happens
on another Spotify device; this board has no audio hardware.

Repo: https://github.com/David-Feldt/ipod-pcb (private), work on `main`.

## Where things are

| Topic | Source of truth |
|---|---|
| GPIO map, power architecture | `DESIGN.md` (pins mirrored in `firmware/remote/components/board/include/pins.h`) |
| Firmware hardware requirements (charger, low battery, display, wheel) | `FIRMWARE-NOTES.md` — treat as requirements |
| Bring-up procedure and **bench log** | `BRINGUP.md` |
| Debug links/pads | `DEBUG.md` |
| Firmware build, console, layout, milestone status | `firmware/remote/README.md` |
| Hardware review evidence, datasheets | `review/` |

## Firmware (`firmware/remote/`, ESP-IDF 6.0.2, C)

Build: `cd firmware/remote && . ./env.sh && idf.py build` — `env.sh` is required
(it selects the right Python env). Flash: `idf.py -p /dev/cu.usbmodem101 flash`.
Board 1 is normally on USB at `/dev/cu.usbmodem101`; drive it through the `remote>`
USB console (pyserial from the IDF env works for scripted checks).

Decisions made with the user (2026-11-05):
- Spotify Web API called **directly from the device**: PKCE, refresh token in NVS
  (rotates; persist every new one). No runtime server. Premium required; Spotify
  dev-mode app with an allowlist (owner + a few users).
- **Pairing:** SoftAP captive portal for Wi-Fi → device-hosted setup page on the LAN
  → static HTTPS bounce page (`pairing/`, to be hosted on Cloudflare Pages since the
  repo is private) forwards the OAuth code to `http://<device-ip>/callback`. Plus a
  dev path: `tools/spotify_token.py` (127.0.0.1 redirect) → console `token set`.
- **MVP scope:** now playing + play/pause/prev/next + wheel volume + device picker.
  Album art is wanted (110 px area reserved on screen), after Spotify works.
- Own SSD1680 driver (GxEPD2 is reference only); LVGL 9.3 native 1-bit.
- Wheel volume: absolute "jump" policy — publish only after >3% deliberate movement.
- USB present → USB500 early at boot (accepted prototype policy, not negotiated).

Board 1 calibration (saved in NVS): orientation `rot180`, insets 2 px each edge.
Measured: full refresh 2.27 s, partial 0.46 s. User does not want the device name on
the now-playing screen.

### Milestones
1. Board layer, power/charger policy, input, settings, console — **done**
2. E-paper driver, LVGL, now-playing layout with mock data — **done**
3. Wi-Fi, SNTP, Spotify auth/token refresh — **done** (board 1 reads live playback)
4. Real now playing, transport controls, wheel volume (then album art) — **next**
5. Device picker
6. Pairing flow
7. Low battery → deep sleep, wake policy

Spotify app: Client ID `fb6818b0834e4e418b6ad5ef191383ee` (Kconfig default; not
secret), Web API only, redirect URI `http://127.0.0.1:8888/callback`. Board 1 has
Wi-Fi and a refresh token in NVS. To drive the console from scripts, the port must
be free: the user sometimes has `idf.py monitor` open.

Open bench items: meter check of battery voltage (firmware now reads 4.15 V
charged; earlier 3.5 V was a flat pack) and `calib wheel` (top end reads 2640 mV).

## Hardware workflow

`./build.sh` regenerates schematic/PCB/fab outputs (KiCad 10). `./check-order.sh`
is the pre-order gate. `fab/manifest-sha256.txt` hashes release docs including
`README.md`, `BRINGUP.md`, `DESIGN.md`, `DEBUG.md` and `gen/*.py`: **after editing any
of them, regenerate the manifest** (`gen/release_checks.py:manifest_text`) and rerun
`./check-order.sh`, or the gate fails with "Stale release file".

## Conventions

- Commit messages: imperative summary line plus a wrapped body explaining what and
  why; group unrelated changes into separate commits. Keep `build/`, `sdkconfig`
  and `managed_components/` out of git; `dependencies.lock` is committed.
- Record bench results in the `BRINGUP.md` bench log with the firmware revision.
- Throwaway experiments (e.g. quick Arduino sketches) go in the scratchpad, not the repo.
