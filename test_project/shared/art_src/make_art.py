"""The test project's sprites (shared/art/): clean sci-fi ships, rockets and a
turret with neon accents, drawn as vectors and rendered natively at 256 x 256
(8 px per design unit: no upscaling, no downscaling, crisp at any zoom the test
lab uses). VectorCraft renders each frame and adds the neon glows (live Outer
Glow effects on the named parts). Every animation is a seamless loop: each frame
samples smooth cycles (sines of the loop phase), so engines breathe, flames
lick and navigation lights blink without a jump at the loop point.

    python3 test_project/shared/art_src/make_art.py [sprite names]

Writes shared/art/<sprite>/<animation>_NN.png (see SPRITES).
"""
import json
import math
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ART = os.environ.get("ART_OUT") or os.path.normpath(os.path.join(HERE, "..", "art"))
SCALE = 8  # 32 design units -> 256 px

CYAN = "#4ff3ff"
MAGENTA = "#ff3d8f"
RED = "#ff3b4e"
ORANGE = "#ff8a3d"
GREEN = "#5dff9a"


def wave(t, k=1, phase=0.0):
    """A smooth cycle of the loop phase t in [0, 1): seamless at t = 1."""
    return math.sin(2 * math.pi * (k * t + phase))


def svg(body, defs=""):
    return ('<svg xmlns="http://www.w3.org/2000/svg" width="32" height="32" viewBox="0 0 32 32">'
            f"<defs>{defs}</defs>{body}</svg>")


def lin(gid, stops, x1=0, y1=0, x2=0, y2=1):
    s = "".join(f'<stop offset="{o}" stop-color="{c}" stop-opacity="{a}"/>' for o, c, a in stops)
    return f'<linearGradient id="{gid}" x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}">{s}</linearGradient>'


def rad(gid, stops):
    s = "".join(f'<stop offset="{o}" stop-color="{c}" stop-opacity="{a}"/>' for o, c, a in stops)
    return f'<radialGradient id="{gid}">{s}</radialGradient>'


def flame(gid, base, axis, length, half, outer, inner="#ffffff", lick=0.0):
    """A two-layer exhaust flame from `base` along unit `axis`: a coloured outer
    tongue and a white-hot core. `lick` bends the tip sideways (animation)."""
    (bx, by), (ax, ay) = base, axis
    px, py = -ay, ax  # sideways
    def tongue(L, w, tag, color_a, color_b, op):
        tipx = bx + ax * L + px * lick * L * 0.18
        tipy = by + ay * L + py * lick * L * 0.18
        c1x, c1y = bx + ax * L * 0.45 + px * w * 0.95, by + ay * L * 0.45 + py * w * 0.95
        c2x, c2y = bx + ax * L * 0.45 - px * w * 0.95, by + ay * L * 0.45 - py * w * 0.95
        d = (f"M{bx + px * w:.3f} {by + py * w:.3f} Q{c1x:.3f} {c1y:.3f} {tipx:.3f} {tipy:.3f} "
             f"Q{c2x:.3f} {c2y:.3f} {bx - px * w:.3f} {by - py * w:.3f} Z")
        g = lin(f"{gid}{tag}", [(0, color_a, op), (0.55, color_b, op * 0.85), (1, color_b, 0)],
                0.5 - ax * 0.5, 0.5 - ay * 0.5, 0.5 + ax * 0.5, 0.5 + ay * 0.5)
        return g, f'<path d="{d}" fill="url(#{gid}{tag})"/>'
    g1, p1 = tongue(length, half, "o", inner, outer, 0.95)
    g2, p2 = tongue(length * 0.55, half * 0.5, "i", "#ffffff", inner, 1.0)
    return g1 + g2, p1 + p2


def mirror(left):
    """A closed, exactly symmetric polygon path from its left half: points from
    the top of the axis (x = 16) down its left side; the right side mirrors them."""
    right = [(32 - x, y) for x, y in reversed(left) if x != 16]
    pts = list(left) + right
    return "M" + " L".join(f"{x:.3f} {y:.3f}" for x, y in pts) + " Z"


def both(path_of):
    """`path_of(sign, cx)` drawn for the left (sign -1) and the mirrored right side."""
    return path_of(-1) + path_of(1)


