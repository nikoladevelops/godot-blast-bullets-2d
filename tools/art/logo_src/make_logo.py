"""The BlastBullets2D logo, black and white like the original.

1. showcase/logo.svg: the original composition (its bullet and blast paths,
   verbatim) with the VT323 title converted to paths (the original named the
   font, so it fell back to another font wherever VT323 was not installed).
2. showcase/logo.png: that SVG at 2x, lit in EffectCraft (a soft white glow and a
   light sweep over the bullet), finished in PhotoCraft (fine monochrome grain
   and a vignette), scaled to 1600 px.

    python3 tools/art/logo_src/make_logo.py
"""
import os
import re
import subprocess

from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
from fontTools.ttLib import TTFont

HERE = os.path.dirname(os.path.abspath(__file__))
SHOWCASE = os.path.normpath(os.path.join(HERE, "..", "..", "..", "showcase"))
W, H = 1481.03, 653.14
TEXT, SIZE, BASELINE, STROKE = "BLAST BULLETS 2D", 150.88, 219.66, 4


def title_path():
    font = TTFont(os.path.join(HERE, "VT323.ttf"))
    gs, cmap, upm = font.getGlyphSet(), font.getBestCmap(), font["head"].unitsPerEm
    s = SIZE / upm
    width = sum(gs[cmap[ord(c)]].width for c in TEXT) * s
    pen = SVGPathPen(gs, ntos=lambda v: ("%.2f" % v).rstrip("0").rstrip("."))
    x = (W - width) / 2
    for c in TEXT:
        g = cmap[ord(c)]
        gs[g].draw(TransformPen(pen, (s, 0, 0, -s, x, BASELINE)))
        x += gs[g].width * s
    return pen.getCommands()


def art_paths():
    """The original bullet, its dark band lines and the blast, in drawing order."""
    src = open(os.path.join(HERE, "original_logo.svg")).read()
    return re.findall(r'<path class="(cls-1|cls-2)" d="([^"]+)"', src)


def build_svg(background=True, name="logo.svg", folder=SHOWCASE):
    fills = {"cls-1": "#000", "cls-2": "#fff"}
    art = "".join(f'<path fill="{fills[c]}" d="{d}"/>' for c, d in art_paths())
    title = title_path()
    svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">'
           + (f'<rect width="{W}" height="{H}" fill="#000"/>' if background else "") + f'{art}'
           f'<path fill="#fff" stroke="#fff" stroke-width="{STROKE}" stroke-linejoin="miter" d="{title}"/></svg>\n')
    path = os.path.join(folder, name)
    with open(path, "w") as f:
        f.write(svg)
    return path


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise SystemExit("failed: %s\n%s%s" % (" ".join(cmd[:6]), r.stdout[-1500:], r.stderr[-1500:]))
    return r.stdout


def main():
    svg = build_svg()
    work = os.path.join(HERE, "work")
    os.makedirs(work, exist_ok=True)
    # The effects work on the art alone (transparent background): the glow and the
    # sweep must touch the white shapes only, never the black around them.
    base = os.path.join(work, "base.png")
    run(["rsvg-convert", "-z", "2", build_svg(False, "art.svg", work), "-o", base])
    # EffectCraft: glow and a light sweep across the bullet, kept black and white.
    jsx = os.path.join(work, "logo.jsx")
    with open(jsx, "w") as f:
        f.write(f'''
var item = app.project.importFile(new ImportOptions(new File("{base}")));
var comp = app.project.items.addComp("Logo", {int(W * 2)}, {int(H * 2)}, 1, 1, 30);
comp.bgColor = [0, 0, 0];
var layer = comp.layers.add(item);
var glow = layer.property("Effects").addProperty("Glow");
glow.property("Glow Threshold").setValue(82);
glow.property("Glow Radius").setValue(22);
glow.property("Glow Intensity").setValue(0.3);
var sweep = layer.property("Effects").addProperty("CC Light Sweep");
sweep.property("Center").setValue([1700, 860]);
sweep.property("Direction").setValue(-28);
sweep.property("Width").setValue(120);
sweep.property("Sweep Intensity").setValue(35);
sweep.property("Edge Intensity").setValue(25);
sweep.property("Light Reception").setValue(1);
app.project.save(new File("{work}/logo.ecproj"));
''')
    ec = ["flatpak", "run", "--filesystem=" + HERE, "--command=effectcraft-cli", "ai.storyteller.effectcraft"]
    run(ec + ["script", jsx])
    lit = os.path.join(work, "lit.png")
    run(ec + ["render-frame", "--project", os.path.join(work, "logo.ecproj"), "--comp", "Logo", "--time", "0", "--out", lit])
    # PhotoCraft: fine monochrome grain and a soft vignette.
    finished = os.path.join(work, "finished.png")
    pc = ["flatpak", "run", "--filesystem=" + HERE, "--command=photocraft-cli", "ai.storyteller.photocraft"]
    run(pc + ["run", lit,
              "--cmd", "filter.noise.addNoise", "--params", '{"amount": 2.2, "distribution": "gaussian", "monochromatic": true, "seed": 7}',
              "--cmd", "filter.lensCorrection", "--params", '{"profile": "none", "correctDistortion": false, "correctVignette": false, "correctCA": false, "vignetteAmount": -35, "vignetteMidpoint": 45}',
              "--out", finished])
    out = os.path.join(SHOWCASE, "logo.png")
    run(["magick", finished, "-resize", "1600x", "-strip", out])
    icon = build_icon()
    run(["rsvg-convert", "-w", "512", icon, "-o", os.path.join(SHOWCASE, "icon.png")])
    print("wrote", svg, out, "and", icon)


def build_icon():
    """A square icon: the logo's bullet and blast, angled up, on black."""
    fills = {"cls-1": "#000", "cls-2": "#fff"}
    art = "".join(f'<path fill="{fills[c]}" d="{d}"/>' for c, d in art_paths())
    # The art spans about x 190..1290, y 283..523 in the logo: center it, scale it, tilt it.
    svg = ('<svg xmlns="http://www.w3.org/2000/svg" width="512" height="512" viewBox="0 0 512 512">'
           '<rect width="512" height="512" rx="96" fill="#000"/>'
           f'<g transform="translate(256 262) rotate(-32) scale(0.4) translate(-740 -403)">{art}</g></svg>\n')
    path = os.path.join(SHOWCASE, "icon.svg")
    with open(path, "w") as f:
        f.write(svg)
    return path


if __name__ == "__main__":
    main()
