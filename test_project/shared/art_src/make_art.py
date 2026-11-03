"""The test project's sprites (shared/art/): clean sci-fi ships, rockets and a
turret with neon accents. Every frame is drawn as an SVG (32 x 32 units, the
sprites' size, facing and frame counts unchanged), VectorCraft adds the neon
glows (live Outer Glow effects on the named parts) and renders it 8x larger,
PhotoCraft sharpens and enriches it, and an 8 x 8 box filter brings it back to
32 x 32 (clean, antialiased edges).

    python3 test_project/shared/art_src/make_art.py
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ART = os.path.normpath(os.path.join(HERE, "..", "art"))
SCALE = 8

CYAN = "#4ff3ff"
MAGENTA = "#ff3d8f"
RED = "#ff3b4e"
ORANGE = "#ff8a3d"


def svg(body, defs=""):
    return ('<svg xmlns="http://www.w3.org/2000/svg" width="32" height="32" viewBox="0 0 32 32">'
            f"<defs>{defs}</defs>{body}</svg>")


def lin(gid, stops, x1=0, y1=0, x2=0, y2=1):
    s = "".join(f'<stop offset="{o}" stop-color="{c}" stop-opacity="{a}"/>' for o, c, a in stops)
    return f'<linearGradient id="{gid}" x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}">{s}</linearGradient>'


def flame_down(gid, cx, top, length, half, colors):
    """A tapered exhaust flame hanging below (cx, top)."""
    tip = top + length
    return (lin(gid, [(0, colors[0], 1), (0.35, colors[1], 0.95), (1, colors[1], 0)]),
            f'<path id="glow_plume_{gid}" d="M{cx - half} {top} Q{cx - half * 0.8} {top + length * 0.55} {cx} {tip} '
            f'Q{cx + half * 0.8} {top + length * 0.55} {cx + half} {top} Z" fill="url(#{gid})"/>')


def flame_left(gid, x, cy, length, half, colors):
    """A tapered exhaust flame trailing left of (x, cy) (rockets fly right)."""
    tip = x - length
    return (lin(gid, [(0, colors[0], 1), (0.35, colors[1], 0.95), (1, colors[1], 0)], 1, 0, 0, 0),
            f'<path id="glow_plume_{gid}" d="M{x} {cy - half} Q{x - length * 0.55} {cy - half * 0.8} {tip} {cy} '
            f'Q{x - length * 0.55} {cy + half * 0.8} {x} {cy + half} Z" fill="url(#{gid})"/>')


# ------------------------------------------------------------------ the sprites
def player_ship(plume):
    defs = (lin("hull", [(0, "#4a5a80", 1), (1, "#1a2134", 1)])
            + lin("glass", [(0, "#d9fdff", 1), (1, "#18a8c8", 1)]))
    body = ('<path d="M16 2 L19 10 L20 15 L28 21.5 L28 25 L20 23 L18.6 26 L13.4 26 L12 23 L4 25 L4 21.5 L12 15 L13 10 Z" fill="url(#hull)"/>'
            '<path d="M12 15.6 L5 21.6 L5 24 L12 22.2 Z M20 15.6 L27 21.6 L27 24 L20 22.2 Z" fill="#202941"/>'
            '<path d="M16 6 L17.4 13 L16 21 L14.6 13 Z" fill="#5b6c96" opacity="0.55"/>'
            '<path id="glow_trim" d="M12.6 15.4 L4.6 22 M19.4 15.4 L27.4 22 M13.4 10.4 L16 3.2 L18.6 10.4" '
            f'fill="none" stroke="{CYAN}" stroke-width="0.7" stroke-linecap="round"/>'
            '<ellipse id="glow_cockpit" cx="16" cy="11.2" rx="1.7" ry="3.1" fill="url(#glass)"/>'
            '<rect x="13.4" y="23.6" width="1.9" height="2.6" rx="0.4" fill="#0d121e"/>'
            '<rect x="16.7" y="23.6" width="1.9" height="2.6" rx="0.4" fill="#0d121e"/>')
    if plume > 0:
        for i, cx in enumerate((14.35, 17.65)):
            d, p = flame_down(f"pl{i}", cx, 25.6, plume, 1.15, ("#ffffff", CYAN))
            defs += d
            body += p
    return svg(body, defs), {"glow_trim": (CYAN, 1.1, 90), "glow_cockpit": (CYAN, 1.4, 70), "glow_plume": (CYAN, 1.6, 75)}


def enemy_ship(plume):
    defs = (lin("hull", [(0, "#8a3254", 1), (0.6, "#4a1a30", 1), (1, "#2a0d1b", 1)])
            + '<radialGradient id="core"><stop offset="0" stop-color="#ffe0ea"/><stop offset="1" stop-color="#ff2e6e"/></radialGradient>')
    body = ('<path d="M16 4 L19 11 L25 8.6 L29.4 15.8 L23.4 18 L20 24 L12 24 L8.6 18 L2.6 15.8 L7 8.6 L13 11 Z" fill="url(#hull)"/>'
            '<path d="M7.4 10 L12.6 12.2 L10.4 17.4 L4.2 15.4 Z M24.6 10 L19.4 12.2 L21.6 17.4 L27.8 15.4 Z" fill="#5c1f39" opacity="0.8"/>'
            f'<path id="glow_trim" d="M3 15.6 L7.2 9 L13 11.2 M29 15.6 L24.8 9 L19 11.2 M12.2 23.4 L16 19.6 L19.8 23.4" '
            f'fill="none" stroke="{MAGENTA}" stroke-width="0.65" stroke-linecap="round" stroke-linejoin="round"/>'
            '<circle id="glow_core" cx="16" cy="15" r="2.3" fill="url(#core)"/>'
            '<rect x="14.6" y="22.8" width="2.8" height="2" rx="0.4" fill="#12060c"/>')
    if plume > 0:
        d, p = flame_down("pl", 16, 24.4, plume, 1.45, ("#fff2e0", ORANGE))
        defs += d
        body += p
    return svg(body, defs), {"glow_trim": (MAGENTA, 1.1, 90), "glow_core": (MAGENTA, 1.6, 85), "glow_plume": (ORANGE, 1.6, 75)}


def turret():
    defs = (lin("metal", [(0, "#6c7690", 1), (0.5, "#3c4458", 1), (1, "#262b38", 1)], 0, 0, 1, 0)
            + lin("body", [(0, "#4a5266", 1), (1, "#23283a", 1)]))
    body = ('<rect x="13.4" y="5" width="5.2" height="13" rx="1" fill="url(#metal)"/>'
            '<rect x="14.6" y="5.4" width="1" height="12" fill="#8a94ae" opacity="0.7"/>'
            f'<rect id="glow_muzzle" x="12.9" y="3" width="6.2" height="2.6" rx="0.8" fill="{RED}"/>'
            '<path d="M8 19 Q8 15.4 11.6 15.4 L20.4 15.4 Q24 15.4 24 19 L24 25 Q24 27.4 21.6 27.4 L10.4 27.4 Q8 27.4 8 25 Z" fill="url(#body)"/>'
            '<path d="M10 17.4 L22 17.4" stroke="#6a748c" stroke-width="0.6" opacity="0.8"/>'
            f'<path id="glow_trim" d="M11 23.6 L21 23.6" stroke="{RED}" stroke-width="1" stroke-linecap="round"/>'
            '<circle cx="10.6" cy="20" r="0.75" fill="#14171f"/><circle cx="21.4" cy="20" r="0.75" fill="#14171f"/>')
    return svg(body, defs), {"glow_muzzle": (RED, 1.4, 85), "glow_trim": (RED, 1.0, 85)}


def turret_holder():
    import math
    pts = " ".join("%.2f,%.2f" % (16 + 12.5 * math.cos(math.pi / 8 + k * math.pi / 4), 16 + 12.5 * math.sin(math.pi / 8 + k * math.pi / 4)) for k in range(8))
    inner = " ".join("%.2f,%.2f" % (16 + 7 * math.cos(math.pi / 8 + k * math.pi / 4), 16 + 7 * math.sin(math.pi / 8 + k * math.pi / 4)) for k in range(8))
    bolts = "".join('<circle cx="%.2f" cy="%.2f" r="0.8" fill="#9aa4bd"/>' % (16 + 10.6 * math.cos(k * math.pi / 2 + math.pi / 4), 16 + 10.6 * math.sin(k * math.pi / 2 + math.pi / 4)) for k in range(4))
    defs = '<radialGradient id="plate"><stop offset="0" stop-color="#5a6380"/><stop offset="1" stop-color="#2c3244"/></radialGradient>'
    body = (f'<polygon points="{pts}" fill="url(#plate)"/>'
            f'<circle id="glow_ring" cx="16" cy="16" r="9" fill="none" stroke="{RED}" stroke-width="0.8"/>'
            f'<polygon points="{inner}" fill="#3a4256" stroke="#6a748c" stroke-width="0.4"/>' + bolts)
    return svg(body, defs), {"glow_ring": (RED, 1.3, 85)}


def rocket(flame, enemy):
    if enemy:
        defs = lin("body", [(0, "#ff8aa3", 1), (0.5, "#c0284f", 1), (1, "#5a0f26", 1)])
        stripe, fin, plume_cols, glow = MAGENTA, "#3a0d1c", ("#fff2e0", ORANGE), ORANGE
    else:
        defs = lin("body", [(0, "#ffffff", 1), (0.5, "#c6d2ea", 1), (1, "#7d8aa8", 1)])
        stripe, fin, plume_cols, glow = CYAN, "#263252", ("#ffffff", CYAN), CYAN
    body = ('<path d="M8 13.6 L20.5 13.6 Q26.6 13.8 29 16 Q26.6 18.2 20.5 18.4 L8 18.4 Z" fill="url(#body)"/>'
            f'<path d="M8 13.6 L11.6 13.6 L8.6 10.2 L7.2 10.2 Z M8 18.4 L11.6 18.4 L8.6 21.8 L7.2 21.8 Z" fill="{fin}"/>'
            f'<path id="glow_fin" d="M7.4 10.4 L8.6 10.4 L11.4 13.6 M7.4 21.6 L8.6 21.6 L11.4 18.4" fill="none" stroke="{stripe}" stroke-width="0.5"/>'
            f'<rect id="glow_stripe" x="12.6" y="13.6" width="1.8" height="4.8" fill="{stripe}"/>')
    if flame > 0:
        d, p = flame_left("fl", 8, 16, flame, 2.0, plume_cols)
        defs += d
        body = p + body
    return svg(body, defs), {"glow_stripe": (stripe, 0.9, 80), "glow_fin": (stripe, 0.7, 70), "glow_plume": (glow, 1.5, 70)}


def light():
    defs = ('<radialGradient id="g"><stop offset="0" stop-color="#ffffff" stop-opacity="1"/>'
            '<stop offset="0.35" stop-color="#ffffff" stop-opacity="0.55"/>'
            '<stop offset="0.7" stop-color="#ffffff" stop-opacity="0.15"/>'
            '<stop offset="1" stop-color="#ffffff" stop-opacity="0"/></radialGradient>')
    return svg('<circle cx="16" cy="16" r="16" fill="url(#g)"/>', defs), {}


SPRITES = {
    # Flame lengths per frame: the old sprites' flicker, frame for frame.
    **{f"player_ship/player_ship{n}.png": (player_ship, (L,)) for n, L in zip((1, 2, 3, 5, 6), (5.6, 4.0, 6.4, 4.8, 3.4))},
    "player_ship/player_ship_idle.png": (player_ship, (2.4,)),
    **{f"enemy_ship/enemy_ship{n}.png": (enemy_ship, (L,)) for n, L in zip((1, 2, 3, 4), (5.2, 3.6, 6.0, 4.2))},
    "enemy_ship/enemy_ship_idle.png": (enemy_ship, (2.2,)),
    "enemy_turret/enemy_turret.png": (turret, ()),
    "enemy_turret/enemy_turret_holder.png": (turret_holder, ()),
    **{f"player_bullets/{n}.png": (rocket, (L, False)) for n, L in zip(range(1, 11), (5.0, 6.6, 4.6, 7.0, 5.6, 6.2, 4.2, 6.8, 5.2, 5.9))},
    **{f"enemy_bullets/enemy_bullet{n}.png": (rocket, (L, True)) for n, L in zip(range(1, 5), (5.0, 6.2, 4.6, 6.6))},
    "light/light_texture.png": (light, ()),
}


# ------------------------------------------------------------------ rendering
def tool(app, args, work):
    cmd = ["flatpak", "run", "--filesystem=" + work, f"--command={app}-cli", f"ai.storyteller.{app}"] + args
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit(f"{app} failed:\n{r.stdout[-1500:]}{r.stderr[-1500:]}")
    return r.stdout


def render(name, fn, args, work):
    text, glows = fn(*args)
    stem = name.replace("/", "_").replace(".png", "")
    src = os.path.join(work, stem + ".svg")
    open(src, "w").write(text)
    steps = []
    for part, (color, blur, opacity) in glows.items():
        steps.append({"command": "document.find", "params": {"name": part, "select": True}})
        steps.append({"command": "effect.apply", "params": {"effect": "stylize.outerGlow",
                                                            "params": {"blur": blur, "color": color, "opacity": opacity, "mode": "screen"}}})
        steps.append({"command": "select.none", "params": {}})
    big = os.path.join(work, stem + "_8x.png")
    plan = os.path.join(work, stem + ".jsonl")
    open(plan, "w").write("\n".join(json.dumps(s) for s in steps) + "\n")
    out = tool("vectorcraft", ["run", "--in", src, "--commands", plan, "--export", big, "--scale", str(SCALE)], work)
    for line in out.splitlines():  # a glow that found nothing is a broken sprite
        r = json.loads(line)
        if r.get("command") == "document.find" and not r["result"]["matches"]:
            sys.exit(f"{name}: a glow part was not found ({line[:120]})")
    finished = os.path.join(work, stem + "_pc.png")
    tool("photocraft", ["run", big, "--cmd", "filter.sharpen.unsharpMask", "--params", '{"amount": 60, "radius": 2.0, "threshold": 0}',
                        "--cmd", "image.adjustments.vibrance", "--params", '{"vibrance": 30, "saturation": 5}', "--out", finished], work)
    img = Image.open(finished).convert("RGBA")
    assert img.size == (32 * SCALE, 32 * SCALE), (name, img.size)
    img = img.reduce(SCALE)
    dst = os.path.join(ART, name)
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    img.save(dst)
    return dst


def main():
    work = tempfile.mkdtemp(prefix="art_", dir=os.environ.get("ART_WORK"))
    try:
        for name, (fn, args) in SPRITES.items():
            render(name, fn, args, work)
            print("drew", name)
    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    main()