# ------------------------------------------------------------------ player ship (nose up)
def player_ship(t, thrust):
    pulse = 0.5 + 0.5 * wave(t)
    defs = (lin("hull", [(0, "#8ea2cf", 1), (0.35, "#4a5b82", 1), (1, "#1a2134", 1)])
            + lin("hullside", [(0, "#10162a", 0.0), (1, "#05070d", 0.55)], 0, 0, 1, 0)
            + lin("wing", [(0, "#3a4870", 1), (1, "#161c2e", 1)])
            + lin("pod", [(0, "#5a6b94", 1), (0.5, "#2a3450", 1), (1, "#141a2a", 1)], 0, 0, 1, 0)
            + lin("glass", [(0, "#f2ffff", 1), (0.45, "#6fe3f6", 1), (1, "#0d4f68", 1)])
            + rad("engcore", [(0, "#ffffff", 1), (0.45, "#c8fdff", 1), (1, CYAN, 0.0)]))
    wing = mirror([(16, 12.6), (12.6, 13.2), (4.4, 21.2), (3.9, 23.9), (6.4, 24.4), (12.8, 22.6), (16, 22.6)])
    hull = mirror([(16, 1.8), (14.9, 5.0), (14.0, 9.6), (13.5, 14.0), (13.2, 19.0), (13.6, 23.2), (14.4, 24.8), (16, 25.2)])
    body = (
        # Wings (under the hull), a darker inner panel and a lit leading edge.
        f'<path d="{wing}" fill="url(#wing)"/>'
        + f'<path d="{mirror([(16, 15.2), (12.4, 15.6), (6.6, 21.4), (6.6, 22.6), (12.4, 21.2), (16, 21.2)])}" fill="#0c1120" opacity="0.75"/>'
        # Wingtip pods.
        + both(lambda s: f'<rect x="{(16 + s * 11.6) - 0.9:.3f}" y="19.6" width="1.8" height="5.2" rx="0.9" fill="url(#pod)"/>')
        # Engine nacelles beside the hull.
        + both(lambda s: f'<rect x="{(16 + s * 2.55) - 1.45:.3f}" y="17.2" width="2.9" height="9.0" rx="1.2" fill="url(#pod)"/>')
        # Hull with a soft side shade (the same on both sides) and a spine highlight.
        + f'<path d="{hull}" fill="url(#hull)"/>'
        + f'<path d="{hull}" fill="url(#hullside)" opacity="0.6"/>'
        + f'<path d="{mirror([(16, 3.4), (15.6, 6.0), (16, 6.2)])}" fill="#ffffff" opacity="0.25"/>'
        '<path d="M16 7.4 L16 24.4" stroke="#c9d6f2" stroke-width="0.35" opacity="0.35"/>'
        # Panel lines and rivets, mirrored.
        '<path d="M14.0 16.2 L18.0 16.2 M13.8 19.6 L18.2 19.6" stroke="#0b0f19" stroke-width="0.22" opacity="0.8"/>'
        + both(lambda s: f'<circle cx="{16 + s * 1.5:.3f}" cy="17.9" r="0.2" fill="#a9b9de" opacity="0.75"/><circle cx="{16 + s * 1.5:.3f}" cy="21.3" r="0.2" fill="#a9b9de" opacity="0.75"/>')
        # Cockpit: a dark frame, the canopy and one central glint.
        + '<ellipse cx="16" cy="10.4" rx="1.75" ry="3.4" fill="#070a12"/>'
        '<ellipse id="gl_cockpit" cx="16" cy="10.4" rx="1.35" ry="3.0" fill="url(#glass)"/>'
        '<ellipse cx="16" cy="9.0" rx="0.45" ry="1.2" fill="#ffffff" opacity="0.8"/>'
        # Neon trims: leading edges of both wings and the nose, one group.
        f'<g id="gl_trim" fill="none" stroke="{CYAN}" stroke-width="0.5" stroke-linecap="round" stroke-linejoin="round">'
        + both(lambda s: f'<path d="M{16 + s * 3.3:.3f} 13.4 L{16 + s * 11.4:.3f} 21.3"/>')
        + '<path d="M14.95 5.2 L16 2.4 L17.05 5.2"/></g>'
        # Nozzle rings, then the hot cores (one group: one glow for both).
        + both(lambda s: f'<ellipse cx="{16 + s * 2.55:.3f}" cy="26.0" rx="1.3" ry="0.62" fill="#05070c"/>')
        + '<g id="gl_engines">'
        + both(lambda s: f'<ellipse cx="{16 + s * 2.55:.3f}" cy="26.0" rx="{0.95 + 0.12 * pulse:.3f}" ry="0.45" fill="url(#engcore)"/>')
        + '</g>')
    # Navigation lights: red port, green starboard, blinking in turn.
    port = 1.0 if (t * 8) % 8 < 2 else 0.3
    star = 1.0 if ((t * 8) + 4) % 8 < 2 else 0.3
    body += (f'<circle id="gl_navl" cx="4.4" cy="20.2" r="0.5" fill="{RED}" opacity="{port}"/>'
             f'<circle id="gl_navr" cx="27.6" cy="20.2" r="0.5" fill="{GREEN}" opacity="{star}"/>')
    # Both engines burn in step (the same flame), mirrored.
    if thrust:
        L = 4.8 + 0.9 * wave(t, 1) + 0.4 * wave(t, 3, 0.2)
        half = 1.0 + 0.1 * wave(t, 2)
        lick = 0.35 * wave(t, 2, 0.25)
    else:
        L, half, lick = 1.7 + 0.35 * pulse, 0.75, 0.0
    flames = ""
    for i, s in enumerate((-1, 1)):
        d, p = flame(f"f{i}", (16 + s * 2.55, 26.25), (0, 1), L, half, CYAN, "#bffbff", lick * s)
        defs += d
        flames += p
    body = f'<g id="gl_plume">{flames}</g>' + body
    glows = {"gl_trim": (CYAN, 0.7, 80), "gl_cockpit": (CYAN, 0.8, 55), "gl_engines": (CYAN, 1.1, 90),
             "gl_plume": (CYAN, 1.2, 70), "gl_navl": (RED, 0.8, 90 * port), "gl_navr": (GREEN, 0.8, 90 * star)}
    return svg(body, defs), glows


