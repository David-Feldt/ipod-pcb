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
        self.shapes = {}  # color -> list of (polygon [(x, y), ...] counter-clockwise on screen, z0, z1)

    def prism(self, color, poly, z0, z1):
        self.shapes.setdefault(color, []).append((poly, min(z0, z1), max(z0, z1)))

    def box(self, color, x0, y0, z0, x1, y1, z1):
        x0, x1, y0, y1 = min(x0, x1), max(x0, x1), min(y0, y1), max(y0, y1)
        self.prism(color, [(x0, y0), (x1, y0), (x1, y1), (x0, y1)], z0, z1)

    def disc(self, color, cx, cy, r, z0, z1, n=48, knurl=0.0):
        """Cylinder; knurl > 0 alternates the radius to suggest a knurled edge."""
        pts = []
        for i in range(n):
            a = 2 * math.pi * i / n
            rr = r - (knurl if i % 2 else 0)
            pts.append((cx + rr * math.cos(a), cy + rr * math.sin(a)))
        self.prism(color, pts, z0, z1)

    def save(self, name):
        os.makedirs(OUT, exist_ok=True)
        parts = ["#VRML V2.0 utf8\n"]
        for color, shapes in self.shapes.items():
            pts, faces = [], []
            for poly, z0, z1 in shapes:
                b, n = len(pts), len(poly)
                # footprint y-down -> model y-up
                for z in (z0, z1):
                    for x, y in poly:
                        pts.append((x, -y, z))
                # winding is reversed because y is flipped above, so normals point outward
                faces.append([b + i for i in range(n)])
                faces.append([b + n + i for i in reversed(range(n))])
                for i in range(n):
                    j = (i + 1) % n
                    faces.append([b + i, b + n + i, b + n + j, b + j])
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
GRAY = "#5a5a5e"
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
    """GDEY0213B74 on the front, portrait, origin at glass centre, tail edge at y = -29.6."""
    m = Model()
    hw, hh, t = 14.6, 29.6, 1.0
    m.box(GLASS, -hw, -hh, 0, hw, hh, t)
    ax0, ax1, ay0, ay1 = -11.85, 11.85, -hh + 7.95, -hh + 56.50   # 23.7 x 48.55 mm active area
    m.box(PAPER, ax0, ay0, t, ax1, ay1, t + 0.01)
    z0, z1 = t + 0.01, t + 0.02

    # mock "now playing" UI (firmware rotates 180 so it reads upright)
    m.box(INK, ax0 + 1.0, ay0 + 1.0, z0, ax0 + 6.5, ay0 + 1.8, z1)      # "Now playing"
    m.box(INK, ax1 - 4.0, ay0 + 0.9, z0, ax1 - 1.2, ay0 + 1.9, z1)      # battery
    m.box(INK, ax1 - 1.2, ay0 + 1.2, z0, ax1 - 0.9, ay0 + 1.6, z1)
    # album art: ordered-dither of a radial "record" gradient
    bayer = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]
    cx, cy, size, px = 0.0, ay0 + 13.5, 20.0, 0.5
    n = int(size / px)
    for i in range(n):
        for j in range(n):
            x = cx - size / 2 + i * px
            y = cy - size / 2 + j * px
            r = math.hypot(x + px / 2 - cx, y + px / 2 - cy)
            shade = 0.9 if r > 9.6 else (0.15 + 0.35 * (0.5 + 0.5 * math.sin(r * 3.6))) if r > 1.8 else 0.95
            if r < 0.6:
                shade = 0.0
            if (1 - shade) * 16 > bayer[j % 4][i % 4]:
                m.box(INK, x, y, z0, x + px, y + px, z1)
    # title / artist / album "text" blocks
    for x0, x1 in ((-10.0, -4.5), (-3.7, 0.0), (0.8, 7.0)):
        m.box(INK, x0, ay0 + 26.5, z0, x1, ay0 + 28.0, z1)
    for x0, x1 in ((-10.0, -6.0), (-5.3, -0.5)):
        m.box(INK, x0, ay0 + 29.5, z0, x1, ay0 + 30.4, z1)
    for x0, x1 in ((-10.0, -3.0), (-2.3, 2.5)):
        m.box(INK, x0, ay0 + 31.8, z0, x1, ay0 + 32.6, z1)
    # progress bar + times
    m.box(INK, -10.0, ay0 + 36.0, z0, 10.0, ay0 + 36.15, z1)
    m.box(INK, -10.0, ay0 + 35.7, z0, -2.0, ay0 + 36.45, z1)
    m.box(INK, -2.4, ay0 + 35.4, z0, -1.6, ay0 + 36.75, z1)
    m.box(INK, -10.0, ay0 + 37.6, z0, -7.6, ay0 + 38.3, z1)
    m.box(INK, 7.6, ay0 + 37.6, z0, 10.0, ay0 + 38.3, z1)
    # volume bar
    m.box(INK, -10.0, ay0 + 42.0, z0, -9.0, ay0 + 43.0, z1)
    m.box(INK, -8.2, ay0 + 42.3, z0, 2.0, ay0 + 42.7, z1)
    m.box(INK, 2.0, ay0 + 42.45, z0, 10.0, ay0 + 42.55, z1)

    # FPC tail: out of the panel, over the board's top edge, back to J4 (0.2 mm toward pin 1, +x)
    tx0, tx1 = 0.2 - 6.25, 0.2 + 6.25
    edge = -hh
    m.box(AMBER, tx0, edge - 0.8, 0.15, tx1, edge, 0.25)
    m.box(AMBER, tx0, edge - 0.9, -2.45, tx1, edge - 0.8, 0.25)
    m.box(AMBER, tx0, edge - 0.9, -2.45, tx1, edge + 7.6, -2.35)
    m.save("EPD_GDEY0213B74")


def rk10j():
    """Alps RK10J1xR thumbwheel in footprint coordinates: knob centre (0, -2.40)."""
    m = Model()
    m.box(STEEL, -7.0, -6.0, 0.0, 7.0, 5.6, 0.25)                       # metal frame
    m.box(STEEL, -7.0, -5.6, 0.0, -5.0, -4.2, 0.25)                     # side lugs
    m.box(STEEL, 5.0, -5.6, 0.0, 7.0, -4.2, 0.25)
    m.box(BLACK, -5.2, -1.0, 0.25, 5.2, 5.4, 0.95)                      # resistor housing
    for x in (-4, -2, 0, 2, 4):                                         # terminals
        m.box(SILVER, x - 0.5, 5.4, 0.0, x + 0.5, 7.2, 0.15)
    m.disc(BLACK, 0, -2.4, 7.0, 1.0, 2.6, n=96, knurl=0.25)            # knurled knob
    m.disc(GRAY, 0, -2.4, 1.4, 2.6, 2.65)                              # hub
    m.save("Alps_RK10J1xR")


def main():
    for f in (esp32_mini, usb_c, jst_ph_sm4, msk12c02, fpc24, epaper, rk10j):
        f()
    print("models written to", os.path.normpath(OUT))


if __name__ == "__main__":
    main()
