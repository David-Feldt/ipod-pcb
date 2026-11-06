# Display boost component review - 2026-10-20

Status updated 2026-10-23: D2-D4, R21, L1/R22, C26 and C30-C35 replacements approved and implemented.
C35 now uses CL21B105KBFNNNE / C28323, 1 uF / 50 V X7R 0805.
Generator, schematic, design metadata and PCB updated; three local trace
segments adjusted. C26 now uses CL31B475KBHNNNE / C51205, 4.7 uF / 50 V
X7R 1206; five existing trace segments adjusted and one short segment added.
C30-C34 now also use CL21B105KBFNNNE / C28323, 1 uF / 50 V X7R 0805.
Five footprints and 19 local trace segments updated; positions and nets preserved.
L1/R22 now use 68 uH / 3 ohm: SWPA4030S680MT / C83473 and
0402WGF300KTCE / C423196. Footprints and all routing unchanged.
R21 now uses 10 kohm / C25744 in its existing 0402 footprint; routing unchanged.
D2-D4 now use onsemi MBR0530T1G / C82046 with their existing SOD-123
footprints and polarity. Q2 remains implemented. Physical and powered
qualification remain open. See REVIEW.md for validation and release details.
The display is not fully signed off; physical fit and powered validation remain open.

## C35: effective-capacitance mismatch corrected

**Finding:** the previous VCOM capacitor was a poor match to the controller reference.

**Evidence:** SSD1680 Rev 0.14 p.43 identifies its C8 as the capacitor between
VCOM and ground, corresponding to our C35 at J4 pin 24. It specifies X7R and
effective capacitance greater than 0.25 uF at 18 V DC bias.
The previous C15849 was Samsung CL10A105KB8NNNC, 1 uF / 50 V X5R 0603.
Linear interpolation of Samsung's typical 25 C bias data at 18 V gives
0.1734 uF. This does not demonstrate compliance with that reference requirement.
VCOM can swing with source voltages; do not evaluate it solely at its negative
DC offset.

**Implemented:** C35 -> Samsung CL21B105KBFNNNE, C28323,
1 uF / 50 V X7R 0805. The same calculation gives 0.7386 uF at 18 V.
The footprint and local routing were changed; DRC/parity and geometry checks pass.
The curve is typical evidence, not a guaranteed minimum across tolerance,
temperature, aging and production. Confirm loaded VCOM waveform on hardware.