# ------------------------------------------------------------------ enemy ship (nose up)
# Gun muzzles (design units), mirrored: the enemy_ship.tscn markers sit here.
ENEMY_GUNS = ((9.2, 5.9), (16, 3.0), (22.8, 5.9))


def enemy_ship(t, thrust):
    pulse = 0.5 + 0.5 * wave(t)
    defs = (lin("hull", [(0, "#b5487a", 1), (0.45, "#621f3d", 1), (1, "#22070f", 1)])
            + lin("plate", [(0, "#7d2c4e", 1), (1, "#2a0b18", 1)])
            + lin("gun", [(0, "#c9a9b9", 1), (0.5, "#6e4f60", 1), (1, "#2a1a23", 1)], 0, 0, 1, 0)
            + rad("core", [(0, "#ffffff", 1), (0.35, "#ffb8d0", 1), (1, MAGENTA, 1)]))
    hull = mirror([(16, 3.6), (14.2, 8.4), (10.4, 9.4), (6.6, 7.6), (2.6, 14.2), (3.4, 16.0), (8.6, 17.6), (11.0, 23.4), (13.6, 24.6), (16, 24.6)])
    plate = mirror([(16, 10.6), (13.8, 11.2), (10.2, 11.8), (7.0, 10.6), (4.8, 14.4), (9.4, 15.8), (12.4, 20.6), (16, 21.2)])
    body = (
        # Gun barrels on the wing roots and the nose, under the hull.
        f'<path d="{hull}" fill="url(#hull)"/>'
        + f'<path d="{plate}" fill="url(#plate)"/>'
        # Wing guns on top of the hull: a housing and a lit barrel.
        + both(lambda s: f'<rect x="{16 + s * 6.8 - 1.7:.3f}" y="9.4" width="3.4" height="3.4" rx="0.9" fill="#1e0d16" stroke="#8a4a68" stroke-width="0.25"/>'
                         f'<rect x="{16 + s * 6.8 - 0.85:.3f}" y="5.6" width="1.7" height="5.6" rx="0.45" fill="url(#gun)"/>')
        + f'<path d="{mirror([(16, 3.6), (14.4, 8.0), (16, 9.6)])}" fill="#d25a8a" opacity="0.55"/>'
        '<path d="M10.2 11.8 L12.4 20.6 M21.8 11.8 L19.6 20.6 M13 13.2 L19 13.2" stroke="#12040a" stroke-width="0.24" opacity="0.85"/>'
        + both(lambda s: f'<circle cx="{16 + s * 7.6:.3f}" cy="12.6" r="0.22" fill="#ffa8c6" opacity="0.6"/><circle cx="{16 + s * 3.6:.3f}" cy="20.4" r="0.22" fill="#ffa8c6" opacity="0.6"/>')
        # Neon trims (one group) and gun tips.
        + f'<g id="gl_trim" fill="none" stroke="{MAGENTA}" stroke-width="0.5" stroke-linecap="round" stroke-linejoin="round">'
        + both(lambda s: f'<path d="M{16 + s * 13.0:.3f} 14.4 L{16 + s * 9.2:.3f} 8.2 L{16 + s * 5.4:.3f} 9.6"/>')
        + '<path d="M12.2 23.6 L16 20.2 L19.8 23.6"/></g>'
        + '<g id="gl_tips">' + "".join(f'<rect x="{x - 0.95:.3f}" y="{y - 0.5:.3f}" width="1.9" height="1.2" rx="0.35" fill="{ORANGE}"/>' for x, y in ENEMY_GUNS) + '</g>'
        # The core.
        '<circle cx="16" cy="15" r="3.2" fill="#100408"/>'
        f'<circle id="gl_core" cx="16" cy="15" r="{2.1 + 0.35 * pulse:.3f}" fill="url(#core)"/>'
        '<rect x="14.3" y="23.4" width="3.4" height="2.0" rx="0.6" fill="#10050a"/>')
    # The nose gun (on top: it is the hull's spike).
    body = f'<rect x="15.25" y="2.6" width="1.5" height="4.0" rx="0.4" fill="url(#gun)"/>' + body
    eyes = 0.55 + 0.45 * (0.5 + 0.5 * wave(t, 2, 0.25))
    body += f'<g id="gl_eyes" opacity="{eyes:.2f}">' + both(lambda s: f'<circle cx="{16 + s * 3.1:.3f}" cy="10.2" r="0.42" fill="{ORANGE}"/>') + '</g>'
    L = (5.4 + 1.2 * wave(t, 1) + 0.5 * wave(t, 3, 0.3)) if thrust else (1.8 + 0.4 * pulse)
    d, p = flame("f", (16, 25.2), (0, 1), L, 1.35 + (0.15 * wave(t, 2) if thrust else 0), ORANGE, "#ffd9a8", 0.0)
    glows = {"gl_trim": (MAGENTA, 0.7, 80), "gl_core": (MAGENTA, 1.3 + 0.4 * pulse, 90), "gl_plume": (ORANGE, 1.2, 75),
             "gl_eyes": (ORANGE, 0.7, 80), "gl_tips": (ORANGE, 0.6, 70)}
    return svg(f'<g id="gl_plume">{p}</g>' + body, defs + d), glows


