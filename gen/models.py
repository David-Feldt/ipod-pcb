"""Simple VRML 3D models for parts KiCad doesn't ship, plus the e-paper panel.

Coordinates are written in mm in *footprint* orientation (x right, y down,
z up from the board surface) and converted to VRML's y-up here. Footprints
reference these files with scale 1/2.54 (KiCad VRML units are 0.1 inch).
"""
import math
import os

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "kicad", "3d")


class Model:
    def __init__(self):
        self.boxes = {}  # color -> list of (x0, y0, z0, x1, y1, z1)

    def box(self, color, x0, y0, z0, x1, y1, z1):
        self.boxes.setdefault(color, []).append((min(x0, x1), min(y0, y1), min(z0, z1),
                                                 max(x0, x1), max(y0, y1), max(z0, z1)))

    def save(self, name):
        os.makedirs(OUT, exist_ok=True)
        parts = ["#VRML V2.0 utf8\n"]
        for color, boxes in self.boxes.items():
            pts, faces = [], []
            for x0, y0, z0, x1, y1, z1 in boxes:
                b = len(pts)
                # footprint y-down -> model y-up
                for z in (z0, z1):
                    for x, y in ((x0, y0), (x1, y0), (x1, y1), (x0, y1)):
                        pts.append((x, -y, z))
                # winding is reversed because y is flipped above, so normals point outward
                for f in ((0, 1, 2, 3), (4, 7, 6, 5), (0, 4, 5, 1), (1, 5, 6, 2), (2, 6, 7, 3), (3, 7, 4, 0)):
                    faces.append([b + i for i in f])
            r, g, bl = (int(color[i:i + 2], 16) / 255 for i in (1, 3, 5))
            shininess = 0.6 if color in METALS else 0.15
            parts.append(
                "Shape {\n appearance Appearance { material Material { diffuseColor %.3f %.3f %.3f "
                "specularColor %.2f %.2f %.2f shininess %.2f } }\n"
                " geometry IndexedFaceSet { solid FALSE\n  coord Coordinate { point [\n%s ] }\n"
                "  coordIndex [\n%s ] }\n}\n" % (
                    r, g, bl, shininess, shininess, shininess, shininess,
                    ",\n".join("   %.4f %.4f %.4f" % p for p in pts),
                    ",\n".join("   " + " ".join(map(str, f)) + " -1" for f in faces)))
        with open(os.path.join(OUT, name + ".wrl"), "w") as f:
            f.write("".join(parts))


SILVER, STEEL, BLACK, PCB, GOLD = "#c9c9cd", "#a7a9ad", "#1c1c1e", "#23283a", "#c9a227"
IVORY, AMBER, PAPER, GLASS, INK = "#ede6d3", "#c27a1c", "#efeee7", "#c8c9c3", "#161616"
METALS = {SILVER, STEEL, GOLD}


def esp32_mini():
    m = Model()
    m.box(PCB, -7.7, -9.8, 0, 7.7, 10.3, 0.8)            # module PCB, antenna end at -y
    m.box(SILVER, -7.4, -4.1, 0.8, 7.4, 10.0, 2.4)       # RF shield can
    for i in range(5):                                    # meander antenna hint
        m.box(GOLD, -6 + i * 2.6, -9.0, 0.8, -5.2 + i * 2.6, -5.0, 0.82)
    m.save("ESP32-S3-MINI-1")


def usb_c():
    m = Model()
    m.box(SILVER, -4.47, -3.65, 0.0, 4.47, 3.65, 3.2)
    m.box(BLACK, -3.9, 3.62, 0.45, 3.9, 3.7, 2.75)       # mouth at +y
    m.save("USB_C_HRO_TYPE-C-31-M-12")


def jst_ph_sm4():
    m = Model()
    m.box(IVORY, -3.95, -2.3, 0.0, 3.95, 4.4, 4.5)
    m.box(BLACK, -2.6, 4.35, 0.8, 2.6, 4.45, 3.6)        # wire entry at +y
    m.box(STEEL, -3.9, 2.2, 0, -2.8, 3.6, 0.3)           # mounting tabs
    m.box(STEEL, 2.8, 2.2, 0, 3.9, 3.6, 0.3)
    m.save("JST_PH_S2B-PH-SM4-TB")


