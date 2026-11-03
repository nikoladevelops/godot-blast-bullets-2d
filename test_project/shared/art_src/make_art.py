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
ART = os.path.normpath(os.path.join(HERE, "..", "art"))
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
        return g, f'<path id="glow_plume_{gid}{tag}" d="{d}" fill="url(#{gid}{tag})"/>'
    g1, p1 = tongue(length, half, "o", inner, outer, 0.95)
    g2, p2 = tongue(length * 0.55, half * 0.5, "i", "#ffffff", inner, 1.0)
    return g1 + g2, p1 + p2


# ------------------------------------------------------------------ player ship (nose up)
def player_ship(t, thrust):
    pulse = 0.5 + 0.5 * wave(t)
    defs = (lin("hull", [(0, "#6a7ca8", 1), (0.45, "#34425f", 1), (1, "#161c2c", 1)], 0, 0, 1, 1)
            + lin("wing", [(0, "#2b3654", 1), (1, "#141a29", 1)])
            + lin("glass", [(0, "#e6feff", 1), (0.5, "#5fd8f0", 1), (1, "#0f5d78", 1)])
            + rad("engine", [(0, "#ffffff", 1), (0.5, CYAN, 0.9), (1, CYAN, 0)]))
    body = (
        # Wings first (under the hull), with a darker inner panel and edge highlight.
        '<path d="M12.2 14.6 L3.6 21.4 L3.6 24.6 L12.4 22.4 Z M19.8 14.6 L28.4 21.4 L28.4 24.6 L19.6 22.4 Z" fill="url(#wing)"/>'
        '<path d="M11.8 16.4 L6 21.3 L6 22.9 L11.9 21.4 Z M20.2 16.4 L26 21.3 L26 22.9 L20.1 21.4 Z" fill="#0f1422" opacity="0.8"/>'
        # Hull.
        '<path d="M16 1.6 L18.9 9.6 L19.9 14.8 L20.2 23.2 L18.6 26.2 L13.4 26.2 L11.8 23.2 L12.1 14.8 L13.1 9.6 Z" fill="url(#hull)"/>'
        '<path d="M16 1.6 L16 26.2 L13.4 26.2 L11.8 23.2 L12.1 14.8 L13.1 9.6 Z" fill="#ffffff" opacity="0.07"/>'
        # Panel lines and rivets.
        '<path d="M13.6 16.6 L18.4 16.6 M13.4 19.6 L18.6 19.6 M16 15.2 L16 23.6" stroke="#0b0f19" stroke-width="0.22" opacity="0.8"/>'
        + "".join(f'<circle cx="{x}" cy="{y}" r="0.18" fill="#9fb0d6" opacity="0.7"/>' for x, y in ((13.9, 18.1), (18.1, 18.1), (13.9, 21.1), (18.1, 21.1)))
        # Cockpit with a glint.
        + '<ellipse cx="16" cy="10.6" rx="1.85" ry="3.3" fill="#0a0e18"/>'
        '<ellipse id="glow_cockpit" cx="16" cy="10.6" rx="1.5" ry="2.95" fill="url(#glass)"/>'
        '<ellipse cx="15.35" cy="9.4" rx="0.4" ry="1.1" fill="#ffffff" opacity="0.75"/>'
        # Neon trims along the leading edges and the spine.
        f'<path id="glow_trim" d="M12.5 15.2 L4.1 21.8 M19.5 15.2 L27.9 21.8 M13.3 9.8 L16 2.6 L18.7 9.8" fill="none" stroke="{CYAN}" stroke-width="0.55" stroke-linecap="round" stroke-linejoin="round"/>'
        # Engines: nozzles with a hot core that breathes.
        '<rect x="13.2" y="23.4" width="2.3" height="3.1" rx="0.5" fill="#0c111c"/>'
        '<rect x="16.5" y="23.4" width="2.3" height="3.1" rx="0.5" fill="#0c111c"/>'
        f'<ellipse id="glow_engine" cx="14.35" cy="26.1" rx="{0.8 + 0.2 * pulse:.3f}" ry="0.5" fill="url(#engine)"/>'
        f'<ellipse id="glow_engine2" cx="17.65" cy="26.1" rx="{0.8 + 0.2 * pulse:.3f}" ry="0.5" fill="url(#engine)"/>')
    # Navigation lights: red port, green starboard, blinking in turn.
    port = 1.0 if (t * 8) % 8 < 2 else 0.25
    star = 1.0 if ((t * 8) + 4) % 8 < 2 else 0.25
    body += (f'<circle id="glow_navl" cx="4.2" cy="23" r="0.55" fill="{RED}" opacity="{port}"/>'
             f'<circle id="glow_navr" cx="27.8" cy="23" r="0.55" fill="{GREEN}" opacity="{star}"/>')
    if thrust:
        for i, cx in enumerate((14.35, 17.65)):
            L = 4.6 + 1.1 * wave(t, 1, i * 0.37) + 0.5 * wave(t, 3, 0.2 + i * 0.5)
            d, p = flame(f"f{i}", (cx, 26.3), (0, 1), L, 0.95 + 0.12 * wave(t, 2, i * 0.3), CYAN, "#bffbff", 0.5 * wave(t, 2, i * 0.25))
            defs, body = defs + d, p + body
    else:
        for i, cx in enumerate((14.35, 17.65)):
            d, p = flame(f"f{i}", (cx, 26.3), (0, 1), 1.6 + 0.35 * pulse, 0.7, CYAN, "#bffbff")
            defs, body = defs + d, p + body
    glows = {"glow_trim": (CYAN, 0.9, 85), "glow_cockpit": (CYAN, 1.0, 60), "glow_engine": (CYAN, 1.2, 90),
             "glow_engine2": (CYAN, 1.2, 90), "glow_plume": (CYAN, 1.3, 75), "glow_navl": (RED, 0.9, 90 * port),
             "glow_navr": (GREEN, 0.9, 90 * star)}
    return svg(body, defs), glows


