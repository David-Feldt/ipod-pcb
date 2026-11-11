# Revision E prototype acceptance

Passing ERC/DRC does not prove an assembled board works. Validate one assembly before a larger batch.

1. Confirm R23/R24/R25 are fitted and J3/J5 are unpopulated. Inspect U4/U5 pin 1, J4 orientation, Q2–Q5, USB-C, LED polarity, wheel seating
   and the filled/capped thermal pads. Compare `fab/assembly-pin1.csv` and drawings.
2. With no power, check for hard shorts on VBUS, VBAT_RAW, VBAT, VSYS, REG_3V3,
   3V3 and EPD_BOOST_IN, and that each of R23/R24/R25 reads near 0 ohms across its
   pads. J2 pin 1 is positive, pin 2 is ground. Check the actual battery lead with a meter before
   plugging it in; JST housings alone do not establish polarity.
3. Start with a current-limited bench source at J2, 3.8 V and 100 mA, USB absent,
   switch OFF. Verify no sustained 3V3 output. Increase the current limit to 1 A
   for boot/load tests, turn ON, and verify regulated 3.3 V before fitting the panel.
4. Sweep the battery source from 4.2 to 3.0 V while applying realistic 3V3 load
   bursts up to 400 mA. Scope the rail for droop/oscillation and check temperatures.
   Verify switch OFF disconnects the rail. Actual low-battery shutdown belongs in
   firmware; a protected battery is still required to prevent deep discharge.
5. Use USB with a protected battery attached. At GPIO6/7 low, input must stay at
   or below the 100 mA mode limit. After valid USB configuration and a 500 mA
   allocation, EN1 high/EN2 low permits USB500. Charge current is about 223 mA
   only when enough input budget remains. Both high select suspend. Never charge
   a battery using a bench supply as a substitute cell that cannot sink current.
6. Test USB flashing in both cable orientations. Hold BOOT, tap RESET, release
   BOOT for ROM recovery. Native USB is GPIO19/20; UART fallback is TP1/TP2 with
   TP3 ground, using 3.3 V logic only. TP1/TX is the board's TXD0 (connect to
   adapter RX); TP2/RX is the board's RXD0 (connect to adapter TX). TP4/3V3 is a
   power rail, not a UART signal; leave the adapter's power output disconnected
   when the board is powered from USB or battery.
7. Fit the panel with power OFF and pin 1 aligned per `MECHANICAL.md`. Use a
   GDEY0213B74/SSD1680 driver with the pin map in `DESIGN.md`; perform full black,
   white and checkerboard refreshes. Check BUSY returns and boost voltages match
   the panel specification. Do not probe the boost nodes with the battery loose.
8. Configure button pull-ups. Read GPIO1 after 250 ms, convert its calibrated
   voltage ×2, and compare with a meter. For the wheel, set GPIO5 high, wait 1 ms,
   sample GPIO4 with maximum ADC attenuation, then set GPIO5 low. Calibrate end
   points. Application firmware should publish only after actual wheel movement.
9. Exercise Wi-Fi and screen refresh together near low battery; measure charging
   temperature, sleep/off current and radio performance with the intended case.

Record measured results, instrument conditions and firmware revision in the bench
log below. Cell temperature sensing is absent (fixed TS bias); charge only within
the selected pack's allowed ambient temperature range.

For current measurement, external 3.3 V power, manual UART recovery and the
external display adapter, follow `DEBUG.md`; opening a link changes which
supply/test pad is connected to each circuit.

## Bench log

### Board 1, 2026-11-04 (firmware `firmware/bringup/bringup.ino`, esp32 core 3.2.0)

| Step | Result |
|---|---|
| 1. Visual | No issues reported |
| 2. Unpowered shorts | No shorts on VBUS, VBAT_RAW, VBAT, VSYS, REG_3V3, 3V3, EPD_BOOST_IN. TP11 (PWR_EN) reads 0 ohm to GND with SW8 OFF (by design); open with SW8 ON |
| 3. First power | Done over USB in USB100 instead of the bench supply at J2; no battery, no panel. 3V3 not yet measured with a meter |
| 6. USB | Enumerates as Espressif USB-Serial/JTAG (303a:1001). esptool: ESP32-S3 (QFN56) v0.2, 8 MB flash, 40 MHz crystal. Flashed over native USB. RESET recovers cleanly. Cable-flip and BOOT/ROM recovery not yet tested |
| 5. Charger pins | EN1/EN2 read USB100 with firmware leaving them as inputs. USB_PRESENT_N low with USB connected |
| 8. Sensing | VBAT_SENSE reads 4.19 V with no battery (charger holds BAT near regulation); meter comparison pending with a battery fitted |
| 8. Wheel | 0 to 2735 mV end to end (expected about 2750 mV), stable to ±2 mV |
| 8. Buttons | MENU/PLAY/PREV/NEXT/SEL register with clean edges; none stuck at boot. BOOT not yet tested |
| Thermal | ESP32 internal temperature 22–24 C, Wi-Fi off |
| Battery | Protected 1000 mAh pack on J2, polarity correct (VBAT_SENSE 3.92 V vs 4.19 V with no battery). D5 lights while charging on USB100. Unplugging USB with SW8 ON: no reboot (button counters retained). TP4/TP5 meter readings pending |
| Button map | Centre button reports SEL as designed |
| 7. Display | GDEY0213B74 on J4 with `firmware/display_test` (GxEPD2 1.6.9, 4 MHz SPI). White/black/white/checker/orientation all complete; BUSY 1.743–1.744 s per full refresh, first refresh 3.6 s including library initial clear. vbat 3.93 V steady, USB connected (not a droop test). Visual quality/orientation: user reports working |