def msk12c02():
    m = Model()
    m.box(SILVER, -3.35, -1.35, 0.0, 3.35, 1.35, 1.4)
    m.box(BLACK, -0.75, 1.35, 0.35, 0.75, 2.9, 1.05)     # actuator at +y
    m.save("SW_SPDT_MSK12C02")


def fpc24():
    m = Model()
    m.box(IVORY, -7.6, -1.2, 0.0, 7.6, 3.0, 1.6)
    m.box(BLACK, -7.4, 1.9, 1.25, 7.4, 3.9, 2.0)         # hinged lid on the cable side (+y)
    m.save("FPC-05FB-24PH20")


def epaper():
    """GDEY0154D67 on the front, origin at panel centre, tail edge at y = -18.66."""
    m = Model()
    hw, hh, t = 15.9, 18.66, 1.0
    m.box(GLASS, -hw, -hh, 0, hw, hh, t)
    ax0, ax1, ay0, ay1 = -13.5, 13.5, -10.74, 16.26      # 27 x 27 mm active area
    m.box(PAPER, ax0, ay0, t, ax1, ay1, t + 0.01)
    z0, z1 = t + 0.01, t + 0.02

    # mock "now playing" UI (firmware rotates 180 so it reads upright)
    m.box(INK, ax0 + 1.0, ay0 + 0.8, z0, ax0 + 6.5, ay0 + 1.6, z1)      # "Now playing"
    m.box(INK, ax1 - 4.0, ay0 + 0.7, z0, ax1 - 1.2, ay0 + 1.7, z1)      # battery
    m.box(INK, ax1 - 1.2, ay0 + 1.0, z0, ax1 - 0.9, ay0 + 1.4, z1)
    # album art: ordered-dither of a radial "record" gradient
    bayer = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]
    cx, cy, size, px = 0.0, ay0 + 10.0, 16.0, 0.5
    n = int(size / px)
    for i in range(n):
        for j in range(n):
            x = cx - size / 2 + i * px
            y = cy - size / 2 + j * px
            r = math.hypot(x + px / 2 - cx, y + px / 2 - cy)
            shade = 0.9 if r > 7.6 else (0.15 + 0.35 * (0.5 + 0.5 * math.sin(r * 4.2))) if r > 1.4 else 0.95
            if r < 0.5:
                shade = 0.0
            if (1 - shade) * 16 > bayer[j % 4][i % 4]:
                m.box(INK, x, y, z0, x + px, y + px, z1)
    # title / artist "text" blocks
    for x0, x1 in ((-11.5, -6.0), (-5.2, -1.5), (-0.7, 5.5)):
        m.box(INK, x0, ay0 + 19.0, z0, x1, ay0 + 20.3, z1)
    for x0, x1 in ((-11.5, -7.5), (-6.8, -2.0)):
        m.box(INK, x0, ay0 + 21.3, z0, x1, ay0 + 22.1, z1)
    # progress bar
    m.box(INK, -11.5, ay0 + 24.0, z0, 11.5, ay0 + 24.15, z1)
    m.box(INK, -11.5, ay0 + 23.7, z0, -2.5, ay0 + 24.45, z1)
    m.box(INK, -2.9, ay0 + 23.45, z0, -2.1, ay0 + 24.7, z1)

    # FPC tail: out of the panel, over the board's top edge, back to J4
    tw = 6.25
    edge = -hh
    m.box(AMBER, -tw, edge - 0.8, 0.15, tw, edge, 0.25)
    m.box(AMBER, -tw, edge - 0.9, -2.45, tw, edge - 0.8, 0.25)
    m.box(AMBER, -tw, edge - 0.9, -2.45, tw, edge + 7.6, -2.35)
    m.save("EPD_GDEY0154D67")


def main():
    for f in (esp32_mini, usb_c, jst_ph_sm4, msk12c02, fpc24, epaper):
        f()
    print("models written to", os.path.normpath(OUT))


if __name__ == "__main__":
    main()
