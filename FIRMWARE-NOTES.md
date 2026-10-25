# Firmware requirements from the hardware review

Living notes for revision E. Update this file as review decisions are made.
These are requirements and open implementation tasks, not implemented firmware or
bench-verified behavior. Hardware pin assignments come from DESIGN.md and
gen/build_sch.py; test procedures are in BRINGUP.md and DEBUG.md.

## Accepted operating behavior

- When ON, this is an active device: expect sustained CPU/Wi-Fi use and overlapping
  display refresh. Do not rely on deep sleep to make the normal power budget work.
- Normal intended use is indoors, usually connected to USB, with battery backup.
- Selected battery: Canada Robotix SKU 0588, 3.7 V nominal, 1000 mAh. The supplier
  lists overvoltage, overcurrent and undervoltage protection. Its protection trip
  and release thresholds have not been confirmed. The cell datasheet's 2.75 V
  discharge limit is not evidence of the pack's actual disconnect threshold.
- User accepts firmware low-battery detection followed by deep sleep, with pack
  protection as backup. This is not a physical battery disconnect.
- User wants charging with SW8 OFF and accepts slow USB100 charging in that state.
- User elects to retain fixed TS bias and assumes the battery remains within its
  allowed charging range (0–45 C). There is no battery-temperature measurement;
  firmware cannot implement a battery-temperature cutoff with the present inputs.

## Charger control and USB power

GPIO6 drives EN1; GPIO7 drives EN2. Table order below is EN1 then EN2;
TI's datasheet Table 7-2 lists EN2 first.

| GPIO6 / EN1 | GPIO7 / EN2 | Mode |
|---|---|---|
| LOW | LOW | USB100, 100 mA maximum charger input; hardware reset default |
| HIGH | LOW | USB500, 500 mA maximum charger input |
| LOW | HIGH | Resistor-programmed input limit; unused in the agreed normal sequence |
| HIGH | HIGH | USB suspend |

- Start conservatively in USB100. Configure both outputs without unintentionally
  selecting a higher-current mode during initialization.
- Accepted prototype policy: select USB500 early after boot when USB is present
  (GPIO6 HIGH, GPIO7 LOW), assuming a 5 V source capable of at least 500 mA.
  This deliberately replaces the earlier wait-for-source-permission requirement.
  It is not universal USB power compliance; test the intended computer ports and
  adapters. Source presence or successful boot is not evidence of permission.
- USB100 and USB500 input limits are specified as 90–100 mA and 450–500 mA,
  respectively. Budget using the lower limits, allowing for supply conditions.
- Those limits are at the charger input, shared by system load and charging;
  they are not output-current ratings for the 3.3 V regulator.
- Handle USB suspend, reset/deconfiguration, detach and reconnect explicitly.
  Retain the planned USB suspend behavior. Document how the prototype USB500
  assumption is reapplied after reconnect; do not describe it as negotiated permission.
- GPIO2 is USB_PRESENT_N: LOW indicates VBUS detection. Disable internal pulls;
  R8 provides the external pull-up. This signal does not establish current
  capability, successful USB configuration, or acceptable power under load.
- Automatic wall-adapter/USB-C current identification is not included in this
  prototype. No detector chips or USB-triggered automatic power-on are planned.
  With SW8 OFF the existing pull-downs retain USB100 charging.
- Delay heavy startup loads until available power is assessed. Test startup with
  a healthy battery, low battery, pack protection open, and battery absent.
- If source power is inadequate during sustained operation, battery supplementation
  can drain the pack even while plugged in. Verify net battery current under the
  intended workload; a larger regulator alone cannot solve an input-power deficit.
- Fast-charge current is set by hardware at about 223 mA nominal (R3 + R14 = 4 kohm).
  Firmware does not set it through EN1/EN2. The charger reduces charging as needed
  for input budget and its internal control limits.

## Low-battery detection and sleep

Accepted mechanism: calibrated ADC detection -> stop heavy loads -> deep sleep;
pack undervoltage protection is the final backup.

