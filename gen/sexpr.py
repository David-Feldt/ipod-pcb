"""Minimal KiCad S-expression parser / writer."""
import re

_TOKEN = re.compile(r'\s*(?:(\()|(\))|"((?:[^"\\]|\\.)*)"|([^\s()"]+))', re.S)


class Sym(str):
    """An unquoted atom (keyword / number)."""


def parse(text):
    stack = [[]]
    pos = 0
    n = len(text)
    while pos < n:
        m = _TOKEN.match(text, pos)
        if not m:
            if text[pos:].strip() == "":
                break
            raise ValueError("bad sexpr at %d: %r" % (pos, text[pos:pos + 40]))
        pos = m.end()
        lp, rp, qs, atom = m.groups()
        if lp:
            stack.append([])
        elif rp:
            item = stack.pop()
            stack[-1].append(item)
        elif qs is not None:
            stack[-1].append(qs.replace('\\"', '"').replace("\\\\", "\\"))
        elif atom is not None:
            stack[-1].append(Sym(atom))
    return stack[0]


def _fmt_atom(a):
    if isinstance(a, Sym):
        return str(a)
    if isinstance(a, bool):
        return "yes" if a else "no"
    if isinstance(a, (int, float)):
        if isinstance(a, float):
            s = ("%.4f" % a).rstrip("0").rstrip(".")
            return "0" if s in ("-0", "") else s
        return str(a)
    s = str(a).replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")
    return '"%s"' % s


def dumps(node, indent=0):
    if not isinstance(node, list):
        return _fmt_atom(node)
    simple = all(not isinstance(c, list) for c in node)
    if simple:
        return "(" + " ".join(_fmt_atom(c) for c in node) + ")"
    pad = "\t" * (indent + 1)
    parts = []
    head = []
    for c in node:
        if isinstance(c, list):
            break
        head.append(_fmt_atom(c))
    out = "(" + " ".join(head)
    for c in node[len(head):]:
        out += "\n" + pad + dumps(c, indent + 1)
    out += "\n" + "\t" * indent + ")"
    return out


def find(node, key):
    for c in node:
        if isinstance(c, list) and c and c[0] == key:
            return c
    return None


def findall(node, key):
    return [c for c in node if isinstance(c, list) and c and c[0] == key]