# ------------------------------------------------------------------ turret
# The gun turns about the image centre (16, 16) and points along +X; the
# muzzle tip is TURRET_MUZZLE_X (enemy_turret.tscn's BulletMarker).
TURRET_MUZZLE_X = 29.6


def turret(t):
    defs = (lin("barrel", [(0, "#9aa5c0", 1), (0.45, "#4a5470", 1), (1, "#1c2130", 1)])
            + rad("hub", [(0, "#6c7792", 1), (0.7, "#343b50", 1), (1, "#1a1e2a", 1)]))
    body = (
        # Twin side rails and the barrel along +X.
        '<rect x="17.5" y="13.2" width="9.5" height="1.2" rx="0.4" fill="#262c3c"/>'
        '<rect x="17.5" y="17.6" width="9.5" height="1.2" rx="0.4" fill="#262c3c"/>'
        '<rect x="17" y="14.3" width="11.4" height="3.4" rx="0.8" fill="url(#barrel)"/>'
        '<rect x="18" y="14.75" width="9.6" height="0.55" fill="#d6dcef" opacity="0.5"/>'
        '<rect x="21.6" y="14.3" width="0.5" height="3.4" fill="#141824"/><rect x="24.6" y="14.3" width="0.5" height="3.4" fill="#141824"/>'
        f'<rect id="gl_muzzle" x="27.6" y="13.9" width="2.2" height="4.2" rx="0.6" fill="{RED}"/>'
        # The hub: a round armoured housing centred on the pivot.
        '<circle cx="16" cy="16" r="6.4" fill="#10131c"/>'
        '<circle cx="16" cy="16" r="5.9" fill="url(#hub)"/>'
        '<path d="M11.4 12.6 A5.9 5.9 0 0 1 20.6 12.6" stroke="#b8c2da" stroke-width="0.35" fill="none" opacity="0.6"/>'
        f'<circle id="gl_eye" cx="16" cy="16" r="1.5" fill="{RED}"/>'
        '<circle cx="16" cy="16" r="2.7" fill="none" stroke="#0b0d14" stroke-width="0.5"/>'
        + "".join('<circle cx="%.2f" cy="%.2f" r="0.35" fill="#9aa5c0"/>' % (16 + 4.4 * math.cos(a), 16 + 4.4 * math.sin(a)) for a in (math.pi * 0.25, math.pi * 0.75, math.pi * 1.25, math.pi * 1.75)))
    return svg(body, defs), {"gl_muzzle": (RED, 1.0, 85), "gl_eye": (RED, 0.9, 85)}