### Board 1, 2026-11-07 (firmware `firmware/remote` 0.3.0-m3, ESP-IDF 6.0.2)

| Step | Result |
|---|---|
| Battery | VBAT_SENSE 4146–4150 mV on USB, charger USB500 (the 3.51–3.60 V seen on 2026-11-05 was a discharged pack). Meter comparison still pending |
| Wheel | Top end 2639–2642 mV with saved calibration 20..2715 mV (97%) |
| Wi-Fi | Joins a 2.4 GHz WPA2 network, DHCP address, RSSI −71 dBm; SNTP sets the clock. Free heap 188 KB with Wi-Fi up (min 178 KB) |
| Spotify | `tools/spotify_token.py` PKCE login → token sent to the board. `spotify now`: token refresh then `GET /v1/me/player` HTTP 200, 1.75 s for both TLS handshakes; shows track, device and volume correctly |

### Board 1, 2026-11-07 (firmware `firmware/remote` 0.4.0-m4)

| Step | Result |
|---|---|
| Live playback | Track, artist, album and progress follow Spotify on a Google Nest Hub; automatic track changes appear within one poll |
| Controls | Play/pause (centre), next, previous/restart and wheel volume work (user check). Spotify acknowledges next in 0.4 s, pause/play in 0.26–0.35 s; a new track is on screen about 3.5 s after NEXT, including the 2.27 s full refresh |

### Board 1, 2026-11-08 (firmware `firmware/remote` 0.4.1, 240 MHz)

| Step | Result |
|---|---|
| Album art | Covers of 7–63 KB decode correctly; user reports the dithered art looks good. First cover after boot 2.0 s TLS connect (2.9 s at 160 MHz); later covers 72–80 ms connect with session resumption, 0.1–0.3 s download and decode. Track text and cover arrive in one full refresh. Min free heap 60 KB over 8 track changes |

### Board 1, 2026-11-09 (firmware `firmware/remote` 0.5.0, milestone 6 part 1)

| Step | Result |
|---|---|
| Pairing (LAN) | `pair start` serves the setup page on port 80; `/login` redirects to Spotify, which accepts the request (303 to its login page); a forged `/callback` is refused. End to end with the local bounce page (`tools/pairing_bounce.py`, redirect `http://127.0.0.1:8888/callback`) from a browser on the same Mac: code exchanged on the board, refresh token saved, playback resumes, setup page stops by itself. Min free heap 105 KB through the TLS exchange (setup server 117 KB idle). From a phone it fails until the bounce page is hosted on HTTPS (expected) |
| Pairing (phone) | Redirect URI `https://david-feldt.github.io/eink-remote-pair/` (GitHub Pages). QR code scanned on a phone on the same Wi-Fi: Spotify login → bounce page → board; refresh token saved about 4 s after `/login`, setup page stopped 5 s later |

### Board 1, 2026-11-10 (firmware `firmware/remote` 0.6.0, milestone 6 part 2; tested on the same code just before the version bump)

| Step | Result |
|---|---|
| Wi-Fi setup hotspot | `wifi setup`: hotspot "E-ink Remote AA8D" (WPA2) up, 3 networks found by the pre-join scan, free heap 117 KB. Phone joined via the screen QR code, setup page opened, chose the home network: joined in 1.6 s on the first attempt, saved, hotspot stopped 12 s later and playback resumed (one failed poll on the connection left over from before the hotspot, then normal). Wrong-password path not yet exercised on hardware |

### Board 1, 2026-11-10 (firmware `firmware/remote` 0.7.0, milestone 7 minimal)

| Step | Result |
|---|---|
| Low-battery shutdown | Triggered with `sleep yes` (USB present, 4146 mV): Wi-Fi stopped, one full refresh (2270 ms) to the "Battery low" screen (user confirmed), charger USB500 → USB100, deep sleep with wake mask 0x3e00 (MENU/PLAY/PREV/NEXT/SEL; USB left out because it was present) |
| Button wake | SEL woke the board: reset reason `deep-sleep wake`, USB present so it resumed normally (Wi-Fi and Spotify back) |
| Not yet tested | Automatic trigger at the provisional 3450 mV threshold, wake on USB insertion, sleep-again on a low-battery button wake, whole-board sleep current |
