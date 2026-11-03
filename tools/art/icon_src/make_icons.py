"""The editor icons of BulletFactory2D, BulletSpawner2D and LaserSpawner2D.
Each icon is designed as simple shapes (rectangles, circles, polygons); VectorCraft's
pathfinder unites them into one clean path and subtracts the cut-outs; the result is
minified into icons/<Class>.svg (16x16, Godot's 2D node blue).

    python3 tools/art/icon_src/make_icons.py
"""
import math
import os
import re
import subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(HERE, "..", "..", "..", "icons"))
BLUE = "#8da5f3"


def f(v):
    s = ("%.2f" % v).rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


def poly(pts):
    return '<path d="M' + "L".join(f"{f(x)} {f(y)}" for x, y in pts) + 'Z" fill="%s"/>' % BLUE


def circle(cx, cy, r):
    return f'<circle cx="{f(cx)}" cy="{f(cy)}" r="{f(r)}" fill="{BLUE}"/>'


def rect(x, y, w, h):
    return f'<rect x="{f(x)}" y="{f(y)}" width="{f(w)}" height="{f(h)}" fill="{BLUE}"/>'


def ogive(x0, w, tip_y, shoulder_y):
    """A bullet nose: vertical sides from shoulder_y up, curving to a point at tip_y."""
    pts = []
    for i in range(13):
        t = i / 12
        # Tangent ogive: x offset shrinks as a circle arc toward the tip.
        y = shoulder_y - (shoulder_y - tip_y) * t
        half = (w / 2) * math.sqrt(max(0.0, 1 - t * t))
        pts.append((x0 + w / 2 - half, y))
    right = [(x0 + w - (x - x0), y) for x, y in reversed(pts)]
    return pts + right


def capsule_out(cx, cy, angle, r0, r1, half_w):
    """A bullet pointing away from (cx, cy): body from r0 to r1, rounded nose at r1."""
    ux, uy = math.cos(angle), math.sin(angle)
    px, py = -uy, ux
    pts = [(cx + ux * r0 + px * half_w, cy + uy * r0 + py * half_w)]
    for i in range(9):  # half circle around the nose
        a = math.pi / 2 - i * math.pi / 8
        pts.append((cx + ux * r1 + (px * math.sin(a) + ux * math.cos(a)) * half_w,
                    cy + uy * r1 + (py * math.sin(a) + uy * math.cos(a)) * half_w))
    pts.append((cx + ux * r0 - px * half_w, cy + uy * r0 - py * half_w))
    return pts


ICONS = {}

# BulletFactory2D: three cartridges (ogive nose, gap, casing) on a production line.
# Every straight edge sits on a whole pixel: crisp at 16 px.
cart = []
for x0, tip in ((1, 3), (6, 1), (11, 3)):
    shoulder = tip + 3
    cart.append(poly(ogive(x0, 4, tip, shoulder)))
    cart.append(rect(x0, shoulder + 1, 4, 12 - (shoulder + 1)))
cart.append(rect(0, 13, 16, 2))
ICONS["BulletFactory2D"] = {"add": cart}

# BulletSpawner2D: a hollow emitter (Node2D style) firing eight bullets outward.
spawn = [circle(8, 8, 3)]
for k in range(8):
    spawn.append(poly(capsule_out(8, 8, -math.pi / 2 + k * math.pi / 4, 4.5, 6.8, 1.0)))
ICONS["BulletSpawner2D"] = {"add": spawn, "cut": [circle(8, 8, 1.5)]}

# LaserSpawner2D: a muzzle ring and a beam that tapers toward its tip, in a soft glow.
ICONS["LaserSpawner2D"] = {
    "add": [circle(4, 8, 3), poly([(6, 6.5), (16, 7), (16, 9), (6, 9.5)])],
    "cut": [circle(4, 8, 1.5)],
    "glow": [poly([(6, 4), (16, 5.5), (16, 10.5), (6, 12)])],
}


def vc(args):
    cmd = ["flatpak", "run", "--filesystem=" + HERE, "--command=vectorcraft-cli", "ai.storyteller.vectorcraft"] + args
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise SystemExit("vectorcraft failed: " + r.stdout[-800:] + r.stderr[-800:])


def svg_doc(body):
    return f'<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 16 16">{body}</svg>\n'


def paths_of(svg_text):
    return re.findall(r'<path[^>]* d="([^"]+)"', svg_text)


def compact(d):
    """Round to 0.01 and drop redundant spaces in VectorCraft's path data."""
    d = re.sub(r"-?\d+\.\d+", lambda m: f(float(m.group())), d)
    d = re.sub(r"\s*([MLCQZz])\s*", r"\1", d)
    return re.sub(r" -", "-", d)


def boolean(shapes, op, name):
    if op == "unite" and len(shapes) == 1:
        return paths_of(shapes[0])  # nothing to merge (a lone shape gives no result)
    src = os.path.join(HERE, name + "_in.svg")
    dst = os.path.join(HERE, name + "_out.svg")
    with open(src, "w") as fh:
        fh.write(svg_doc("".join(shapes)))
    vc(["run", "--in", src, "--cmd", "select.all", "--cmd", "object.pathfinder." + op,
        "--params", '{"removeRedundantPoints": true}', "--export", dst])
    with open(dst) as fh:
        return paths_of(fh.read())


os.makedirs(OUT, exist_ok=True)
for cls, spec in ICONS.items():
    united = boolean(spec["add"], "unite", cls + "_add")
    main = united
    if spec.get("cut"):
        main = boolean([f'<path d="{d}" fill="{BLUE}"/>' for d in united] + spec["cut"], "minusFront", cls + "_cut")
    body = ""
    if spec.get("glow"):
        glow = boolean(spec["glow"], "unite", cls + "_glow")
        body += "".join(f'<path fill="{BLUE}" fill-opacity=".4" d="{compact(d)}"/>' for d in glow)
    body += "".join(f'<path fill="{BLUE}" d="{compact(d)}"/>' for d in main)
    with open(os.path.join(OUT, cls + ".svg"), "w") as fh:
        fh.write(svg_doc(body))
    print(cls, os.path.getsize(os.path.join(OUT, cls + ".svg")), "bytes")
for n in os.listdir(HERE):
    if n.endswith(("_in.svg", "_out.svg")):
        os.remove(os.path.join(HERE, n))
