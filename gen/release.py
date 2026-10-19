"""Check assembly exports; produce orientation/mechanical references (KiCad Python)."""
import csv
import hashlib
import html
import json
from pathlib import Path
import pcbnew as P
from build_pcb import W, H, PANEL_AA

ROOT = Path(__file__).resolve().parent.parent
FAB = ROOT / 'fab'
b = P.LoadBoard(str(ROOT/'kicad/eink-pod.kicad_pcb'))
design = json.loads((ROOT/'kicad/design.json').read_text())
expected = {c['ref'] for c in design['components'] if c.get('lcsc') and not c.get('dnp')}
bom = list(csv.DictReader((FAB/'eink-pod-bom.csv').open()))
cpl = list(csv.DictReader((FAB/'eink-pod-cpl.csv').open()))
assert {r for row in bom for r in row['Designator'].split(',')} == expected
assert {r['Designator'] for r in cpl} == expected
assert len(cpl) == len(expected), 'Duplicate placement reference'
assert all(row['LCSC Part #'].startswith('C') and row['LCSC Part #'][1:].isdigit() for row in bom)
assert all(int(row['Qty']) == len(row['Designator'].split(',')) for row in bom)
fps = {f.GetReference():f for f in b.GetFootprints()}

def xy(pos):
    return P.ToMM(pos.x)-100, P.ToMM(pos.y)-100

def header(width=62,height=118):
    return [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}mm" height="{height}mm" viewBox="-10 -10 {width} {height}">',
            '<rect x="-10" y="-10" width="100%" height="100%" fill="white"/>',
            '<style>text{font-family:Arial,sans-serif;font-size:1.5px} .small{font-size:1.1px}</style>',
            f'<rect x="0" y="0" width="{W}" height="{H}" rx="3" fill="#f4f7f4" stroke="#222" stroke-width=".2"/>']

with (FAB/'assembly-pin1.csv').open('w',newline='') as f:
    out=csv.writer(f, lineterminator='\n')
    out.writerow(['Reference','Part','Side','KiCad angle','Pad1 x mm from left','Pad1 y mm from top','Pad1 net'])
    for ref in sorted(expected):
        fp=fps[ref]
        p=next((p for p in fp.Pads() if p.GetNumber() in ('1','A1')),None)
        if p:
            x,y=xy(p.GetPosition())
            out.writerow([ref,fp.GetValue(),'Front' if fp.GetLayer()==P.F_Cu else 'Back',fp.GetOrientationDegrees(),f'{x:.4f}',f'{y:.4f}',p.GetNetname()])
for front in (True,False):
    s=header()
    title='FRONT' if front else 'BACK (mirrored physical view)'
    s.append(f'<text x="0" y="-5">REV E — {title}</text>')
    s.append('<text x="0" y="-2.5">Red dot = pad 1 / USB A1; compare JLC preview</text>')
    def sx(x): return x if front else W-x
    for ref in sorted(expected):
        fp=fps[ref]
        if (fp.GetLayer()==P.F_Cu)!=front: continue
        for p in fp.Pads():
            if not p.GetNumber(): continue
            x,y=xy(p.GetPosition())
            bb=p.GetBoundingBox();w,h=P.ToMM(bb.GetWidth()),P.ToMM(bb.GetHeight())
            s.append(f'<rect x="{sx(x)-w/2:.3f}" y="{y-h/2:.3f}" width="{w:.3f}" height="{h:.3f}" fill="#c6c6c6"/>')
            if p.GetNumber() in ('1','A1'):
                s.append(f'<circle cx="{sx(x):.3f}" cy="{y:.3f}" r=".22" fill="#d02020"/>')
        x,y=xy(fp.GetPosition())
        s.append(f'<text class="small" text-anchor="middle" x="{sx(x):.3f}" y="{y:.3f}">{html.escape(ref)}</text>')
    s.append('</svg>');(FAB/('assembly-front.svg' if front else 'assembly-back.svg')).write_text('\n'.join(s))
s=header(95,120)
s += ['<text x="0" y="-5">REV E — FRONT VIEW — PRINT AT 100%</text>',
      '<rect x="6.4" y="0" width="29.2" height="59.2" fill="none" stroke="#086" stroke-width=".2"/>']
(ax0,ay0),(ax1,ay1)=PANEL_AA
s.append(f'<rect x="{ax0}" y="{ay0}" width="{ax1-ax0}" height="{ay1-ay0}" fill="none" stroke="#06a" stroke-width=".15"/>')
for x in (3.5,38.5):
    for y in (3.5,94.5):
        s.append(f'<circle cx="{x}" cy="{y}" r="1.35" fill="white" stroke="#222" stroke-width=".15"/>')
        s.append(f'<circle cx="{x}" cy="{y}" r="2.25" fill="none" stroke="#888" stroke-width=".1" stroke-dasharray=".5 .5"/>')
