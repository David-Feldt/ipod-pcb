"""Generate a KiCad schematic from a Python netlist description.

Every connected pin gets a short wire stub and a net label at its end, so
connectivity is defined purely by net names. Unused pins get no-connect flags.
"""
import copy
import json
import math
import os
import uuid as _uuid

from sexpr import Sym, dumps, find, findall, parse

S = Sym
GRID = 1.27
STUB = 2.54
KICAD_SYMBOLS = os.environ.get(
    "KICAD_SYMBOLS", os.path.expanduser("~/Applications/KiCad/KiCad.app/Contents/SharedSupport/symbols"))


def uid():
    return str(_uuid.uuid4())


def snap(v):
    return round(round(v / GRID) * GRID, 4)


class Lib:
    def __init__(self, extra_dirs=()):
        self.cache = {}
        self.dirs = list(extra_dirs) + [KICAD_SYMBOLS]

    def _file(self, lib):
        for d in self.dirs:
            p = os.path.join(d, lib + ".kicad_sym")
            if os.path.exists(p):
                return p
        raise FileNotFoundError(lib)

    def _lib(self, lib):
        if lib not in self.cache:
            with open(self._file(lib)) as f:
                root = parse(f.read())[0]
            self.cache[lib] = {s[1]: s for s in findall(root, "symbol")}
        return self.cache[lib]

    def get(self, lib_id):
        """Return a flattened symbol definition named 'Lib:Name'."""
        lib, name = lib_id.split(":", 1)
        syms = self._lib(lib)
        node = copy.deepcopy(syms[name])
        ext = find(node, "extends")
        if ext:
            parent = copy.deepcopy(self.get(lib + ":" + ext[1]))
            pname = parent[1].split(":", 1)[1]
            # child's properties override parent's
            child_props = {p[1]: p for p in findall(node, "property")}
            new = [S("symbol"), lib_id]
            for c in parent[2:]:
                if isinstance(c, list) and c[0] == "property" and c[1] in child_props:
                    new.append(child_props.pop(c[1]))
                elif isinstance(c, list) and c[0] == "symbol":
                    c = copy.deepcopy(c)
                    c[1] = c[1].replace(pname + "_", name + "_", 1)
                    new.append(c)
                else:
                    new.append(c)
            for p in child_props.values():
                new.insert(2, p)
            return new
        node[1] = lib_id
        return node


def symbol_pins(sym, unit=1):
    pins = []
    for sub in findall(sym, "symbol"):
        parts = sub[1].rsplit("_", 2)
        su = int(parts[-2])
        if su not in (0, unit):
            continue
        for p in findall(sub, "pin"):
            at = find(p, "at")
            num = find(p, "number")[1]
            name = find(p, "name")[1]
            hidden = any(c == "hide" for c in p) or (find(p, "hide") is not None
                                                     and find(p, "hide")[1] == "yes")
            pins.append(dict(number=str(num), name=str(name), x=float(at[1]),
                             y=float(at[2]), ang=float(at[3]) if len(at) > 3 else 0.0,
                             type=str(p[1]), hidden=hidden))
    return pins


def xform(px, py, ox, oy, rot, mirror=None):
    # library coords are y-up; schematic is y-down
    x, y = px, -py
    if mirror == "y":
        x = -x
    elif mirror == "x":
        y = -y
    r = math.radians(rot)
    c, s = round(math.cos(r)), round(math.sin(r))
    xr = x * c + y * s
    yr = -x * s + y * c
    return ox + xr, oy + yr