def turret_holder(t):
    pts = " ".join("%.2f,%.2f" % (16 + 12.6 * math.cos(math.pi / 8 + k * math.pi / 4), 16 + 12.6 * math.sin(math.pi / 8 + k * math.pi / 4)) for k in range(8))
    inner = " ".join("%.2f,%.2f" % (16 + 7.4 * math.cos(math.pi / 8 + k * math.pi / 4), 16 + 7.4 * math.sin(math.pi / 8 + k * math.pi / 4)) for k in range(8))
    bolts = "".join('<circle cx="%.2f" cy="%.2f" r="0.65" fill="#a8b2cb"/>' % (16 + 10.6 * math.cos(k * math.pi / 2 + math.pi / 4), 16 + 10.6 * math.sin(k * math.pi / 2 + math.pi / 4)) for k in range(4))
    defs = rad("plate", [(0, "#5f6987", 1), (1, "#272c3c", 1)])
    body = (f'<polygon points="{pts}" fill="url(#plate)" stroke="#121520" stroke-width="0.35"/>'
            f'<circle id="gl_ring" cx="16" cy="16" r="9.2" fill="none" stroke="{RED}" stroke-width="0.6"/>'
            f'<polygon points="{inner}" fill="#2c3245" stroke="#7a84a0" stroke-width="0.3"/>' + bolts)
    return svg(body, defs), {"gl_ring": (RED, 1.0, 80)}


# ------------------------------------------------------------------ rockets (fly right)
def rocket(t, enemy):
    if enemy:
        defs = lin("body", [(0, "#ffb0c4", 1), (0.45, "#cf2e58", 1), (1, "#4e0c22", 1)])
        stripe, fin, flame_col, flame_in = MAGENTA, "#3a0c1c", ORANGE, "#ffd9a8"
    else:
        defs = lin("body", [(0, "#ffffff", 1), (0.45, "#cdd8ee", 1), (1, "#6f7c99", 1)])
        stripe, fin, flame_col, flame_in = CYAN, "#22304f", CYAN, "#c9fdff"
    defs += lin("nose", [(0, "#ffffff", 0.9), (1, stripe, 0.0)], 0, 0, 1, 0)
    blink = 0.6 + 0.4 * (0.5 + 0.5 * wave(t, 2))
    body = ('<path d="M8 13.7 L20.4 13.7 Q26.7 13.9 29.2 16 Q26.7 18.1 20.4 18.3 L8 18.3 Z" fill="url(#body)"/>'
            '<path d="M8 13.7 L20.4 13.7 Q26.7 13.9 29.2 16 L8 16 Z" fill="#ffffff" opacity="0.12"/>'
            f'<path d="M8.2 13.7 L11.9 13.7 L8.8 10.1 L7.2 10.1 Z M8.2 18.3 L11.9 18.3 L8.8 21.9 L7.2 21.9 Z" fill="{fin}"/>'
            f'<path id="gl_fin" d="M7.5 10.3 L8.7 10.3 L11.6 13.7 M7.5 21.7 L8.7 21.7 L11.6 18.3" fill="none" stroke="{stripe}" stroke-width="0.4"/>'
            f'<rect id="gl_stripe" x="12.5" y="13.7" width="1.6" height="4.6" fill="{stripe}" opacity="{blink:.2f}"/>'
            '<rect x="15.2" y="13.7" width="0.5" height="4.6" fill="#0d1220" opacity="0.5"/>'
            '<path d="M24.5 14.3 Q27.6 14.9 29.2 16 Q27.6 17.1 24.5 17.7 Z" fill="url(#nose)"/>')
    L = 5.6 + 1.3 * wave(t, 1) + 0.6 * wave(t, 3, 0.35)
    d, p = flame("fl", (8.1, 16), (-1, 0), L, 1.9 + 0.2 * wave(t, 2, 0.2), flame_col, flame_in, 0.55 * wave(t, 2, 0.15))
    return svg(f'<g id="gl_plume">{p}</g>' + body, defs + d), {"gl_stripe": (stripe, 0.8, 80), "gl_fin": (stripe, 0.6, 70), "gl_plume": (flame_col, 1.2, 75)}


