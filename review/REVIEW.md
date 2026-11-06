# Guided hardware review

Release manifest SHA256: 38e062ea73ddf83fbbf1776b23b3f5727411f6d16b3efda149deab955f685272

This is a learning/review session, not order sign-off. No circuit changes made.

## Current round
02: TPS63031 input range, buck/boost behavior, then current capability.
Status: regulator retained for prototype by user; load validation deferred to bring-up. Next discussion: USB interface and power source.
Source: original fab/eink-pod-schematic.pdf, gen/build_sch.py, TI SLUS810N.

## Review queue
- Charger: pin/package map, tolerances, TS fixed bias, timers, thermal layout.
- USB input capacitance/inrush: C1 nominal 10 uF plus C40 100 nF; compare total direct VBUS capacitance, tolerance and DC bias against TI page 21 and source requirements. Not yet resolved.
- Regulator: supply range, load transients, component ratings and loop layout.
- ESP32/USB: reset/boot, pin allocation, reference plane, signal integrity.
- Display: boost substitutions, power sequence, connector pin mapping and physical flex fit.
- Sensing/controls: ADC range, leakage/back-power, wake behavior.
- Assembly and physical fit: exact parts, pin 1, polarity, supplier preview.

## Presentation issue
The exported schematic has overlapping component values/net labels. Use the source-derived connection table for clarity and cross-check the actual netlist during pin review.

## Charger discussion decisions
- User accepts USB100 hardware default; firmware may select USB500 after source permission. Source detection for wall adapters remains unresolved.
- User confirms Canada Robotix SKU 0588, 3.7 V 1000 mAh. Supplier listing explicitly states overvoltage, overcurrent and minimum-voltage protection: https://www.canadarobotix.com/products/588 .
- Approximately 223 mA nominal charge setting fits the supplied 1C charge rating. Exact voltage tolerance, pack fit and measured connector polarity remain to check.
- User wants charging with switch OFF and accepts the existing slow USB100 charging behavior as the simplest design.
- User elects to retain fixed TS bias, assuming operation inside the battery charging-temperature range. No battery-temperature sensing is provided; this is an accepted limitation, not verified temperature safety.
- No hardware has been bench-tested; no order-review.json supplier sign-offs have been changed.

## Active-load requirement clarified
User expects substantial ESP32 activity while ON, not a passive/sleep-centric device. Size for sustained CPU/Wi-Fi operation and overlapping display refresh, with transient margin at low battery. ESP32 module table lists 355 mA Wi-Fi TX peak under its test conditions; Espressif recommends at least 500 mA supply capability. Add display and other loads; do not treat these figures as an established total worst-case draw. TPS63031 low-voltage boost headroom remains unverified. No regulator change authorized or made by this discussion.

## Firmware requirements record
Maintain [FIRMWARE-NOTES.md](../FIRMWARE-NOTES.md) during the remaining review.
User accepts GPIO1 low-battery monitoring followed by deep sleep, with pack
protection as backup. Threshold, hysteresis, wake policy and GPIO retention remain
to be designed and tested. This is not regulator shutdown; SW8 directly controls EN.

## Load estimate and brownout protection
Provisional combined rail estimate: 415 mA (355 mA listed ESP32 TX figure plus
assumed 50 mA display/boost and 10 mA other-load allowances). Not a guaranteed
worst case. Conservative 500 mA ESP32 allocation gives 560 mA before extra margin.
Full assumptions and optional TPS3808 supervisor discussion recorded in
FIRMWARE-NOTES.md. No supervisor selected or added; regulator sign-off remains open.

## Decision: no added supervisor (2026-10-20)
User chooses the existing hardware with firmware low-battery sleep and internal
ESP32 brownout detection. Do not add a supervisor or automatic hardware cutoff.
See FIRMWARE-NOTES.md for implementation requirements. Regulator load capability
remains an open hardware check, independent of this simplicity decision.

## Regulator review disposition (2026-10-20)
User accepts retaining U5 for the prototype and defers physical load validation to
bring-up. This is a prototype decision based on reviewed datasheet information and
a provisional load estimate, not a proven worst-case current-margin sign-off.
Pending: actual inductor/capacitor rating checks, sustained combined workload, rapid
load steps, rail measurements at ESP32 pins over the selected battery operating
range, and tests with the actual pack. No hardware changes. Next: USB interface
and intended power source; wall-adapter current capability remains unresolved.

