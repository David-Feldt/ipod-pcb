"""Revision E debug access: names, net assignments and board-local placement."""
# ref: (net, x, y, pad diameter mm). Existing TP1-TP5 are retained.
POINTS = {
 'TP6': ('VBAT_RAW',3,88.7,1.0),
 'TP7': ('VBUS',24,94,1.0),
 'TP8': ('VSYS',34,83.5,1.0),
 'TP9': ('GND',34,70.5,1.5),
 'TP10': ('REG_3V3',23,73,1.0),
 'TP11': ('PWR_EN',20.5,70.5,1.0),
 'TP12': ('CHG_STAT',18,73,1.0),
 'TP13': ('VOL_SENSE',23,63.5,1.0),
 'TP14': ('VBAT_SENSE',9.5,42,1.0),
 'TP15': ('USB_PRESENT_N',13.5,42,1.0),
 'TP16': ('EN',20,38.5,1.0),
 'TP17': ('BOOT',22,43,1.0),
 'TP18': ('GND',18.5,43,1.5),
 'TP19': ('SPARE_GPIO8',8,51,1.0),
 'TP20': ('SPARE_GPIO14',11,51,1.0),
 'TP21': ('SPARE_GPIO15',14,51,1.0),
 'TP22': ('EPD_SW',12.4,11,0.6),
 'TP23': ('EPD_GDR',8.9,13.5,0.6),
 'TP24': ('EPD_RESE',8.7,10,0.6),
 'TP25': ('EPD_PREVGH',19,16.3,0.6),
 'TP26': ('EPD_PREVGL',13.5,22,0.6),
 'TP27': ('EPD_VSH1',25.6,21,0.6),
 'TP28': ('EPD_VSL',29.6,21,0.6),
 'TP29': ('GND',21,16.3,1.5),
 'TP30': ('GND',12,24,1.5),
}
PROGRAM = ['GND', '+3V3', 'TXD0', 'RXD0', 'EN', 'BOOT']
# Good Display DESPI-C02 user manual pins 1..8; connect by signal labels.
DISPLAY = ['EPD_BUSY','EPD_RST','EPD_DC','EPD_CS','EPD_SCK','EPD_MOSI','GND','+3V3']
# Keep high-impedance/switching-node probes local, with no autorouted branches.
PROBE_ANCHORS = {'TP22':('Q2',3), 'TP23':('Q2',1), 'TP24':('R22',1),
                 'TP25':('C30',1), 'TP26':('C31',1), 'TP27':('C32',1), 'TP28':('C34',1)}
