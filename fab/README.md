# Revision E fabrication notes

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
- BQ24074 power-path charger, TPS63031 buck-boost, 223 mA nominal charge current.
- Select epoxy-filled and copper-capped vias. Thermal pads contain vias; ordinary
  tenting is not sufficient for the specified assembly.
- Four M2.5 holes are unplated. Board size is 42 × 98 mm.
- `assembly-pin1.csv` provides pad-1 coordinates and nets to compare with the
  supplier preview. KiCad CPL angles are not automatically supplier-calibrated.
- Buy a protected 1S 4.2 V LiPo separately, rated for at least 250 mA charging and
  1 A pulse discharge; verify lead polarity. Firmware is required for operation.
- Check assembly rotations and connector orientation in the supplier preview.
- DS1 is a mechanical panel model, excluded from BOM and placement; purchase
  and fit the GDEY0213B74 panel separately.

Bench validation and the physical display/battery/enclosure fit are still
required. Electrical/layout reports are in `../kicad/`; these do not certify
battery protection, thermal behavior, RF performance or USB signal integrity.

See `../MECHANICAL.md` for cable/panel/wheel dimensions and `../BRINGUP.md` for
prototype acceptance. JLC stock is not reserved by assigning an LCSC number.

Revision E adds three assembled 0805 zero-ohm links (R23–R25, C17477). J3/J5
are DNP debug headers, excluded from BOM/CPL. Test pads require no components.
Use `debug-map.svg` and `debug-access.csv` with `../DEBUG.md` for probe access.