## USB power sources (2026-10-20)
User intends to use both computer USB ports and wall adapters at normal 5 V.
Voltage compatibility and current permission are separate checks. Automatic wall
adapter current identification remains open; USB presence or an enumeration
timeout is not sufficient evidence for USB500. See FIRMWARE-NOTES.md.

## USB prototype scope decision (2026-10-20)
User accepts no hardware changes: firmware selects USB500 after boot under an
adequate-5 V-source assumption; OFF remains USB100. No detector chips or USB
auto-power-on. This supersedes earlier universal-source-detection requirements;
compatibility with intended computer ports/adapters is a prototype test.

## Remaining guided review groups
1. ESP32 boot/reset, strapping and firmware recovery.
2. USB-C wiring, ESD and data routing (power policy now decided).
3. Display boost, pinout, sequencing and flex orientation.
4. Buttons, wheel, ADC sensing and leakage/back-power.
5. PCB implementation: power-component ratings, switching loops, decoupling,
   antenna clearance and test access; includes still-unverified power passives.
6. Mechanical/assembly/fabrication: actual fit, battery polarity, exact parts,
   orientation, stackup/via process and supplier previews.
Bench bring-up remains a separate gate after delivery.

## ESP32 boot/reset/recovery findings (2026-10-20)
Compared routed PCB pad nets for U1, R10/C8, SW6/SW7, J3 and TP1–TP3 with the
module datasheet pp.11–16 and Espressif boot/reset guidance. No wiring change
indicated: normal boot defaults retained, recommended RC starting point fitted,
manual USB ROM entry and UART fallback available. GPIO3 strap caveat and eFuse
recovery restrictions captured in firmware notes. Actual boot/power-ramp/recovery
tests remain for bring-up. Evidence page: 03-esp32-review.html.
Five grouped sections remain after this design-level review.

## USB wiring and protection review (2026-10-20)
Actual PCB nets checked for J1, R1/R2, U6, R12/R13, C1/C40. Separate 5.1 kΩ CC
pull-downs, joined duplicate data contacts, USBLC6-2SC6 pin map and ESP32 GPIO20
D+ / GPIO19 D− mapping are correct. Evidence: 04-usb-review.html.
Open recommendation: change C1 10 µF to a suitable 4.7 µF ceramic and retain
C40 100 nF. Direct VBUS total is 10.1 µF nominal; TI p.21 calls for <10 µF
for USB inrush. Tolerance/DC bias make this a margin concern, not demonstrated
failure. Verify effective capacitance against TI IN bypass recommendation
(1–10 µF). No component/generator/release changes made.
Achieved 90 Ω impedance needs supplier confirmation. Summed copper lengths
include branches and do not establish equal point-to-point paths. Function,
inrush and ESD remain untested. Charging policy unchanged. Four review groups
follow USB: display, controls/sensing, PCB components/layout, mechanical/assembly.

## Display review started (2026-10-20)
Compared schematic topology with GDEY0213B74 datasheet pp.8–9 and 29. SPI mode
and signal assignments match. Boost uses nominal reference L/R/C values with
AO3400A/B5819W substitutions; part-margin and dynamic validation remain pending.
User requires both event-driven updates and playback progress. Firmware notes
capture paced partial updates, coalescing, BUSY handling and clearing refreshes.
Panel timing/quality and flex fit are not signed off. Evidence: 05-display-review.html.

## C1 change applied (2026-10-20)
User approved C1 10 µF → 4.7 µF / 25 V, Samsung CL21A475KAQNNNE / C1779,
X5R ±10%, same 0805 footprint. Generator and generated design updated; original
copper routing preserved. Schematic/netlist, PCB value/part, BOM and fabrication
outputs regenerated. Initial full reroute was interrupted and its generated
KiCad files backed up before reusing the initially clean routed board. Final
PCB diff contains only C1 value and LCSC fields.
Manufacturer typical curve at 5 V is −31.84%, giving approximately 3.20 µF;
this is typical design evidence, not a guaranteed corner minimum. Curve saved
in c1-samsung-dcbias.json. Input inrush/droop still needs bench measurement.
The earlier proposed/open C1 status is superseded.

