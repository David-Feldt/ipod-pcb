# E-ink Remote — hardware design

A pocket Spotify remote: ESP32-S3 (Wi-Fi), 2.13" portrait e-ink, 5 iPod-style
buttons, a volume thumbwheel, LiPo battery, USB-C (charging + firmware). The board
is 42 × 98 mm with four M2.5 mounting holes. Music plays on your phone, computer or
speaker; this device shows what's playing and controls it through Spotify's
official Web API. No audio hardware on board.

## Decisions

| Area | Choice | Why |
|---|---|---|
| MCU | ESP32-S3-MINI-1-N8 module | Pre-certified Wi-Fi + antenna, native USB, no RF layout to get wrong |
| Antenna | At the right board edge, Espressif's full keepout (no copper on any layer) | Wi-Fi range is now the core feature |
| Display | GoodDisplay GDEY0213B74, 2.13" 122x250 portrait, bare panel on 24-pin FPC | Zero power while showing now-playing; room for album art plus title, artist and volume |
| Volume | Alps RK10J11R0A0H 10k thumbwheel pot on the back, knob edge overhangs the right board edge by ~1.5 mm | Bluetooth-speaker style wheel; powered from a GPIO only while read |
| Mounting | Four 2.7 mm NPTH (M2.5) holes, 3.5 mm from each corner | Board is wide enough that the holes clear the display glass |
| Power | USB-C -> MCP73831 charger -> LiPo; P-FET power path; AP2112K 3.3V LDO | Simple, cheap, JLC-assemblable; LDO covers ~350 mA Wi-Fi bursts |
| Bulk caps | 22 µF + 10 µF + 100 nF at the module, 10 µF at the LDO | Wi-Fi TX current spikes |
| On/off | Slide switch on LDO EN; Q4/Q5 disconnect battery sensing when 3.3 V is off | Avoids feeding an unpowered ADC through the battery divider |
| USB detection | Q3 open-drain output, R8 pulled up to 3.3 V | Active-low detection with full logic swing and no DC path from VBUS into the GPIO |
| Input | 5 tactile buttons in an iPod cross: MENU / PREV / NEXT / PLAY + centre SELECT | All RTC GPIOs, any can wake from deep sleep |
| No audio / SD | Removed | Remote only; playback happens on the Spotify device |

## Power budget (estimate)

| State | Current |
|---|---|
| Awake, Wi-Fi connected, polling now-playing | ~40–100 mA (Wi-Fi modem sleep between requests) |
| Screen showing now-playing, ESP32 in deep sleep | ~0.1 mA (e-ink keeps the image with no power) |
| Switch off | To be measured; includes charger, regulator and MOSFET leakage |

The firmware should wake on a button press, reconnect to Wi-Fi (~1–2 s), act,
refresh the screen, and go back to sleep after ~30 s idle. With a 1000 mAh cell
that gives weeks of normal use. Staying awake continuously gives ~10–20 hours.

## GPIO map (ESP32-S3-MINI-1)

| Function | GPIO | Notes |
|---|---|---|
| VBAT_SENSE | 1 | ADC1_CH0, switched 1M/1M divider; VBAT = 2 × calibrated ADC voltage; wait at least 250 ms after power-on |
| USB_PRESENT_N | 2 | Low = USB plugged in; high = absent. Disable internal pulls; R8 supplies the pull-up |
| VOL_SENSE | 4 | ADC1_CH3, thumbwheel wiper, 10 nF to GND |
| VOL_EN | 5 | Drive high to power the thumbwheel, low otherwise |
| BTN_MENU | 9 | to GND, internal pull-up, wake |
| BTN_PLAY | 10 | to GND, internal pull-up, wake |
| BTN_PREV | 11 | to GND, internal pull-up, wake |
| BTN_NEXT | 12 | to GND, internal pull-up, wake |
| BTN_SEL | 13 | to GND, internal pull-up, wake |
| EPD_SCK | 16 | |
| EPD_MOSI | 17 | |
| EPD_CS | 18 | |
| USB_D- / USB_D+ | 19 / 20 | native USB |
| EPD_DC | 21 | |
| UART TX / RX | 43 / 44 | test pads |
| EPD_RST | 47 | |
| EPD_BUSY | 48 | |
| BOOT | 0 | button to GND |
| spare | 6–8, 14, 15, 38–42 | free for later |
| avoid | 3, 45, 46 (strapping), 26–37 (module flash) | |