def light(t):
    defs = rad("g", [(0, "#ffffff", 1), (0.2, "#ffffff", 0.75), (0.45, "#ffffff", 0.3), (0.75, "#ffffff", 0.07), (1, "#ffffff", 0)])
    return svg('<circle cx="16" cy="16" r="16" fill="url(#g)"/>', defs), {}


# name -> (folder, [(file stem, frame count, draw(t))])
SPRITES = {
    "player_ship": [("idle", 8, lambda t: player_ship(t, False)), ("moving", 8, lambda t: player_ship(t, True))],
    "enemy_ship": [("idle", 8, lambda t: enemy_ship(t, False)), ("moving", 8, lambda t: enemy_ship(t, True))],
    "player_bullets": [("rocket", 10, lambda t: rocket(t, False))],
    "enemy_bullets": [("plasma_rocket", 8, lambda t: rocket(t, True))],
    "enemy_turret": [("turret_gun", 1, turret), ("turret_base", 1, turret_holder)],
    "light": [("light", 1, light)],
}


# ------------------------------------------------------------------ rendering
def vectorcraft(args, work):
    cmd = ["flatpak", "run", "--filesystem=" + work, "--filesystem=" + ART, "--command=vectorcraft-cli", "ai.storyteller.vectorcraft"] + args
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit(f"vectorcraft failed:\n{r.stdout[-1500:]}{r.stderr[-1500:]}")
    return r.stdout


def render(text, glows, dst, work):
    stem = os.path.basename(dst).replace(".png", "")
    src = os.path.join(work, stem + ".svg")
    open(src, "w").write(text)
    names = list(glows)
    for a in names:  # document.find matches a SUBSTRING of the name
        for b in names:
            if a != b and a in b:
                sys.exit(f"{dst}: glow part names must not contain each other ({a} in {b})")
    steps, finds = [], []
    for part, (color, blur, opacity) in glows.items():
        if opacity <= 0:
            continue
        finds.append(part)
        steps += [{"command": "document.find", "params": {"name": part, "select": True}},
                  {"command": "effect.apply", "params": {"effect": "stylize.outerGlow",
                                                         "params": {"blur": blur, "color": color, "opacity": opacity, "mode": "screen"}}},
                  {"command": "select.none", "params": {}}]
    plan = os.path.join(work, stem + ".jsonl")
    open(plan, "w").write("\n".join(json.dumps(s) for s in steps) + "\n")
    out = vectorcraft(["run", "--in", src, "--commands", plan, "--export", dst, "--scale", str(SCALE)], work)
    found = [json.loads(line) for line in out.splitlines() if line.strip().startswith("{")]
    found = [r for r in found if r.get("command") == "document.find"]
    for part, r in zip(finds, found):
        if r["result"].get("total") != 1:
            sys.exit(f"{dst}: glow part {part} matched {r['result'].get('total')} objects (expected exactly 1)")


def main():
    only = sys.argv[1:]
    work = tempfile.mkdtemp(prefix="art_", dir=os.environ.get("ART_WORK"))
    try:
        for folder, anims in SPRITES.items():
            if only and folder not in only:
                continue
            os.makedirs(os.path.join(ART, folder), exist_ok=True)
            for stem, count, draw in anims:
                for k in range(count):
                    text, glows = draw(k / count)
                    name = f"{stem}_{k + 1:02d}.png" if count > 1 else f"{stem}.png"
                    render(text, glows, os.path.join(ART, folder, name), work)
            print("drew", folder)
    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    main()