s.append('<circle cx="36.5" cy="61.5" r="7" fill="none" stroke="#b60" stroke-width=".2"/>')
for ref in ('SW1','SW2','SW3','SW4','SW5'):
    x,y=xy(fps[ref].GetPosition());s.append(f'<circle cx="{x}" cy="{y}" r="1.75" fill="none" stroke="#555" stroke-width=".15"/>')
for i,label in enumerate(['PCB 42 x 98 mm','Glass 29.2 x 59.2','Active 23.70 x 48.55','Holes: 2.7 mm NPTH','Boss/head OD max 4.5','Wheel OD 14','Overhang 1.50 nominal','Units: mm','Drawing-derived nominals','Physical dry-fit required']):
    s.append(f'<text x="46" y="{5+i*3}">{html.escape(label)}</text>')
s.append('<path d="M 0 103 H 20 M 0 102 V 104 M 20 102 V 104" fill="none" stroke="black" stroke-width=".2"/><text x="5" y="107">20 mm scale check</text>')
s.append('</svg>');(FAB/'mechanical.svg').write_text('\n'.join(s))
# One physical back-view map for unfitted headers, link pads and every probe.
debug_refs={r for r in fps if r.startswith('TP')} | {'J3','J5','R23','R24','R25'}
with (FAB/'debug-access.csv').open('w',newline='') as f:
    out=csv.writer(f,lineterminator='\n')
    out.writerow(['Reference','Pin','Net','X mm from front-left','Y mm from top','Access side'])
    for ref in sorted(debug_refs):
        for p in sorted(fps[ref].Pads(),key=lambda p:p.GetNumber()):
            x,y=xy(p.GetPosition())
            out.writerow([ref,p.GetNumber(),p.GetNetname(),f'{x:.4f}',f'{y:.4f}','Back'])
s=header(150,118)
s.append('<text x="0" y="-5">REV E — DEBUG ACCESS — BACK VIEW (mirrored)</text>')
for ref in sorted(debug_refs):
    for p in fps[ref].Pads():
        x,y=xy(p.GetPosition());x=W-x
        color='#06a' if ref in ('J3','J5') and p.GetNumber()=='1' else '#b20'
        s.append(f'<circle cx="{x:.3f}" cy="{y:.3f}" r=".45" fill="{color}"/>')
    x,y=xy(fps[ref].GetPosition())
    label_y=y+1.7 if ref in ('TP25','R23','R24','R25') else y-1.1
    s.append(f'<text class="small" text-anchor="middle" x="{W-x:.3f}" y="{label_y:.3f}">{ref}</text>')
ordered=sorted((r for r in debug_refs if r.startswith('TP')),key=lambda r:int(r[2:]))
for i,ref in enumerate(ordered):
    p=next(iter(fps[ref].Pads()))
    s.append(f'<text x="46" y="{i*3.0:.2f}">{ref}: {html.escape(p.GetNetname())}</text>')
for base,ref in [(0,'J3'),(26,'J5'),(58,'R23'),(70,'R24'),(82,'R25')]:
    s.append(f'<text x="92" y="{base}">{ref}: {html.escape(fps[ref].GetValue())}</text>')
    for i,p in enumerate(sorted(fps[ref].Pads(),key=lambda p:p.GetNumber())):
        s.append(f'<text x="92" y="{base+3+i*3}">{p.GetNumber()}: {html.escape(p.GetNetname())}</text>')
s.append('<text x="46" y="99">TP22–TP28: high-voltage / switching probes. Use local GND.</text>')
s.append('<text x="46" y="103">Blue = header pin 1. J3/J5 NOT FITTED; J3 uses EN/IO0.</text>')
s.append('</svg>');(FAB/'debug-map.svg').write_text('\n'.join(s))
paths=['kicad/eink-pod.kicad_pcb','kicad/eink-pod.kicad_sch','kicad/erc.rpt','kicad/drc.rpt','kicad/drc-all.rpt','kicad/verification.txt','fab/eink-pod-gerbers.zip','fab/eink-pod-bom.csv','fab/eink-pod-cpl.csv','fab/debug-access.csv','fab/debug-map.svg']
(FAB/'manifest-sha256.txt').write_text('\n'.join(hashlib.sha256((ROOT/p).read_bytes()).hexdigest()+'  '+p for p in paths)+'\n')
print(f'PASS: BOM/CPL match {len(expected)} assembled components; orientation maps and SHA256 manifest generated')