## Display connection

The panel sits on the front, face up and portrait, with its FPC tail edge flush with
the board's top edge. The glass is 29.2 × 59.2 mm, and the 23.7 × 48.55 mm active area
starts 8.0 mm below the tail edge. The 14.3 mm tail wraps over the top edge to J4 on
the back (cable entry 5.5 mm in from the edge). Per the datasheet drawing, the tail is
0.2 mm off centre toward pin 1, so J4 sits at x = 21.2 mm. J4 is a dual-contact
connector, so it mates whichever way the tail's contacts face. The tail's pin 1 lands
on the +X side (right, seen from the front), which is where J4's pin 1 is. The panel
uses GoodDisplay's standard 24-pin pinout and the same boost circuit as the 1.54-inch
panel, so the display electronics are unchanged.

## Revision C changes

- 2.13-inch GDEY0213B74 panel, portrait. Board grows from 40 × 78 to 42 × 98 mm.
  Back-side parts move 2 mm right, so the antenna stays at the right edge. The power
  and USB group and the front buttons move 20 mm down, below the panel.
- RV1 volume thumbwheel at the right edge, below the antenna keepout. The knob is
  plastic, and its metal lugs are outside the keepout. The pot is wired 1 = GND,
  2 = wiper (`VOL_SENSE`), 3 = `VOL_EN`; its frame and tabs are grounded. The
  enclosure needs a slot in the right wall at y ≈ 54–69 mm so the wheel edge
  can be reached.
- H1–H4: M2.5 mounting holes. The top-right hole is about 4 mm from the antenna
  keepout; use a nylon screw there.
- C36 10 nF filters the wiper for the ADC.

## Revision B circuit and layout changes

- Q3 (AO3400A) translates VBUS into an active-low GPIO signal. Its insulated gate
  is driven by VBUS; its drain is pulled up only to the switched 3.3 V rail.
- Q4 (AO3401A) switches the battery divider at its high side: source = VBAT,
  drain = VBAT_MEAS. R9 pulls its gate to VBAT when off; Q5 pulls that gate low
  while 3.3 V is present. R11 discharges Q5's gate with the rail off. The divider
  still reads the battery while USB powers the device. C5 and the divider have
  a nominal 50 ms time constant; allow at least 250 ms before reading.
- R12/R13 are 22 ohm USB series resistors near the MCU. The long USB run is an
  explicit parallel pair on F.Cu over In1.GND: 0.25 mm trace width and 0.15 mm
  edge gap, with short 0.15 mm neckdowns at USB-C. Each data line changes layers
  twice; the ESD array uses flow-through routing. Target differential impedance
  is 90 ohm. Use JLC04161H-7628 (1.6 mm, 0.2104 mm outer prepreg, 1 oz outer /
  0.5 oz inner copper) and have the fabricator confirm impedance; this is a
  geometry target, not a field-solver or TDR certification.
- C7 connects directly to U1's 3.3 V pad; C6/C9 are within 5 mm of that pad.
  Display boost switching paths and the Q2/R22 sense path are fixed before
  autorouting, rather than left to the general router.
- `gen/verify_board.py` checks sense topology, capacitor distances, preservation
  of the fixed routes, USB branch/via counts, and the ground reference plane.
  ERC, DRC and schematic parity remain required as well.

Bench checks still needed: USB operation in both plug orientations, off-state
current with/without USB, battery ADC calibration, low-battery Wi-Fi bursts,
charger/LDO temperatures, and display refresh. Use a protected 1S LiPo rated
for the 500 mA charge setting. Enclosure retention and display/antenna clearance
must be checked with the actual panel, cell and enclosure.

Reference documents: [ESP32-S3 hardware guidelines](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32s3/),
[AO3400A](https://www.aosmd.com/res/data_sheets/AO3400A.pdf),
[AO3401A](https://www.aosmd.com/res/data_sheets/AO3401A.pdf),
[JLCPCB stackups](https://jlcpcb.com/impedance).