Display refinement: datasheet p.10 recommends a full refresh after five partial
or fast updates, with 0.42 s typical partial and 3 s full refresh at 25 °C.
Use this baseline in firmware. Clearing-flash tolerance and cadence are still
open UX questions; support for both static and progress modes remains required.

C1 release validation: fresh ERC, DRC/schematic parity, board topology/routing,
BOM/CPL and archive/hash checks passed. Only two existing documented footprint
warnings remain. Release SHA256: 9f06ac6abde2d9358110ca0e061f0dfc231cb695bb4bbe7c9419e537c467ee5c.
Supplier/physical sign-offs remain pending; no order approval recorded.

## Controls/sensing review (2026-10-20)
Moved to section 06 at user request; display boost/substitution and physical-fit
checks remain open. User accepts display clearing flashes; cadence deferred.
Actual PCB nets for buttons, wheel, divider and USB sensing checked against
schematic. No wiring correction identified. Voltage/RC estimates and firmware
requirements recorded in 06-controls-review.html and FIRMWARE-NOTES.md.
Accuracy, calibration, wake behavior and power-off leakage are bench checks.
Wheel is a 10 kΩ absolute potentiometer; confirm desired end-stop behavior.
Exact RK10J11R0A0H supplier drawing/availability is still pending.

Controls decision (2026-10-20): user accepts the existing absolute-position
wheel and delegates volume behavior to firmware. No controls hardware change.
Calibration, wake, leakage and real input behavior remain bring-up checks.

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

Q2 release verification: automated pre-order checks PASS; 75 exact BOM/CPL
assignments and complete fabrication archive. Release SHA256:
66370da1afdf333e56bd251ed4d0e87d9874b31cd3993447dafadedec6312386
Non-Q2 footprints and all other design components compared unchanged against
the pre-edit snapshot. Supplier/physical review remains pending in
order-review.json; no external sign-off was recorded.

## Remaining display boost component review (2026-10-20)
Added review/05-display-boost-findings.md and saved supplier/manufacturer evidence.
New concrete issue: C35 C15849 is X5R and typical effective capacitance at 18 V
is about 0.173 uF; SSD1680 Rev 0.14 p.43 specifies X7R and >0.25 uF for VCOM.
Propose C28323 (1 uF / 50 V X7R 0805), about 0.739 uF typical at 18 V.
Propose C26 C51205 (4.7 uF / 50 V X7R 1206) for improved margin; current
25 V rating alone is not proof of failure. C30-C34 bias retention also weak.
L1 ratings verified; existing 47 uH / 2.2 ohm has generic controller-reference
support, whereas Waveshare specifies 68 uH / 3 ohm. R22's 62.5 mW meets the
generic >=50 mW starting point; actual pulse/RMS margin still needs measurement.
Recommend panel-reference alignment for L1/R22/R21 and MBR0530-family diodes,
without claiming the existing circuit has been demonstrated faulty.
Correction: CJ B5819W's 120 pF entry is in the Max column, not typical.
J4 dimensions nominally match; tail thickness tolerances do not fully overlap
(connector +/-0.02 mm, panel +/-0.03 mm around 0.30 mm). Fit remains pending.
No hardware or fabrication change authorized/made. Display not fully signed off.

## C35 replacement implemented (2026-10-20)
User approved replacing only C35 with Samsung CL21B105KBFNNNE / C28323,
1 uF / 50 V X7R, using Capacitor_SMD:C_0805_2012Metric.
Updated the schematic generator, existing schematic (preserving UUIDs), design
metadata and PCB footprint. Preserved placement, side and net mapping:
pad 1 EPD_VCOM, pad 2 GND. Adjusted three local trace segments, maintaining
the incoming 45-degree VCOM corner. All other footprints and all vias match
the pre-edit snapshot; all other design components are unchanged.
Added exact C35 part/footprint/net checks to gen/verify_board.py.
ERC: 0 violations. DRC: 0 errors, 0 unconnected pads and 0 parity issues;
only the existing U1/J1 library-footprint warnings remain.
Protected-routing/geometry checks, 3 review tests and 12 release tests pass.
A temporary schematic regeneration agrees with current metadata for every part.
Backup path: /var/folders/zc/29lrkrhs2zjb_8cmv04dxzmh0000gn/T/ipod-before-c35-iit5rd21
Also recorded in /tmp/ipod-c35-backup-path.
C26, C30-C34, L1/R21/R22, D2-D4 and J4 remain unchanged. Display qualification
and physical/supplier checks remain open; this does not approve an order.

