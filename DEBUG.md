# Revision E debug access

The board remains 42 × 98 mm. J3/J5 are unpopulated 2.54 mm through-hole
footprints, excluded from JLC BOM and placement files. Their holes can be used
for temporary wires, probes or hand-fitted headers. Fit headers on the back.
The additional debug components are R23/R24/R25: three 0805 zero-ohm
links, UNI-ROYAL 0805W8F0000T5E, LCSC C17477.

See `fab/debug-map.svg` for a mirrored back view and `fab/debug-access.csv` for
exact front-origin coordinates. Board silkscreen abbreviates long signal names.

## Programming and manual recovery

J3 pin order: 1 GND, 2 3V3, 3 TXD0 (board output), 4 RXD0 (board input),
5 EN, 6 GPIO0/BOOT. Square pad marks pin 1. This is a manual programming header,
not a generic FTDI cable pinout and not a DTR/RTS auto-reset interface.

Use a 3.3 V logic USB-UART adapter; connect adapter RX to board TX and vice versa.
The original test pads provide TP1/TX = TXD0, TP2/RX = RXD0, TP3/G = ground,
and TP4/3V3 = the system supply. Each now has its own aligned silkscreen label.
Use board battery power, or a suitable external 3.3 V supply with R24 removed.
Leave adapter power disconnected when the board is already powered. A small
USB-UART adapter's 3.3 V output is not assumed capable of supplying Wi-Fi peaks.

Hold GPIO0 low, pulse EN low, release EN, then release GPIO0 to enter the ROM
loader. BOOT/RESET buttons remain, and TP16/TP17/TP18 provide EN/BOOT/GND access.
No automatic-reset parts or firmware recovery application were added.

## Removable links

| Link | Pin 1 | Pin 2 | Use when removed |
|---|---|---|---|
| R23 | VBAT_RAW | VBAT | Insert an ammeter between TP6 (battery side) and TP5 (board side) |
| R24 | REG_3V3 | +3V3 | Isolate the buck-boost from the system; feed regulated 3.3 V at TP4 or J3 pin 2 |
| R25 | +3V3 | EPD_BOOST_IN | Disconnect L1 and its C27 input capacitor from the system supply |

Remove links with all power disconnected. For battery sleep-current measurement,
remove USB and externally powered debug/display leads; the ammeter must tolerate
startup and radio bursts without excessive burden voltage. All battery-positive
loads, including sensing and charging, are downstream of R23. Restore the link
before normal use.

U5 feedback and both required output capacitors C4/C37 remain on REG_3V3 when
R24 is removed. Use a current-limited 3.3 V bench supply and common ground, with
the slide switch OFF. Never bridge the switch to VSYS: its OFF position grounds
PWR_EN. R24 is not a reverse-polarity protection device.

R25 disables boost input power only. It does not disconnect J4 logic power or
SPI signals, and it does not discharge the high-voltage capacitors. Keep the
panel disconnected for boost-fault isolation and avoid refresh commands. There
is no new display power switch or signal-isolation circuitry.

## Display fallback and bus probing

J5 pin order matches the DESPI-C02 manual's signal list:
1 BUSY, 2 RST, 3 DC, 4 CS, 5 SCK, 6 MOSI/SDI, 7 GND, 8 3V3.
These are also the display bus test points. Match labels and pin 1 at both ends;
connector viewing direction can reverse a cable. Use short wires and start with
a low SPI clock. It is a wired diagnostic interface, not a mechanical stacking
connector for the adapter board.

With power disconnected, move the actual panel from J4 to the DESPI-C02, remove
R25 to isolate the onboard boost input, and wire J5 to the adapter. Use only one
panel connection and one 3.3 V supply. Set the adapter's RESE option to 2.2 ohms
for GDEY0213B74. This bypasses the onboard FPC connector and boost circuitry; it
does not bypass a fault on the MCU's SPI nets.

## Probes and spare GPIOs

- Power: TP4 +3V3, TP5 VBAT, TP6 VBAT_RAW, TP7 VBUS, TP8 VSYS, TP10 REG_3V3.
- Ground: TP3, TP9, TP18, TP29, TP30; J3 pin 1 and J5 pin 7.
- Sensing: TP11 PWR_EN, TP12 CHG_STAT, TP13 VOL_SENSE, TP14 VBAT_SENSE,
  TP15 USB_PRESENT_N. CHG_STAT is the charge LED sink, not a 3.3 V GPIO output.
- Spare: TP19 GPIO8, TP20 GPIO14, TP21 GPIO15. GPIO6/7 remain charger controls.
- Tiny local boost probes: TP22 switching node, TP23 gate drive, TP24 current
  sense, TP25 PREVGH, TP26 PREVGL, TP27 VSH1, TP28 VSL. These have short fixed
  connections and no remote header stubs. TP29/TP30 provide nearby ground.

High-voltage rail values depend on the panel's configured waveform. Check the
GDEY0213B74 specification; do not assume every rail is ±20 V. Use a suitably rated,
low-capacitance probe and short ground spring; never ground-clip a negative rail.
Probe capacitance on gate/current-sense nodes can alter boost operation.

No reverse-battery protection, switch bypass, extra spare GPIO bank, fitted
headers or application firmware were added. Validate the assembled prototype.

References: [DESPI-C02 manufacturer product/manual](https://www.good-display.com/blank7.html?productId=516),
[ESP32-S3 ROM loader](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/advanced-topics/boot-mode-selection.html),
[TPS63031](https://www.ti.com/lit/ds/symlink/tps63031.pdf),
[zero-ohm link part](https://www.lcsc.com/product-detail/Chip-Resistor-Surface-Mount-UniOhm_0R-0R0-1_C17477.html).
