# Revision E validation

Validated 2026-10-19 against the final routed PCB and generated schematic.

| Check | Result |
|---|---|
| ERC, all severities | 0 errors, 0 warnings |
| Electrical DRC | 0 errors |
| Unconnected pads | 0 |
| Generator / routed-board geometry and text | Match within 0.001 mm |
| Schematic / PCB parity | 0 differences |
| Critical circuit, isolation links and geometry verification | PASS |
| Display spacing, 10 mm button pitch, mounting heads and antenna margins | PASS |
| Individual TP1–TP4 UART silkscreen labels and pad nets | PASS |
| Local C40 USB ESD bypass and protected routes | PASS |
| Filled ground under USB centers and edges | 6594 samples pass |
| BOM / placement consistency | 75 assembled components, all with LCSC IDs |

The full DRC report contains two footprint-library mismatch warnings. U1 and
J1 have overhanging silkscreen outline segments moved to the fabrication layer
by `finish_silkscreen()` in `gen/build_pcb.py`; their pads and copper geometry
are unchanged. No silkscreen clearance warnings remain. The existing ignored
check categories are listed in the reports; no new exclusions were added.

`fab/manifest-sha256.txt` identifies the PCB, schematic, validation reports and
manufacturing files as one release. The fabrication archive and the BOM/CPL
were regenerated after the final board edits.

This is design validation, not a recorded hardware test. Supplier assembly
rotations, actual display-flex fit and battery lead polarity require the
checks in `MECHANICAL.md`, `fab/README.md` and `BRINGUP.md`. Select epoxy-filled,
copper-capped vias for assembly. Firmware has not been implemented.

The pre-order gate (`./check-order.sh`) reruns ERC, DRC/parity and circuit/layout
checks without regenerating the board. Export validation now checks each exact
LCSC assignment, value and footprint, duplicate references, and CPL coordinates,
side and rotation against the PCB. The manifest also covers generator sources,
design rules and assembly/mechanical references. Fault-injection tests in
`gen/test_release_checks.py` exercise stale files, wrong BOM assignments and
placements, incomplete archives, unreviewed DRC findings and stale sign-offs.
Supplier and physical reviews remain pending in `order-review.json`; automated
success alone does not authorize a prototype order.

Revision E also verifies DNP header attributes and pinouts, the three spare GPIOs,
short local boost-probe connections, at least 0.5 mm from J3/J5 header pads to
any battery, USB or system power copper, and that tracks, pads and vias clear the
4.5 mm mounting heads. The PCB outline remains 42 × 98 mm. J3/J5 and all test
pads are absent from the assembly BOM/CPL; the three links are present.

Spacing was rechecked against the routed board with zone refill and full DRC.
No copper, courtyard or silkscreen spacing violations were found under the
configured rules (0.15 mm global copper clearance, 0.30 mm copper-to-edge,
0.25 mm hole clearance). The silkscreen rule checks overlap with a 0 mm minimum;
this does not certify a fabricator-specific silkscreen margin. Explicit display
checks now cover its missing courtyard: the minimum glass gaps are 4.05 mm to
front component courtyards, 0.80 mm to front lands and 0.65 mm to 4.5 mm mounting
heads. See `MECHANICAL.md` for the nominal dimensions and physical fit limits.

The October 19 review follow-up replaces the reversed combined UART legend with
individual mirrored labels above TP1–TP4. C40 adds 100 nF of local VBUS bypassing
opposite U6; its fixed planar supply path is about 1.38 mm through one via,
with a separate local ground via. This is a layout improvement, not an ESD test.

The USB reference check now samples actual filled In1.GND at the trace center
and both edges every 0.05 mm. Only the 0.60 mm radius around that signal's own
transition vias is exempt, to allow their antipads. The stronger check exposed
a small EPD_MOSI via antipad intrusion beneath a USB trace edge on the previous
board. A via-only keepout now protects the long trunk. This sampling verifies
copper coverage; it does not calculate impedance or certify USB signal integrity.