# ------------------------------------------------------------------ enemy ship (nose up)
def enemy_ship(t, thrust):
    pulse = 0.5 + 0.5 * wave(t)
    defs = (lin("hull", [(0, "#a03d63", 1), (0.5, "#5a1f37", 1), (1, "#260a17", 1)], 0, 0, 1, 1)
            + lin("plate", [(0, "#6e2744", 1), (1, "#2a0c1a", 1)])
            + rad("core", [(0, "#ffffff", 1), (0.35, "#ffb3cc", 1), (1, MAGENTA, 1)]))
    r = 2.1 + 0.35 * pulse
    body = (
        '<path d="M16 3.4 L19.2 10.6 L25.4 8 L29.8 15.6 L23.6 18.2 L20.2 24.4 L11.8 24.4 L8.4 18.2 L2.2 15.6 L6.6 8 L12.8 10.6 Z" fill="url(#hull)"/>'
        '<path d="M7.2 9.6 L12.6 11.8 L10.6 17 L4.2 15.2 Z M24.8 9.6 L19.4 11.8 L21.4 17 L27.8 15.2 Z" fill="url(#plate)"/>'
        '<path d="M16 3.4 L19.2 10.6 L16 12.4 L12.8 10.6 Z" fill="#c25079" opacity="0.6"/>'
        '<path d="M10.6 17 L12 21.6 M21.4 17 L20 21.6 M13 13 L19 13" stroke="#14050b" stroke-width="0.22" opacity="0.85"/>'
        + "".join(f'<circle cx="{x}" cy="{y}" r="0.2" fill="#ffa3c2" opacity="0.6"/>' for x, y in ((8.2, 12), (23.8, 12), (12.6, 20.4), (19.4, 20.4)))
        + f'<path id="glow_trim" d="M2.7 15.6 L6.9 8.5 L12.8 10.9 M29.3 15.6 L25.1 8.5 L19.2 10.9 M12 23.8 L16 19.8 L20 23.8" fill="none" stroke="{MAGENTA}" stroke-width="0.55" stroke-linecap="round" stroke-linejoin="round"/>'
        '<circle cx="16" cy="15" r="3.1" fill="#12050a"/>'
        f'<circle id="glow_core" cx="16" cy="15" r="{r:.3f}" fill="url(#core)"/>'
        '<rect x="14.4" y="23" width="3.2" height="2.2" rx="0.5" fill="#12060c"/>')
    eyes = 0.55 + 0.45 * (0.5 + 0.5 * wave(t, 2, 0.25))
    body += "".join(f'<circle id="glow_eye{i}" cx="{x}" cy="9.4" r="0.45" fill="{ORANGE}" opacity="{eyes:.2f}"/>' for i, x in enumerate((9.6, 22.4)))
    L = (5.4 + 1.2 * wave(t, 1) + 0.5 * wave(t, 3, 0.3)) if thrust else (1.8 + 0.4 * pulse)
    d, p = flame("f", (16, 24.9), (0, 1), L, 1.35 + (0.15 * wave(t, 2) if thrust else 0), ORANGE, "#ffd9a8", 0.45 * wave(t, 2, 0.1) if thrust else 0)
    glows = {"glow_trim": (MAGENTA, 0.9, 85), "glow_core": (MAGENTA, 1.4 + 0.4 * pulse, 90), "glow_plume": (ORANGE, 1.3, 75),
             "glow_eye0": (ORANGE, 0.8, 80), "glow_eye1": (ORANGE, 0.8, 80)}
    return svg(p + body, defs + d), glows


