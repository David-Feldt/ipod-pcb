# E-ink Remote

A pocket Spotify remote: ESP32-S3-MINI-1-N8, 1.54-inch e-paper panel, five buttons,
USB-C and a 1S LiPo, on a 40 × 78 mm four-layer PCB. Playback happens on another
Spotify device; this board has no audio or microSD hardware.

See `DESIGN.md` for the GPIO map, power architecture and revision B changes.

![Back of board](renders/back.png)

## Project files

| Path | Contents |
|---|---|
| `kicad/eink-pod.kicad_pro` | KiCad 10 project |
| `kicad/eink-pod.kicad_sch` | Generated schematic |
| `kicad/eink-pod.kicad_pcb` | Generated and routed PCB |
| `kicad/erc.rpt`, `kicad/drc.rpt` | Latest electrical and board checks |
| `kicad/drc-all.rpt` | DRC including silkscreen warnings |
| `kicad/verification.txt` | Circuit/layout regression checks |
| `fab/eink-pod-gerbers.zip` | Gerbers and drill files |
| `fab/eink-pod-bom.csv`, `fab/eink-pod-cpl.csv` | Assembly BOM and placement |
| `fab/eink-pod-schematic.pdf` | Printable schematic |
| `gen/` | Circuit, placement, fixed routing and output generators |

## Rebuilding

Run `./build.sh` with KiCad 10, Python 3, Java and `tools/freerouting.jar` installed.
The script regenerates the schematic and PCB, checks ERC, routes the board,
checks DRC and schematic parity, verifies the critical routes, then refreshes
fabrication files and front/back renders. It stops if a required check fails.

Edit `gen/build_sch.py` for circuit changes, `gen/build_pcb.py` for placement,
and `gen/critical_routes.py` for USB, decoupling and boost wiring. The router
preserves these explicit routes; `gen/verify_board.py` checks they survived.
Each routing run uses a separate exchange directory to avoid concurrent-build
collisions. Avoid rebuilding while another editor is saving the same board.
Manual edits to generated KiCad files will be overwritten by the next build.

## Layers and fabrication

| Layer | Use |
|---|---|
| F.Cu | Buttons, charge LED, USB pair, other signals and GND pour |
| In1.Cu | GND plane, reference for the long USB pair |
| In2.Cu | 3.3 V plane |
| B.Cu | Main components, signals and GND pour |

Use the JLC04161H-7628 four-layer, nominal 1.6 mm stackup with 1 oz outer and
0.5 oz inner copper. The USB trunk is 0.25 mm wide with 0.15 mm edge spacing;
short connector neckdowns are 0.15 mm wide. Specify a 90-ohm differential target
and confirm it with the fabricator for the actual stackup and copper pours.
DRC does not verify impedance. See `fab/README.md` for ordering notes.

Assembly is on both sides. To use bottom-side assembly only, hand-solder
SW1–SW5, D5 and R4 on the front. Check every assembly rotation, especially the
MOSFETs, USB-C connector, FPC connector and slide switch, in the assembler's
preview. DS1 is a mechanical display model and is excluded from assembly files.

## Firmware and bring-up

- GPIO2 is now `USB_PRESENT_N`: low means USB is connected. Do not use the old
  active-high `VBUS_DET` logic. Disable internal pulls; R8 is the external pull-up.
- Battery voltage is twice the calibrated GPIO1 ADC voltage. Wait at least
  250 ms after power-on for the switched divider and its 100 nF filter to settle.
- Use the `GxEPD2_154_GDEY0154D67` panel driver and the GPIO map in `DESIGN.md`.
- Buttons use internal pull-ups; configure RTC pull-ups and wake behavior for
  deep sleep. BOOT and RESET remain accessible for USB recovery/flashing.
- Use a protected 1S LiPo whose manufacturer allows 500 mA charging. Verify the
  JST lead polarity; battery connectors are not universally wired the same way.
- Test USB in both orientations, Wi-Fi during low battery and screen refresh,
  off/sleep current, and charging temperature before relying on runtime estimates.
- Fit the actual display flex, battery and enclosure before ordering a batch.
  The PCB has no dedicated mounting holes; the enclosure must support button
  forces. Keep the battery and conductive enclosure/display features away from
  the antenna region, then test radio performance in the assembled device.

Firmware has not been implemented in this repository.