class Schematic:
    def __init__(self, project, title, lib, paper="A2"):
        self.project = project
        self.title = title
        self.lib = lib
        self.paper = paper
        self.root_uuid = uid()
        self.lib_symbols = {}
        self.items = []
        self.components = []
        self.nets = {}
        self._pwr = 0

    def text(self, s, x, y, size=2.54):
        self.items.append([S("text"), s, [S("exclude_from_sim"), S("no")],
                           [S("at"), x, y, 0],
                           [S("effects"), [S("font"), [S("size"), size, size], [S("bold"), S("yes")]],
                            [S("justify"), S("left"), S("bottom")]],
                           [S("uuid"), uid()]])

    def add(self, ref, lib_id, value, footprint, x, y, nets, rot=0, mirror=None,
            unit=1, lcsc=None, mpn=None, dnp=False, fields=None, nc_rest=True):
        sym = self.lib.get(lib_id)
        self.lib_symbols[lib_id] = sym
        pins = symbol_pins(sym, unit)
        x, y = snap(x), snap(y)
        sid = uid()
        known = {p["number"] for p in pins}
        for n in nets:
            if str(n) not in known:
                raise KeyError("%s: no pin %s on %s (pins: %s)" % (
                    ref, n, lib_id, sorted(known)))
        props = [("Reference", ref, False), ("Value", value, False),
                 ("Footprint", footprint, True), ("Datasheet", "", True)]
        if lcsc:
            props.append(("LCSC", lcsc, True))
        if mpn:
            props.append(("MPN", mpn, True))
        for k, v in (fields or {}).items():
            props.append((k, v, True))
        node = [S("symbol"), [S("lib_id"), lib_id], [S("at"), x, y, rot]]
        if mirror:
            node.append([S("mirror"), S(mirror)])
        node += [[S("unit"), unit], [S("exclude_from_sim"), S("no")],
                 [S("in_bom"), S("no") if ref.startswith(("#", "TP", "H")) else S("yes")],
                 [S("on_board"), S("no") if ref.startswith("#") else S("yes")],
                 [S("dnp"), S("yes") if dnp else S("no")], [S("uuid"), sid]]
        for i, (k, v, hide) in enumerate(props):
            eff = [S("effects"), [S("font"), [S("size"), 1.27, 1.27]]]
            if hide:
                eff.append([S("hide"), S("yes")])
            node.append([S("property"), k, v, [S("at"), x, y - 2.54 * (2 - i) if i < 2 else y, 0], eff])
        for p in pins:
            node.append([S("pin"), p["number"], [S("uuid"), uid()]])
        node.append([S("instances"), [S("project"), self.project,
                                      [S("path"), "/" + self.root_uuid,
                                       [S("reference"), ref], [S("unit"), unit]]]])
        self.items.append(node)

        padnets = {}
        seen_ends = set()
        for p in pins:
            ex, ey = xform(p["x"], p["y"], x, y, rot, mirror)
            ex, ey = round(ex, 4), round(ey, 4)
            net = nets.get(p["number"])
            if net is None:
                if p["type"] in ("no_connect",) or p["hidden"] or not nc_rest:
                    continue
                if (ex, ey) in seen_ends:
                    continue
                self.items.append([S("no_connect"), [S("at"), ex, ey], [S("uuid"), uid()]])
                seen_ends.add((ex, ey))
                continue
            padnets[p["number"]] = net
            self.nets.setdefault(net, []).append((ref, p["number"]))
            if (ex, ey) in seen_ends:
                continue  # stacked pins share one label
            seen_ends.add((ex, ey))
            a = math.radians(p["ang"] + 180)
            vx, vy = xform(math.cos(a), math.sin(a), 0, 0, rot, mirror)
            vx, vy = round(vx), round(vy)
            sx, sy = round(ex + vx * STUB, 4), round(ey + vy * STUB, 4)
            self.items.append([S("wire"), [S("pts"), [S("xy"), ex, ey], [S("xy"), sx, sy]],
                               [S("stroke"), [S("width"), 0], [S("type"), S("default")]],
                               [S("uuid"), uid()]])
            lang = {(1, 0): 0, (-1, 0): 180, (0, -1): 90, (0, 1): 270}[(vx, vy)]
            just = S("right") if lang in (180, 270) else S("left")
            self.items.append([S("global_label"), net, [S("shape"), S("passive")],
                               [S("at"), sx, sy, lang], [S("fields_autoplaced"), S("yes")],
                               [S("effects"), [S("font"), [S("size"), 1.0, 1.0]],
                                [S("justify"), just]],
                               [S("uuid"), uid()],
                               [S("property"), "Intersheetrefs", "${INTERSHEET_REFS}",
                                [S("at"), sx, sy, 0],
                                [S("effects"), [S("font"), [S("size"), 1.0, 1.0]], [S("hide"), S("yes")]]]])
        if not ref.startswith("#"):
            self.components.append(dict(ref=ref, value=value, footprint=footprint,
                                        uuid=sid, lcsc=lcsc, mpn=mpn, dnp=dnp,
                                        pads=padnets))
        return sid

    def pwr_flag(self, net, x, y):
        self._pwr += 1
        self.add("#FLG%02d" % self._pwr, "power:PWR_FLAG", "PWR_FLAG", "", x, y,
                 {"1": net})

    def save(self, path):
        root = [S("kicad_sch"), [S("version"), 20231120], [S("generator"), "eeschema"],
                [S("generator_version"), "8.0"], [S("uuid"), self.root_uuid],
                [S("paper"), self.paper],
                [S("title_block"), [S("title"), self.title], [S("rev"), "D"],
                 [S("company"), "E-ink Remote"]],
                [S("lib_symbols")] + list(self.lib_symbols.values())]
        root += self.items
        root.append([S("sheet_instances"), [S("path"), "/", [S("page"), "1"]]])
        with open(path, "w") as f:
            f.write(dumps(root) + "\n")

    def save_design(self, path):
        with open(path, "w") as f:
            json.dump(dict(root_uuid=self.root_uuid, components=self.components,
                           nets=self.nets), f, indent=1)
