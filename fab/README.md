# Revision B fabrication notes

Generated from `kicad/eink-pod.kicad_pcb`; regenerate with `./build.sh` after
circuit or placement changes. The archive contains copper, masks, paste,
silkscreen, board outline and drill files. Do not order from stale exports.

- Four layers, nominal 1.6 mm, JLC04161H-7628, 1 oz outer / 0.5 oz inner copper.
- Manufacturer stackup: outer dielectric 0.2104 mm each, inner copper 0.0152 mm
  each, core dielectric 1.065 mm; outer copper 0.035 mm each. Confirm the current
  manufacturer's construction and finished thickness when ordering.
- USB target: 90 ohm differential; F.Cu referenced to In1.GND, 0.25 mm width and
  0.15 mm edge gap on the long parallel section, 0.15 mm short connector
  neckdowns. Outer GND pours use 0.25 mm clearance. Impedance must be confirmed
  by the fabricator; the routing geometry is not a measured certification.
- Both-side assembly unless the seven front components are hand-soldered.
- BOM and CPL include R12/R13 (22 ohm), Q3/Q4/Q5 and R11. R9 is now 1 megohm.
- Check assembly rotations and connector orientation in the supplier preview.
- DS1 is a mechanical panel model, excluded from BOM and placement; purchase
  and fit the GDEY0154D67 panel separately.

Bench validation and the physical display/battery/enclosure fit are still
required. Electrical/layout reports are in `../kicad/`; these do not certify
battery protection, thermal behavior, RF performance or USB signal integrity.