1. Read VBAT_SENSE on GPIO1. Allow at least 250 ms after power-on for divider/filter
   settling. Convert calibrated ADC voltage to battery voltage by multiplying by 2;
   validate the result against a meter and account for divider tolerances.
2. Choose the low-battery threshold from pack data, voltage sag under the active
   workload, regulator margin and measurements. No numeric threshold is approved.
   Do not wait for brownout or use the pack protection trip as the normal shutdown.
3. Define filtering/debounce to reject brief load sag, plus a sustained-critical
   path that responds before the supply collapses. Durations remain to be chosen.
4. Stop Wi-Fi and display refresh before deep sleep; set VOL_EN LOW. A final
   low-battery screen is optional and must not require a refresh when insufficient
   energy remains. Put peripherals in their documented low-power states.
5. Design and test GPIO6/7 states during deep sleep. Do not assume levels persist:
   use supported retention or deliberately accept the pull-down default. Preserve
   any applicable USB suspend/current obligations; avoid a floating intermediate
   state that selects an unintended mode.
6. Define wake sources and recovery hysteresis before implementation. USB insertion
   and user buttons are candidates; timer wake is optional. Reject wake attempts
   that would immediately restart heavy loads with inadequate battery/source power.
   Check held buttons and voltage rebound to prevent repeated wake/brownout loops.
7. Measure the complete board's sleep current, including regulator, sensing divider,
   charger and display. Deep sleep leaves the 3.3 V regulator enabled and consumes
   nonzero power, so it does not preserve the battery indefinitely.

USB-present behavior needs an explicit policy: a low battery can charge while the
system runs if USB has sufficient budget; USB presence alone does not prove this.
On wake, reassess both battery voltage and valid source capability before normal use.

## Switch and charge indicator

- SW8 directly drives U5's EN pin, not an ESP32 GPIO. Firmware cannot command a
  true regulator shutdown with the current circuit and receives no dedicated
  advance warning before the user switches OFF. Avoid relying on last-moment saves.
- OFF disables the normal 3.3 V supply; the charger remains connected to USB/battery.
  GPIO pull-downs select USB100 with the MCU unpowered. Off-state leakage remains.
- D5 is a hardware charge LED via R4 and the charger CHG output. No LED firmware is
  needed. ON means charging; OFF is ambiguous (complete, disabled or otherwise
  inactive). Do not infer a precise battery percentage from this LED.
- E-paper retains its image when power is removed; a visible image is not proof the
  device is currently powered. No separate power-on LED is fitted.

## Existing peripheral notes to preserve

- Native USB uses GPIO19/20; BOOT/RESET support ROM recovery. UART fallback uses
  GPIO43/44 via the documented test pads; use 3.3 V logic.
- Display: GDEY0213B74 / SSD1680. Confirm the installed GxEPD2 driver and use the
  DESIGN.md GPIO map. Include BUSY timeouts and recovery during bring-up.
- Wheel: GPIO5 VOL_EN HIGH, wait approximately 1 ms, read GPIO4 with suitable ADC
  attenuation/calibration, then drive GPIO5 LOW. Calibrate endpoints and publish
  volume changes only after wheel movement.
- Buttons use pull-ups; configure and test RTC pull-ups/wake behavior for sleep.

## Open decisions and acceptance evidence

- [ ] USB stack/configuration, current permission, suspend and reconnect handling.
- [ ] Validate and document the accepted at-least-500 mA source assumption on the
      intended adapters/computer ports; automatic detection is outside prototype scope.
- [ ] ADC calibration; low-battery threshold, filtering and recovery hysteresis.
- [ ] Deep-sleep GPIO retention, wake policy, peripheral shutdown and measured current.
- [ ] Pack protection trip/recovery data and recovery after connecting USB.
- [ ] Sustained CPU/Wi-Fi plus display-refresh power test across the intended battery range.
- [ ] Startup with low/absent battery, USB100/USB500, and weak supply conditions.
- [ ] No unintended reset loops, battery drain during intended plugged-in use, or
      repeated low-battery wakeups. Record waveforms/current and firmware version.

