# Revision D mechanical interface

All x/y dimensions are millimetres from the PCB's top-left corner, viewed from
the display/front side. The board is 42 × 98 × 1.6 mm. These are drawing-derived
nominals, not measurements of purchased parts. Print `fab/mechanical.svg` at 100%.

| Feature | Nominal |
|---|---|
| Glass envelope | x 6.4–35.6, y 0–59.2; thickness 1.0 ±0.1 |
| Active area | x 9.15–32.85, y 7.95–56.50; 23.7046 × 48.55 |
| Visible area | approximately x 8.75–33.25, y 7.55–56.90; 24.50 × 49.35 |
| FPC connector origin | x 21.20, y 9.40, back |
| FPC pad 1 | x 26.95, y 11.02 |
| FPC pad 24 | x 15.45, y 11.02 |
| Wheel center | x 36.50, y 61.50 |
| Wheel diameter | 14; rightmost edge x 43.50, 1.50 beyond PCB |
| Hole centers | (3.5,3.5), (38.5,3.5), (3.5,94.5), (38.5,94.5) |
| Hole diameter | 2.7 NPTH, M2.5 screws |
| Top screw head/boss diameter | maximum 4.5; 0.65 nominal glass clearance |

The Good Display page-7 drawing is rotated clockwise so the tail exits at the
PCB top. Its cable edge is 8.15 mm from the glass's right side in this orientation;
the first pad is another 0.50 mm inward. Thus pin 1 is at 35.6 − 8.15 − 0.50 = 26.95,
and the 24-pad array center is 26.95 − 11.5/2 = 21.20. Folding over the top edge does
not exchange left and right. Insert pin 1 toward the PCB right side as viewed from
the front (left side when directly viewing the back). The dual-contact connector
accepts either contact face; it does not make a reversed pin order safe.

Active-area bottom inset is 0.45 + 1.45 + 0.40 + 0.40 = 2.70 mm, so its top is
59.20 − 2.70 − 48.55 = 7.95 mm. Allow drawing tolerances and adhesive positioning
in the case window; do not use the exact active rectangle as a zero-clearance cutout.

Alps drawing No.2 gives the knob center 1.8 mm below the lower edge of the mounting
lands. In the footprint: −4.9 + 0.7 + 1.8 = −2.4 mm. At the placed back-side 90°
rotation that becomes x = 34.1 + 2.4 = 36.5. Slots and pads retain the manufacturer's
land geometry. The selected H suffix has ±30% resistance tolerance and end stops.

Use nylon hardware at the antenna-side top-right hole. Keep the battery, metal
case, screws and other conductors out of the antenna keepout shown in KiCad.
The panel clears that keepout by 1.85 mm nominal. The wheel is below it.

The 3D flex is schematic geometry, not a bend simulation. The actual 14.3 mm tail,
6 mm stiffener, adhesive thickness and connector insertion must be dry-fitted on
one prototype. The battery has no specified enclosure pocket yet: choose the pack
and support it on an insulating tray above component height without loading solder
joints or crossing the antenna region. A battery must not rest directly on components.