# ------------------------------------------------------------------ turret
def turret(t):
    defs = (lin("metal", [(0, "#7c87a3", 1), (0.5, "#3e475c", 1), (1, "#232837", 1)], 0, 0, 1, 0)
            + lin("body", [(0, "#525b72", 1), (1, "#20242f", 1)]))
    body = ('<rect x="13.3" y="4.6" width="5.4" height="13.6" rx="1" fill="url(#metal)"/>'
            '<rect x="14.4" y="5.2" width="0.9" height="12.4" fill="#a6b0c8" opacity="0.6"/>'
            '<rect x="13.3" y="9" width="5.4" height="0.6" fill="#1a1e29"/><rect x="13.3" y="12.4" width="5.4" height="0.6" fill="#1a1e29"/>'
            f'<rect id="glow_muzzle" x="12.8" y="2.8" width="6.4" height="2.7" rx="0.8" fill="{RED}"/>'
            '<path d="M8 19 Q8 15.2 11.8 15.2 L20.2 15.2 Q24 15.2 24 19 L24 25 Q24 27.6 21.4 27.6 L10.6 27.6 Q8 27.6 8 25 Z" fill="url(#body)"/>'
            '<path d="M9.4 17.2 L22.6 17.2" stroke="#7c87a3" stroke-width="0.4" opacity="0.8"/>'
            f'<path id="glow_trim" d="M11 23.6 L21 23.6" stroke="{RED}" stroke-width="0.8" stroke-linecap="round"/>'
            '<circle cx="10.6" cy="20" r="0.7" fill="#0d1017"/><circle cx="21.4" cy="20" r="0.7" fill="#0d1017"/>')
    return svg(body, defs), {"glow_muzzle": (RED, 1.3, 85), "glow_trim": (RED, 0.9, 85)}


def turret_holder(t):
    pts = " ".join("%.2f,%.2f" % (16 + 12.6 * math.cos(math.pi / 8 + k * math.pi / 4), 16 + 12.6 * math.sin(math.pi / 8 + k * math.pi / 4)) for k in range(8))
    inner = " ".join("%.2f,%.2f" % (16 + 7 * math.cos(math.pi / 8 + k * math.pi / 4), 16 + 7 * math.sin(math.pi / 8 + k * math.pi / 4)) for k in range(8))
    bolts = "".join('<circle cx="%.2f" cy="%.2f" r="0.7" fill="#a8b2cb"/>' % (16 + 10.6 * math.cos(k * math.pi / 2 + math.pi / 4), 16 + 10.6 * math.sin(k * math.pi / 2 + math.pi / 4)) for k in range(4))
    defs = rad("plate", [(0, "#5f6987", 1), (1, "#272c3c", 1)])
    body = (f'<polygon points="{pts}" fill="url(#plate)" stroke="#121520" stroke-width="0.35"/>'
            f'<circle id="glow_ring" cx="16" cy="16" r="9" fill="none" stroke="{RED}" stroke-width="0.7"/>'
            f'<polygon points="{inner}" fill="#363d52" stroke="#7a84a0" stroke-width="0.3"/>' + bolts)
    return svg(body, defs), {"glow_ring": (RED, 1.2, 85)}


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
            f'<path id="glow_fin" d="M7.5 10.3 L8.7 10.3 L11.6 13.7 M7.5 21.7 L8.7 21.7 L11.6 18.3" fill="none" stroke="{stripe}" stroke-width="0.4"/>'
            f'<rect id="glow_stripe" x="12.5" y="13.7" width="1.6" height="4.6" fill="{stripe}" opacity="{blink:.2f}"/>'
            '<rect x="15.2" y="13.7" width="0.5" height="4.6" fill="#0d1220" opacity="0.5"/>'
            '<path d="M24.5 14.3 Q27.6 14.9 29.2 16 Q27.6 17.1 24.5 17.7 Z" fill="url(#nose)"/>')
    L = 5.6 + 1.3 * wave(t, 1) + 0.6 * wave(t, 3, 0.35)
    d, p = flame("fl", (8.1, 16), (-1, 0), L, 1.9 + 0.2 * wave(t, 2, 0.2), flame_col, flame_in, 0.55 * wave(t, 2, 0.15))
    return svg(p + body, defs + d), {"glow_stripe": (stripe, 0.8, 80), "glow_fin": (stripe, 0.6, 70), "glow_plume": (flame_col, 1.2, 75)}


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
    steps = []
    for part, (color, blur, opacity) in glows.items():
        if opacity <= 0.5:
            continue
        steps += [{"command": "document.find", "params": {"name": part, "select": True}},
                  {"command": "effect.apply", "params": {"effect": "stylize.outerGlow",
                                                         "params": {"blur": blur, "color": color, "opacity": opacity, "mode": "screen"}}},
                  {"command": "select.none", "params": {}}]
    plan = os.path.join(work, stem + ".jsonl")
    open(plan, "w").write("\n".join(json.dumps(s) for s in steps) + "\n")
    out = vectorcraft(["run", "--in", src, "--commands", plan, "--export", dst, "--scale", str(SCALE)], work)
    for line in out.splitlines():
        r = json.loads(line)
        if r.get("command") == "document.find" and not r["result"]["matches"]:
            name = steps[[i for i, s in enumerate(steps) if s["command"] == "document.find"][0]]["params"]["name"]
            sys.exit(f"{dst}: a glow part was not found ({name})")


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