Sources:
- [SSD1680 manufacturer reference, p.43](ssd1680.pdf#page=43), obtained from
  https://files.seeedstudio.com/wiki/Other_Display/213-epaper/IC%20Driver%20SSD1680%20Datasheet.pdf
- https://product.samsungsem.com/mlcc/CL10A105KB8NNN.do
- https://product.samsungsem.com/mlcc/CL21B105KBFNNN.do
- https://jlcpcb.com/partdetail/C28323
- Saved curves: boost-CL10A105KB8NNN-dcbias.json and boost-CL21B105KBFNNN-dcbias.json.
  Interpolation uses adjacent samples at 17.592 V and 18.448 V.

## C26: voltage and capacitance margin improvement implemented

**Finding:** 25 V is not automatically a failure, but the previous 0805 part
loses most of its nominal capacitance at the boost voltage.

**Evidence:** C26 connects EPD_SW to EPD_X. From the diode topology, its steady
charge is of the order of the positive boosted rail, not 3.3 V and not automatically
the sum of the positive and negative rails. Actual differential voltage and
overshoot require measurement. Both Waveshare reference diagrams specify
4.7 uF / 50 V. The generic SSD1680 diagram allows a 25 V flying capacitor, so the
difference alone is not proof of overvoltage.

Samsung CL21A475KAQNNNE / C1779 typical curve gives:
- 15 V: 1.062 uF
- 20 V: 0.749 uF
- 25 V: 0.569 uF

**Implemented:** C26 -> Samsung CL31B475KBHNNNE / C51205,
4.7 uF / 50 V X7R 1206. Its typical curve gives 3.167 uF at 15 V,
2.614 uF at 20 V and 2.150 uF at 25 V. This improves voltage headroom and
retained capacitance; it does not establish a guaranteed operating minimum.
Replaced the 0805 footprint with 1206 at the same position and side. Updated
the adjacent switching traces and routing generator; ERC and DRC/parity pass.
Pad 1 remains EPD_SW and pad 2 remains EPD_X.

Sources:
- Waveshare V3 p.26 / V4 p.28, saved in this directory.
- https://product.samsungsem.com/mlcc/CL21A475KAQNNN.do
- https://product.samsungsem.com/mlcc/CL31B475KBHNNN.do
- https://jlcpcb.com/partdetail/52218-CL31B475KBHNNNE/C51205
- Saved boost-CL21A475KAQNNN-dcbias.json and boost-CL31B475KBHNNN-dcbias.json.

## C30-C34: capacitance margin improvement implemented

**Finding:** these previously used C15849. Its 50 V rating provides
voltage headroom, but its capacitance is about 0.21 uF at 15.26 V and
0.15 uF at 20.25 V, from Samsung's typical data.

**Implemented (2026-10-21):** user approved the same 0805 C28323 for C30-C34
as a margin improvement following the C35 correction. This is not the same evidence strength as C35:
the explicit >0.25 uF at 18 V requirement applies to VCOM, not every rail.
No claim that the previous five parts failed a published minimum. Account for larger
charge/startup demand and validate rail rise, ripple and refresh behavior.
C33 may operate at a lower voltage depending on the panel profile.

## L1 and R22: panel-reference pair implemented; current qualification pending

**Finding:** the previous 47 uH / 2.2 ohm pair agrees with the generic SSD1680 reference;
both supplied Waveshare panel references instead show 68 uH / 3 ohm.
The previous values were not intrinsically invalid.

**Evidence:** Sunlord's 2022 SWPA catalog, PDF p.8 / printed p.193:

| Part | L | DCR max / typical | Listed Isat / typical | Listed Irms / typical |
|---|---|---|---|---|
| SWPA4030S470MT / C54731 | 47 uH +/-20% | 0.579 / 0.445 ohm | 0.95 / 1.00 A | 0.72 / 0.80 A |
| SWPA4030S680MT / C83473 | 68 uH +/-20% | 1.128 / 0.868 ohm | 0.72 / 0.80 A | 0.52 / 0.57 A |

The catalog headings call the first current columns "Max."; do not relabel them
as guaranteed minimum saturation thresholds. Footnotes p.16 define Isat at about
30% inductance loss and Irms at 40 C rise from 20 C ambient. These are test
conditions, not the desired operating point.

R22's Uniroyal 0402 rating is 1/16 W at 70 C, above the generic controller
reference's >=0.05 W requirement. Package size alone is not a demonstrated defect.
The reviewed sources do not supply a guaranteed RESE current threshold.

For illustration only, at equal threshold I_peak = V_threshold / R:
2.2 ohm would permit 3/2.2 = 1.36 times the current of 3 ohm.
At equal applied inductor voltage, the initial current ramp at 47 uH is
68/47 = 1.45 times that at 68 uH. Actual drive timing, losses and control behavior
matter. Do not convert these ratios into an invented absolute current limit.

**Implemented (2026-10-22):** user approved 68 uH / 3 ohm together for the
ordered Waveshare panel. L1 uses SWPA4030S680MT / C83473 in the existing
SWPA4030S footprint. R22 uses UNI-ROYAL 0402WGF300KTCE / C423196, 3 ohm,
1%, 62.5 mW in the existing 0402 footprint. Exact supplier mapping and
manufacturer family power/package specifications were checked. The resistor
meets the generic >=0.05 W reference rating, but this does not qualify its
repetitive pulse or thermal stress in this circuit. All geometry/routing is unchanged.
The 68 uH part has higher DCR and lower listed current ratings than the old
47 uH part; startup current, saturation margin and temperature remain unmeasured.
Do not require a larger resistor solely from its size. Compute average power
from measured RESE voltage as mean(V_RESE^2)/R and check repetitive pulse
stress, temperature and startup. Sense-resistor current is Q2 source current;
it is not the full-cycle inductor RMS current. Measure inductor current separately
or reconstruct the off interval correctly to evaluate its thermal rating.

Sources:
- https://www.sunlordinc.com/uploads/files/20221122/SWPA%20series%20of%20SMD%20Power%20Inductor.pdf
  (saved sunlord-swpa.pdf)
- https://jlcpcb.com/partdetail/Sunlord-SWPA4030S680MT/C83473
- [Uniroyal resistor data, p.4](boost-C25158.pdf#page=4), from https://jlcpcb.com/partdetail/C25158
- https://www.lcsc.com/product-detail/C423196.html (exact 3 ohm / 1% / 62.5 mW / 0402 mapping)
- https://www.royalohm.com/assets/pdf/products/smd/1.pdf (manufacturer family dimensions and power ratings)
- [SSD1680 application reference](ssd1680.pdf#page=43)

## R21: panel-reference pull-down implemented

**Finding:** the previous 1 Mohm pull-down was 100 times weaker than Waveshare's 10 kohm
pull-down. This affects gate discharge when the driver is high impedance and
susceptibility to coupled charge; it does not prove Q2 is inadvertently turning on.

**Implemented (2026-10-22):** user approved 10 kohm, existing 0402 C25744
selection, as shown by both Waveshare references. Footprint, routing and
EPD_GDR-to-GND mapping preserved. Measure GDR relative to Q2
source during startup, reset and shutdown. Do not infer turn-off time simply
from typical input capacitance and this resistor when the driver is active.

## D2-D4: verified reference-family substitution implemented

**Finding:** the previous CJ B5819W / C8598 has higher voltage/current ratings than
MBR0530, but its dynamic and leakage behavior differs.

**Evidence:** CJ p.1 gives 40 V / 1 A, a capacitance entry of 120 pF at
4 V / 1 MHz (placed in the table's Max column), and maximum leakage 1 mA at
40 V. This corrects the earlier handoff's description of that entry as typical.
Onsemi MBR0530T1G is 30 V / 0.5 A, has typical capacitance around 60 pF at
4 V from p.3, and leakage limits of 20 uA at 15 V / 130 uA at 30 V.
These are not matching leakage test voltages or matching capacitance guarantees;
do not report a precise performance ratio.
The controller reference names MBR0530 and calls for <=430 mV forward voltage;
CJ's guaranteed 0.6 V at 1 A does not establish performance at the actual pulses.

**Implemented (2026-10-23):** user requested verification followed by the
change. D2-D4 now use onsemi MBR0530T1G / C82046, retaining their footprints,
positions, polarity and routing. Manufacturer Rev.9 specifies 30 V reverse,
0.5 A average at TL=100 C, and maximum 0.43 V forward at 0.5 A / TJ=25 C
with a 300 us, <=2% duty pulse test. These match the generic controller's
listed diode criteria at the specified conditions; they do not establish
worst-case circuit margins. The supplier catalog maps C82046 to this exact part;
actual order quantity/stock and assembly preview still require supplier confirmation.

Package check: onsemi CASE 425 style 1 identifies pin 1 as cathode and pin 2
as anode. Its suggested pads are 0.91 x 1.22 mm with 3.27 mm center spacing;
our standard KiCad SOD-123 uses 0.90 x 1.20 mm at 3.30 mm spacing. The small
differences are compatible with retaining the standard land pattern; final
assembly preview remains open. Preserved cathode/anode nets:
D2 PREVGH/SW; D3 GND/X; D4 X/PREVGL. The cathode band remains on pad 1.
The datasheet's discontinued ordering entry is NRVB0530T3G, not MBR0530T1G.

**Necessity:** this is reference alignment, not a demonstrated repair of faulty
B5819W diodes. Waveshare does not identify a manufacturer, so this is not proof
of its identical vendor part. The change reduces nominal ratings from 40 V/1 A
to 30 V/0.5 A. Measure reverse-voltage overshoot, diode current/heating,
negative-rail startup and idle leakage before hardware acceptance.

Sources:
- [CJ B5819W](boost-C8598.pdf), from https://jlcpcb.com/partdetail/C8598
- https://www.onsemi.com/download/data-sheet/pdf/mbr0530t1-d.pdf (Rev.9; saved mbr0530-onsemi.pdf)
- [Verified package and polarity drawing](05-mbr0530-package.png)
- https://jlcpcb.com/partdetail/onsemi-MBR0530T1G/C82046

## J4: nominal compatibility, tolerance and fit still open

**Finding:** the connector drawing lists FPC-05FB-24PH20 with 24 contacts,
0.5 mm pitch, 2.0 mm height, and nominal 0.30 mm cable thickness.
The Waveshare drawing also has 24 x 0.5 mm contacts and 0.30 mm stiffened tail.

**Evidence:** connector recommended tail thickness is 0.30 +/-0.02 mm;
panel drawing gives 0.30 +/-0.03 mm. Nominals agree but the panel tolerance
extends 0.01 mm beyond the recommendation at each end. Do not call this a
fully guaranteed mating fit. The manufacturer's connector PDF has malformed
cross-references; Poppler recovered a readable drawing, saved as
05-connector-drawing.png.

The existing footprint's 0.3 x 1.2 mm signal lands on 0.5 mm pitch, 11.5 mm
contact-array span and 2.0 x 1.8 mm mounting lands agree nominally with the
drawing (mounting-row separation rounds 3.25 mm to 3.24 mm in the library).
This is a drawing check, not an assembly approval.

**Recommendation:** retain J4 and the page-7 panel net mapping for now.
Verify actual tail thickness, latch retention, insertion depth, pad-1 orientation,
14.3 mm flex reach, bend radius and glass/enclosure fit. Dual contact accepts
either exposed contact face; it does not make reversed pin order safe.
If supplier confirmation of the tolerance fit is unavailable, obtain an explicit
first-prototype fit-experiment decision before marking the order check complete.

Sources:
- [XUNPU drawing](boost-C2856831.pdf), from https://jlcpcb.com/partdetail/C2856831
- Waveshare V4 p.6 and V3/V4 p.7.
- kicad/eink-pod.pretty/FPC-05FB-24PH20.kicad_mod and MECHANICAL.md.

## Proposed order of work

1. C35 correction completed: strongest direct component-selection evidence.
2. C26 voltage/capacitance margin improvement completed.
3. L1/R22 and C30-C34 updates completed. R21 and D2-D4 updates completed; proceed to physical fit, supplier review and bench planning.
4. Re-run ERC, DRC/parity, geometry/routing and release checks after any approved
   hardware edit; regenerate manufacturing files only then.
5. Complete physical fit and supplier checks before order sign-off; measure
   startup/refresh waveforms and combined Wi-Fi load after delivery.

D2-D4, R21, L1/R22, C26 and C30-C35 were approved and implemented in this review. Other candidates require final
layout and sourcing checks if approved. Exact delivered panel initialization,
waveforms, voltage settings and boost soft-start must be retained in the bench record.

## Which changes were required?

- C35: clearest documented mismatch. Previous X5R selection did not meet the
  reference X7R requirement, and its typical bias curve fell below the specified
  >0.25 uF at 18 V. A replacement was justified; new-part worst-case behavior
  still requires qualification.
- C26 and C30-C34: capacitance/voltage margin improvements. We did not establish
  that all previous selections violated a published operating minimum.
- Q2, L1/R22, R21 and D2-D4: reference alignment with identified tradeoffs.
  None was demonstrated to have failed on powered hardware. These are reasonable
  prototype selections, not proof every original part needed replacing.
- ERC/DRC/release checks validate design consistency and fabrication exports,
  not startup, thermal, waveform, battery-load or display compatibility margins.