Regulator current margin remains under review. The existing 400 mA burst exercise
in BRINGUP.md is not by itself acceptance for the newly clarified active-load
requirement; expand the test load after establishing the combined demand and margin.

## References

- [Hardware design and GPIO map](DESIGN.md)
- [Prototype bring-up](BRINGUP.md)
- [Review discussion log](review/REVIEW.md)
- [TI BQ24074 datasheet](https://www.ti.com/lit/ds/symlink/bq24074.pdf)
- [TI TPS63031 datasheet](https://www.ti.com/lit/ds/symlink/tps63031.pdf)
- [Selected battery listing](https://www.canadarobotix.com/products/588)

## Brownout discussion and provisional load estimate

- Keep the ESP32 brownout detector enabled; choose/check its configuration against
  the module/flash operating requirements. Brownout normally causes reset rather
  than permanent hardware damage, but repeated restarts and interrupted writes
  are not acceptable normal low-battery behavior.
- Reject/defer OTA and nonessential flash writes when available power is inadequate.
  Use transactional/power-loss-tolerant storage and OTA rollback/recovery; these
  reduce risk rather than guaranteeing every write survives arbitrary power loss.
- Detect low battery before rail collapse, stop heavy loads, and avoid reboot loops
  as already planned. Test power interruption during data writes and recovery.
- Historical option, not selected: a voltage supervisor such as the TI
  TPS3808 family monitoring +3V3, with active-low open-drain reset driving ESP32 EN.
  Exact threshold/tolerance, assertion response, release delay and interaction with
  existing EN pull-up/capacitor require design. No part or circuit change approved.
  This monitors the MCU rail; it neither disconnects the battery nor fixes inadequate
  regulator capacity. Reset load release can allow voltage recovery and repeated
  restarts; reset delay alone is not a latched low-battery shutdown.

Provisional sizing arithmetic (not a worst-case qualification):
- ESP32 module Table 6-4: listed TX figure 355 mA under its specified test conditions.
- Panel datasheet p.10: 3.5 mA typical at 3.0 V, 10.5 mW typical; maximum refresh/
  boost startup current is unspecified. Custom boost implementation must be measured.
- Use an explicitly assumed 50 mA display/boost allowance and 10 mA miscellaneous
  3.3 V allowance for an exploratory estimate: 355 + 50 + 10 = 415 mA. Neither
  allowance is a verified upper bound. Charge LED is on VSYS, not the 3.3 V rail.
- At 3.0 V regulator input and assumed 85% efficiency, this implies roughly
  3.3 * 0.415 / (3.0 * 0.85) = 0.537 A regulator input current.
- A more conservative sizing allocation uses 500 mA for ESP32 supply capability
  plus the same provisional allowances: 560 mA before extra margin. This exceeds
  the TPS63031's advertised up-to-500 mA boost figure, so margin remains unresolved.
- TI's 900 mA minimum average switch limit is stated at VIN=3.6 V and 25 C; do not
  extrapolate it into a guaranteed low-voltage output rating.

Sources: saved review/esp32-s3-mini-1.pdf p.28, review/gdey0213b74.pdf p.10,
review/tps63031-ti.pdf pp.1 and 5; https://www.ti.com/product/TPS3808 ;
https://docs.espressif.com/projects/esp-idf/en/v5.2/esp32s3/api-guides/fatal-errors.html .

## Decision: retain simple hardware (2026-10-20)

User elects not to add an external voltage supervisor or automatic hardware
low-battery cutoff. Keep the present circuit. Implement calibrated GPIO1 battery
monitoring, a measured low-battery threshold, filtering/recovery hysteresis,
controlled peripheral shutdown and deep sleep. Keep the ESP32 brownout detector
enabled as backup and use power-loss-tolerant storage/OTA behavior. Pack protection
remains the final discharge safeguard. Thresholds and recovery behavior remain open.

This decision accepts the firmware-based approach; it does not establish regulator
current margin or approve brownout during normal operation. Verify sustained active
loads and transients across the intended operating voltage range.

## Prototype regulator decision (2026-10-20)
Retain U5 for the prototype as requested. During bring-up, validate sustained
CPU/Wi-Fi plus display activity, load steps, and voltage droop at the ESP32 pins
across the intended battery range. Use those results to finalize the low-battery
threshold and recovery policy; do not treat the 415 mA estimate as a tested maximum.
The choice to proceed does not close unverified component-rating or load-margin
checks. USB power-source identification remains open for the next review.

## Earlier USB source requirement discussion (superseded by decision below)
User may connect either a computer USB port or a USB wall charger, expecting normal
5 V input. Support for both is required; this is not an assumption of a dedicated
known-current adapter. Source voltage alone does not establish available current.
Computer-host operation can use an accepted USB configuration to select USB500.
For wall adapters, automatic capability detection remains unresolved on the current
hardware. Do not select USB500 solely because VBUS is present or enumeration times
out. Retaining USB100 is conservative but may not sustain the active workload;
verify source handling before claiming equivalent operation from both source types.
No USB-PD higher-voltage request is planned. No new hardware or manual adapter
override was selected in this discussion.

## Current decision: no USB hardware changes (2026-10-20)
Retain charger and switch hardware. Select USB500 early after boot on USB power
under the explicit adequate-source assumption; retain USB100 charging while OFF.
Do not add automatic source detection or USB auto-power-on for this prototype.
This supersedes earlier notes requiring capability detection before every USB500
selection. There is no claim of universal USB-port compatibility. Firmware is
still unimplemented; intended ports, suspend/reconnect and low-battery startup
remain test requirements.

## ESP32 boot and recovery review (2026-10-20)
Actual PCB nets checked: EN pad 45 uses R10 10 kΩ/C8 1 µF and RESET to GND;
BOOT grounds GPIO0 pad 4. GPIO45/46 are unconnected with internal pull-down
defaults. GPIO3 is unused; its strap is ignored with default
EFUSE_STRAP_JTAG_SEL=0. Do not enable GPIO3 JTAG strap selection without defining
its hardware level. No circuit change indicated for the prototype.

Preserve USB/UART ROM download access during development. Do not burn eFuses that
disable recovery/debug or change flash voltage without a separately reviewed
provisioning plan. Configure the exact ESP32-S3-MINI-1-N8: 8 MB flash, no PSRAM.
Do not enable PSRAM based on examples for the N4R2 module. Avoid reusing USB19/20
for other functions. The native USB interface is not a fitted USB-to-UART bridge.

Manual ROM entry: hold BOOT, tap/release RESET, hold BOOT briefly after RESET
release (strap hold requirement is 3 ms), then release BOOT. Verify USB flashing
and 3.3 V logic UART fallback through TP1/TX, TP2/RX and TP3/GND or manual J3.
Never assume J3 matches an off-the-shelf FTDI cable pinout. Leave adapter power
unconnected when the board has its own supply. J3 is DNP; hand-fit if needed.

Bench checks: repeated cold boot and rapid power cycles, EN timing relative to
3.3 V, download mode and reflash even with invalid application firmware, and UART
fallback. RC timing is an appropriate starting point, not a power-ramp guarantee.

## USB interface bring-up checks (2026-10-20)
Test enumeration, repeated flashing/reconnect with both plug orientations and
intended USB-A–C / USB-C–C cables and sources. Exercise suspend/resume and
power-switch OFF/ON without leaving the charger in an unintended mode. Retain
the agreed USB500-on-boot source assumption and USB100 OFF behavior. Measure
VBUS plug-in droop/inrush: firmware cannot limit the initial charging of C1/C40
directly across VBUS. Proposed C1 reduction remains an open hardware review
action, not an implemented change. See review/04-usb-review.html.

## Display sequence and refresh review (2026-10-20)
Reference: review/gdey0213b74.pdf pp.8–9, 29, 31; evidence page
review/05-display-review.html. BS1 is grounded for 4-wire SPI. GPIO16 SCK,
17 MOSI, 18 CS, 21 DC, 47 RESET and 48 BUSY. Honor the panel-specific reset,
initialization, waveform and power sequence. During active operations BUSY HIGH
means busy; use bounded waits and controlled recovery, not unbounded loops.
Complete refresh before sleep; wake the panel from deep sleep with hardware
reset. Panel sleep does not disconnect its shared 3.3 V supply.
Refresh policy is not yet decided: suggested event-driven changes, coalescing
wheel movement, partial refresh where supported and periodic full refresh to
clear ghosting. Exact timing/quality must be tested. Do not promise smooth
animation from the panel. Boost substitutions and physical flex fit remain open.

### Accepted display requirement: both modes (2026-10-20)
User wants both event-driven static content and playback progress updates. This
supersedes the undecided policy above. Implement dirty-region partial refresh
where the exact panel/driver supports it, coalesce input changes, and schedule
periodic full refreshes based on measured ghosting. Make progress cadence
configurable; 1 second is an initial bench target, not a guaranteed capability.
Skip progress updates that do not change a displayed pixel, never overlap
refresh commands, and prioritize the latest UI state over a queue of stale frames.
Measure current, latency and image quality with Wi-Fi active. Preserve the
previous-frame state required by the driver; reinitialize/full-refresh after
reset or sleep when required instead of assuming partial state survives.

### USB C1 implementation and display clearing baseline (2026-10-20)
C1 reduction is now applied: 4.7 µF / 25 V, C1779. C40 remains 100 nF. Earlier
notes calling this proposed are superseded; plug-in testing remains pending.
Panel datasheet p.10 recommends a full clearing refresh after five partial/fast
updates. Typical partial/full times at 25 °C are 0.42 s / 3 s. Start from that
clearing policy and count all relevant updates, including controls. A 1-second
progress cadence was only a bench target, not user acceptance of frequent
clearing flashes. Keep cadence configurable and confirm UX with the user.
Deep sleep does not retain panel RAM (p.10); preserve/rebuild frame history and
follow the exact driver requirements before subsequent differential updates.

## Controls/sensing review (2026-10-20)
Actual PCB nets checked for SW1–SW5, RV1, R19/C36, battery divider and Q3–Q5.
Buttons GPIO9–13 are active LOW and require firmware pull-ups/debounce. Verify
RTC wake/pull configuration and avoid immediate re-wake while a button remains held.
Wheel GPIO5 enable / GPIO4 ADC1_CH3: wait about 1 ms after enabling, calibrate
endpoints, filter noise and suppress unchanged values. Nominal upper voltage
2.75 V; 2.864 V with +30% pot/−1% series resistor at exactly 3.3 V. Include
rail variation in bench testing. The wheel is an absolute-position potentiometer;
behavior when phone volume and wheel position differ remains to be specified.
Battery GPIO1 ADC1_CH0: nominal 2:1 divider, 50 ms RC time constant, wait at
least 250 ms at startup and calibrate against a meter. Large source resistance
means leakage and ADC sampling effects must be measured, not assumed negligible.
GPIO2 USB presence is active LOW with external pull-up; disable internal pulls.
Check OFF-state residual sense voltage and 3.3 V rail behavior. Q4/Q5 isolate
the sense divider; they do not disconnect the main battery load.

Display acceptance: user accepts clearing flashes as not ideal; support both
static/event updates and playback progress. Exact cadence is deferred to bring-up.

### Wheel hardware accepted (2026-10-20)
User accepts the existing absolute-position potentiometer with physical end stops.
No encoder or hardware change. Firmware will handle calibration, filtering and
volume synchronization. Preserve remote volume until deliberate wheel movement;
exact jump-versus-pickup policy remains an implementation choice to validate.
Firmware cannot change the wheel's physical end stops.

## Ordered Waveshare panel: identity/compatibility open (2026-10-20)
User ordered https://www.waveshare.com/2.13inch-e-paper.htm (SKU12672, bare
black/white 250x122). Exact shipped revision and manufacturer code unknown.
Do not assume the shipped panel is GDEY0213B74. Saved official V4 spec as
review/waveshare-213-v4.pdf. Page 6 drawing: 59.2x29.2 mm glass, 24x0.5 mm
contacts, 14.3 mm nominal tail projection, SSD1680Z8 controller. Page 7 pin
assignments agree with the designed panel interface. Physical fit not verified.
Page 28 boost reference differs: 68 µH / 3 Ω / 10 kΩ gate pull-down /
4.7 µF 50 V flying cap versus current 47 µH / 2.2 Ω / 1 MΩ / 4.7 µF 25 V.
Page 28 also contradicts page 7 on pins 1/4/5 (HLT_CTL/VGL/VGH versus
NC/NC/VSH2), so do not blindly apply its connector net labels. No compatibility
sign-off or hardware change. Confirm shipped revision and reconcile supplier
reference before selecting driver/waveforms and approving boost components.
Prior Good Display refresh timings remain provisional for the ordered panel.

## Waveshare V3/V4 compatibility clarification (2026-10-20)
User expects either V3 or V4. Waveshare monochrome 2.13-inch manual explicitly
states V4 is fully compatible with V3 and has a fast-refresh demo:
https://www.waveshare.com/wiki/2.13inch_e-Paper_HAT_Manual .
Both official bare-panel pin tables (p.7 in each specification) have the same
24-pin assignments; both mechanical drawings identify SSD1680Z8. V3 versus V4
is not itself evidence that PCB connector wiring must change. Record shipped
revision on arrival and use its supported firmware initialization/waveforms.
The remaining hardware issue is whether our custom boost circuit/substitutions
suit the Waveshare panel; the vendor compatibility statement does not validate
our different boost values. Keep that review open; no hardware change made.


## Display boost Q2 finding (2026-10-20)
Compared actual AO3400A and reference Si1308EDL manufacturer datasheets p.2.
Both are 30 V MOSFETs, but typical Ciss is 630 pF versus 105 pF under matching
15 V VDS / 0 V VGS / 1 MHz conditions. Typical Qg at 4.5 V is 6 nC versus
1.4 nC, with different test drain currents (5.7 A versus 1.4 A). This is a
gate-driver loading concern, not proof the current circuit fails. Recommendation:
use reference Si1308EDL or qualify a comparable low-charge alternative. The
reference uses SC-70/SOT-323 rather than current SOT-23, requiring a footprint
and layout change. No hardware change authorized or made by this finding.
Both Waveshare V3 p.26 and V4 p.28 use the same 68 uH / 3 ohm / 10 kohm /
4.7 uF 50 V reference. Generic circuit labels conflict with p.7 pin tables; do
not change connector nets from that figure. Other boost component checks and
physical fit remain open. See review/05-display-review.html for comparisons.
Firmware: use the delivered Waveshare revision's supported initialization and
waveforms; do not force the original Good Display profile. At bring-up measure
boost startup and refresh rails, Q2 gate-source/drain-source waveforms and input
current with an appropriate probe setup. Firmware cannot establish hardware
substitution equivalence. BUSY timeout recovery remains required.
Sources: https://www.aosmd.com/res/data_sheets/AO3400A.pdf
and https://www.vishay.com/docs/63399/si1308edl.pdf .


## Q2 replacement implemented (2026-10-20)
User approved switching Q2 to reference Vishay Si1308EDL-T1-GE3, LCSC C469327.
Updated gen/build_sch.py, schematic and design metadata, and replaced the PCB
SOT-23 land pattern with stock Package_TO_SOT_SMD:SOT-323_SC-70. Gate/source/
drain remain pins 1/2/3 on EPD_GDR/EPD_RESE/EPD_SW. Adjusted six adjacent
track segments and removed three zero-length autorouter rounding stubs.
Existing protected-route generation follows actual pad coordinates and passes.
Added Q2 part/footprint/LCSC/pin-map checks to verify_board.py. ERC has zero
messages; DRC has zero errors, zero unconnected pads and zero parity issues;
only existing U1/J1 library-footprint warnings remain. Review-check tests (3)
and release-check tests (12) pass. Manufacturing files regenerated.
No firmware pin changes required. Continue using the delivered Waveshare
revision's supported driver; boost startup/refresh measurements and other
component checks remain pending. This supersedes the earlier proposed-only
Q2 status and AO3400A substitution concern, not the rest of the boost review.
Exact catalog entry: https://www.lcsc.com/product-detail/C469327.html .

## Display boost validation requirements (2026-10-20)
See review/05-display-boost-findings.md for the component recommendations;
none are implemented yet. Record delivered panel revision, driver version,
waveform/LUT, gate/source/VCOM settings and boost soft-start configuration for
each test. Use the supported panel profile; do not tune drive voltages to hide
a component-selection problem.
Measure boost startup, full/partial refresh and recovery with Wi-Fi active:
positive/negative rails, VCOM including its AC swing, Q2 VGS/VDS, differential
C26 voltage, RESE voltage and input/inductor currents. Calculate R22 average
power from mean(V_RESE^2)/R and evaluate repetitive pulses separately.
Q2 source current measured at R22 is not full-cycle inductor RMS current.
Bound retry counts after BUSY timeout; avoid endless boost restart attempts.
Do not implement status reads unless the selected driver/interface supports
bidirectional SDA and the documented detection commands have been run.
C35's effective-capacitance issue must be resolved in hardware, not assumed
fixed by firmware. Capacitor curves are typical; rail measurements remain needed.

Sleep clarification: the generic SSD1680 data distinguishes RAM-retaining deep
sleep mode 1 from non-retaining mode 2. Earlier blanket "sleep loses RAM" notes
are not universal to all modes/revisions. Preserve the conservative application
behavior: after reset/power loss or whenever the exact panel driver requires it,
rebuild frame history and perform the required full initialization/refresh.

## C35 hardware correction implemented (2026-10-20)
C35 now uses Samsung CL21B105KBFNNNE / C28323, 1 uF / 50 V X7R 0805.
This supersedes the proposed-only C35 status above. The replacement's typical
effective capacitance is about 0.739 uF at 18 V, compared with the previous
part's 0.173 uF. No firmware pin or sequencing change follows from this edit.
Retain loaded VCOM/startup/full/partial refresh measurements and bounded BUSY
recovery in bring-up. Other boost recommendations are not implemented.

C35 release package passed final automated checks (75 assembly assignments).
Release SHA256: ed4c3eb81f91b7b577041ed0f4086d3f9add4574896867e49dbe3ac67fc5d7b5
This records design/export verification, not measured display qualification.

## C26 hardware improvement implemented (2026-10-20)
C26 now uses Samsung CL31B475KBHNNNE / C51205, 4.7 uF / 50 V X7R 1206,
between EPD_SW (pad 1) and EPD_X (pad 2). This supersedes its earlier proposed
status. Typical capacitance at 20 V improves from about 0.749 uF to 2.614 uF;
this is not a guaranteed operating minimum. No firmware pin changes.
Retain startup, full/partial-refresh, differential flying-capacitor voltage and
overshoot measurements, including Wi-Fi load and low-battery conditions.
Other remaining boost proposals and physical fit are still open.

C26 final release package passed automated checks. Release SHA256:
8fe3b16522e2e7c5a0f86be462160d447aeb0684b9359908f7c45b396fa1a0d3
Powered display qualification remains pending.

## C30-C34 hardware margin improvement implemented (2026-10-21)
All five now use Samsung CL21B105KBFNNNE / C28323, 1 uF / 50 V X7R 0805,
replacing C15849 X5R 0603. Rail assignments and firmware pins are unchanged.
This supersedes their earlier proposed status. Higher retained capacitance
may affect charging demand and startup: measure rail rise, ripple, full/partial
refresh, low-battery and concurrent Wi-Fi operation using the delivered panel
profile. VCOM's explicit minimum is not assumed to apply to all five rails.
Other boost proposals and physical fit remain open.

C30-C34 final release package passed automated checks. Release SHA256:
5839c08035ca05ec8053301ac8b0da3d0c41f4c56da28b1d2198156b26ee2a2b
Powered display qualification remains pending.

## L1/R22 panel-reference pair implemented (2026-10-22)
L1 is now 68 uH SWPA4030S680MT / C83473; R22 is 3 ohm 1% 62.5 mW
0402WGF300KTCE / C423196. This supersedes earlier proposed-only status.
No pin or routing changes. Use the delivered panel driver/profile; preserve
its boost soft-start and waveform settings. Measure startup/refresh current,
rail rise, ripple and temperature, including low battery and Wi-Fi load.
R22 average dissipation is mean(V_RESE squared)/3; check repetitive pulse
stress separately. RESE measures Q2 source current, not full-cycle inductor
RMS current. Do not infer an absolute current limit from an undocumented
RESE threshold. The new inductor has higher DCR and lower listed current
ratings, so actual saturation/thermal margin is still unqualified.

L1/R22 package passed final automated checks. Release SHA256:
931f2d638a8bbd6aaf00405357a3571ee9273f026700c0d0e58681a766c8c425
This is export/design verification; powered display qualification remains pending.

## R21 gate pull-down implemented (2026-10-22)
R21 is now 10 kohm / C25744, replacing 1 Mohm / C26083 in the same 0402
footprint. This supersedes its earlier proposed status. EPD_GDR-to-GND
connectivity and firmware pins are unchanged. Measure GDR relative to Q2
source through startup, reset and shutdown. Do not infer switching turn-off
time from typical input capacitance and the pull-down alone while the gate
driver is active. Diode review and powered display qualification remain open.

R21 package passed final automated checks. Release SHA256:
c4213041f3b4d67e56dc1853085a42a71d288cc57f3040ff0341bcfcc6f77cab
Powered gate/display qualification remains pending.

## D2-D4 reference-family change implemented (2026-10-23)
D2-D4 now use onsemi MBR0530T1G / C82046 in the same SOD-123 footprint.
Pin 1 cathode mappings preserved: D2 PREVGH, D3 GND, D4 X; pin 2 anodes
remain SW, X and PREVGL respectively. No firmware pin changes. This supersedes
earlier proposed-only status. Ratings are 30 V / 0.5 A rather than 40 V / 1 A.
Use the delivered panel waveform and boost settings; measure diode reverse
voltage including startup overshoot, current/heating, negative-rail startup
and idle leakage. Datasheet forward-voltage limits use specified pulse/
temperature conditions; automated PCB checks do not prove operating margin.

D2-D4 final validation: ERC 0 violations; DRC 0 errors, unconnected items or
schematic-parity issues, with only the two existing U1/J1 footprint warnings.
Board verification and all 15 regression tests pass. Backup comparison confirms
only D2-D4 value/LCSC/MPN metadata changed in the PCB and schematic; routing,
geometry and existing UUIDs are preserved. Temporary schematic regeneration
agrees with component metadata and positions. Manufacturing exports regenerated;
diode labels visually checked. Final automated order checks pass all 75 exact
assembly assignments, archive completeness and release hashes.
Release SHA256: c8c927b90f6aee0cb7d5948541e5a4dc58848645ccfb6573b46cc919cf581b3e
Physical fit, supplier review, part availability and powered startup/overshoot/
current/thermal qualification remain pending. Prototype order hold unchanged.