Final C35 export verification: refreshed the schematic PDF/review image,
Gerbers/archive, BOM/CPL, board renders and assembly maps. Added schematic
spacing for the longer C35 value label and visually checked the exports.
check-order.sh --automated-only passes, including fresh ERC/DRC/parity,
75 exact assembly assignments, archive completeness and release hashes.
Release SHA256: ed4c3eb81f91b7b577041ed0f4086d3f9add4574896867e49dbe3ac67fc5d7b5
External review remains pending; prototype order hold is unchanged.

## C26 replacement implemented (2026-10-20)
User approved Samsung CL31B475KBHNNNE / C51205, 4.7 uF / 50 V X7R 1206.
Updated schematic, metadata, schematic and routing generators, PCB and exact-part
verification. Position (109.7, 116) mm, B.Cu side and orientation preserved;
pad 1 EPD_SW, pad 2 EPD_X. Five trace segments adjusted and one short segment
added for the 45-degree approach. Copper zones refilled. All other footprints
and all vias unchanged against the backup. All UUIDs remain unique.
Temporary schematic generation agrees on every component and symbol position.
ERC: 0 violations. DRC: 0 errors, 0 unconnected, 0 parity issues; only the
existing U1/J1 library warnings. Board verification and all 15 regression tests pass.
Backup: /var/folders/zc/29lrkrhs2zjb_8cmv04dxzmh0000gn/T/ipod-before-c26-l6e8mi2v
C30-C34, L1/R21/R22, D2-D4 and J4 remain unchanged. Physical/supplier review
and powered display qualification remain pending. No order sign-off.

Final C26 manufacturing package: regenerated Gerbers/archive, BOM/CPL,
schematic PDF, board renders and assembly maps. Visually checked schematic
and back-board render. check-order.sh --automated-only passes fresh ERC/DRC,
75 exact BOM/placement assignments, archive completeness and release hashes.
Release SHA256: 8fe3b16522e2e7c5a0f86be462160d447aeb0684b9359908f7c45b396fa1a0d3
External supplier/physical checks remain pending; prototype order is on hold.

## C30-C34 replacements implemented (2026-10-21)
User approved Samsung CL21B105KBFNNNE / C28323, 1 uF / 50 V X7R 0805
for all five display-rail capacitors. Updated schematic, metadata, schematic
generator, PCB and exact-part checks. Spaced C30-C35 schematic symbols to
keep their longer value labels readable. Board placement, side, orientation
and net mapping preserved for all five. Adjusted 19 local trace segments:
vertical pad entries, VSH2/VCOM clearance paths, PREVGH corner and probe branches.
The routing generator preserves the adjusted PREVGH corner. Copper zones refilled.
All other footprints and all vias preserved; no tracks added or removed.
Backup: /var/folders/zc/29lrkrhs2zjb_8cmv04dxzmh0000gn/T/ipod-before-c30-c34-43qjrt00
This is a capacitance margin improvement, not evidence of a published minimum
violation on all five rails. L1/R21/R22, D2-D4 and J4 remain unchanged.
Physical/supplier review and powered display qualification remain pending.

Validation completed: ERC 0 violations; DRC 0 errors, 0 unconnected pads,
0 schematic parity issues, only the two existing U1/J1 footprint warnings.
Board/protected-routing checks and all 15 regression tests pass. Temporary
schematic regeneration agrees on all component metadata and symbol positions.
Final backup comparison confirms only the five intended footprints and 19 local
tracks changed; all placements, other footprints and vias preserved.
Regenerated and visually checked schematic PDF/review image and board render.
Updated Gerbers/archive, BOM/CPL and assembly maps. check-order.sh --automated-only
passes 75 exact assembly assignments, archive completeness and release hashes.
Release SHA256: 5839c08035ca05ec8053301ac8b0da3d0c41f4c56da28b1d2198156b26ee2a2b
External supplier/physical review remains pending; no order sign-off.

## L1/R22 pair implemented (2026-10-22)
User approved L1 68 uH (Sunlord SWPA4030S680MT / C83473) and R22 3 ohm
(UNI-ROYAL 0402WGF300KTCE / C423196, 1%, 62.5 mW). Exact resistor catalog
mapping and manufacturer family dimensions/power checked; repetitive pulse
and actual thermal/current margins remain pending bench measurements.
Updated generator, schematic, design metadata, PCB assembly/value fields and
exact-part/pin checks. Backup comparison confirms all geometry, tracks, vias
and zones unchanged. No other component selection changed. UUIDs unique;
temporary schematic generation agrees on all metadata and symbol positions.
ERC 0 violations; DRC 0 errors, 0 unconnected, 0 parity issues; only existing
U1/J1 footprint warnings. Board checks and all 15 regression tests pass.
Backup: /var/folders/zc/29lrkrhs2zjb_8cmv04dxzmh0000gn/T/ipod-before-l1-r22-i0p7gjoa
R21 and D2-D4 remain open. Supplier/physical review and display bench qualification
remain pending; no order sign-off.

Final L1/R22 package regenerated: Gerbers/archive, BOM/CPL, schematic PDF,
assembly maps and renders. Schematic review image refreshed and visually
checked. check-order.sh --automated-only passes fresh ERC/DRC/parity,
75 exact assembly assignments, archive completeness and release hashes.
Release SHA256: 931f2d638a8bbd6aaf00405357a3571ee9273f026700c0d0e58681a766c8c425
External supplier/physical checks remain pending; prototype order hold unchanged.

## R21 gate pull-down implemented (2026-10-22)
User approved R21 from 1 Mohm / C26083 to 10 kohm / C25744, retaining the
existing R_0402_1005Metric footprint. Updated generator, schematic, design
metadata, PCB value/LCSC fields and exact-part/pin checks. R21 pad 1 remains
EPD_GDR, pad 2 GND. No firmware pin changes.
Backup: /var/folders/zc/29lrkrhs2zjb_8cmv04dxzmh0000gn/T/ipod-before-r21-6riw_5i6
D2-D4 remain unchanged and await review. Physical/supplier checks and
powered gate/startup/shutdown qualification remain pending; no order sign-off.

R21 final validation: ERC 0 violations; DRC 0 errors/unconnected/parity issues,
only existing U1/J1 footprint warnings. Board checks and all 15 regression
tests pass. Backup comparison confirms only R21 value/LCSC fields changed
in schematic/PCB; geometry, routing and UUIDs preserved. Temporary schematic
generation agrees on component metadata and positions. Manufacturing package
regenerated and schematic image visually checked. Final automated order check
passes 75 assembly assignments, archive completeness and release hashes.
Release SHA256: c4213041f3b4d67e56dc1853085a42a71d288cc57f3040ff0341bcfcc6f77cab
External supplier/physical checks remain pending; prototype order hold unchanged.

## D2-D4 verification and replacement (2026-10-23)
User requested verification followed by implementation and asked whether the
changes are necessary. Verified MBR0530T1G / C82046 against onsemi Rev.9,
controller criteria, supplier mapping, CASE 425 style 1 polarity and actual
PCB pads/nets. Pin 1 cathode; pad sizes 0.90x1.20 mm at 3.30 mm vs suggested
0.91x1.22 mm at 3.27 mm. Retained standard SOD-123 footprint and orientation.
Updated three values, LCSC/MPN fields, schematic generator and exact-part/
polarity verification. No routing/geometry changes. Necessity is documented
as reference alignment rather than proven failure; reduced ratings and
unmeasured startup/overshoot/thermal margins explicitly remain open.
Backup: /var/folders/zc/29lrkrhs2zjb_8cmv04dxzmh0000gn/T/ipod-before-d2-d4-0grwq_ur

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
