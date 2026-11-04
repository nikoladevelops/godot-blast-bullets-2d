"""The test project's effects and backgrounds (shared/art/effects, shared/art/background):

  explosion_NN.png   16 frames, 256 px: flash, a noise-shaped fireball cooling from
                     white to orange to smoke, a shockwave ring and spark streaks
  hit_spark_NN.png    8 frames, 128 px: an impact burst: flash, shock ring, tapered sparks (bullet hits)
  laser_impact_NN.png 8 frames, 128 px: a laser's wall / target impact, sparks thrown back along -X
  muzzle_flash_NN.png 4 frames, 128 px: a cone flash pointing right (spawn flashes)
  laser_beam_NN.png   4 frames, 256 x 64: a beam body that tiles along x
  laser_orb_NN.png    8 frames, 128 px: a pulsing orb (laser muzzle and tip)
  nebula.png         2048 px, tiles seamlessly (periodic FFT noise), finished in PhotoCraft
  stars.png          1024 px, tiles seamlessly, transparent (a parallax layer)

Everything is drawn 2x larger and downsampled (clean edges). Deterministic.

    python3 test_project/shared/art_src/make_effects.py [names]   (ART_OUT=<dir> previews elsewhere)
"""
import os
import subprocess
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ART = os.environ.get("ART_OUT") or os.path.normpath(os.path.join(HERE, "..", "art"))


def save(rgba, path, size):
    img = Image.fromarray((np.clip(rgba, 0, 1) * 255).astype(np.uint8), "RGBA")
    if img.size[0] != size:
        img = img.resize((size, size), Image.Resampling.LANCZOS)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    img.save(path)


def periodic_noise(n, beta, seed):
    """Tileable fractal noise: white noise shaped by 1/f^beta in the frequency domain."""
    rng = np.random.default_rng(seed)
    f = np.fft.fftfreq(n)
    fx, fy = np.meshgrid(f, f)
    amp = 1.0 / np.maximum(np.hypot(fx, fy), 1.0 / n) ** beta
    spec = np.fft.fft2(rng.standard_normal((n, n))) * amp
    x = np.real(np.fft.ifft2(spec))
    return (x - x.min()) / (x.max() - x.min())


def ramp(v, stops):
    """Colour ramp: stops = [(t, (r, g, b, a)), ...] -> (..., 4)."""
    ts = np.array([s[0] for s in stops])
    cs = np.array([s[1] for s in stops], dtype=np.float64)
    out = np.empty(v.shape + (4,))
    for c in range(4):
        out[..., c] = np.interp(v, ts, cs[:, c])
    return out


def over(dst, src):
    a = src[..., 3:4]
    rgb = src[..., :3] * a + dst[..., :3] * dst[..., 3:4] * (1 - a)
    alpha = a + dst[..., 3:4] * (1 - a)
    return np.concatenate([np.where(alpha > 1e-6, rgb / np.maximum(alpha, 1e-6), 0), alpha], axis=-1)


def add_light(dst, rgb, intensity):
    """Additive glow (alpha grows with the light)."""
    out = dst.copy()
    out[..., :3] = out[..., :3] * out[..., 3:4] + rgb * intensity[..., None]
    out[..., 3] = np.clip(out[..., 3] + intensity, 0, 1)
    out[..., :3] = np.where(out[..., 3:4] > 1e-6, out[..., :3] / np.maximum(out[..., 3:4], 1e-6), 0)
    return out


# ------------------------------------------------------------------ explosion
def explosion():
    n, frames = 512, 16
    yy, xx = (np.mgrid[0:n, 0:n] - n / 2 + 0.5) / (n / 2)
    r = np.hypot(xx, yy)
    ang = np.arctan2(yy, xx)
    turb = periodic_noise(n, 1.6, 11)
    rng = np.random.default_rng(5)
    sparks = [(rng.uniform(0, 2 * np.pi), rng.uniform(0.55, 1.0), rng.uniform(0.006, 0.012)) for _ in range(28)]
    for k in range(frames):
        t = k / (frames - 1)
        img = np.zeros((n, n, 4))
        # Fireball: radius eases out, edge distorted by noise that drifts outward.
        R = 0.12 + 0.62 * (1 - (1 - t) ** 2.4)
        edge = R * (0.78 + 0.32 * np.roll(turb, int(40 * t), axis=0))
        inside = np.clip((edge - r) / (0.18 * R + 1e-3), 0, 1)
        heat = np.clip(1.15 - t * 1.25 - r / (R + 1e-3) * 0.6 + (turb - 0.5) * 0.5, 0, 1)
        fire = ramp(heat, [(0, (0.10, 0.08, 0.09, 0.0)), (0.12, (0.25, 0.12, 0.10, 0.75)),
                           (0.35, (0.85, 0.22, 0.08, 0.95)), (0.6, (1.0, 0.55, 0.12, 1.0)),
                           (0.85, (1.0, 0.86, 0.45, 1.0)), (1.0, (1.0, 1.0, 0.92, 1.0))])
        smoke_fade = np.clip(1.4 - t * 1.3, 0, 1)
        fire[..., 3] *= inside * smoke_fade
        img = over(img, fire)
        # Shockwave ring.
        ring_r = 0.2 + 0.85 * t ** 0.7
        ring = np.exp(-((r - ring_r) / (0.025 + 0.02 * t)) ** 2) * (1 - t) ** 1.5
        img = add_light(img, np.array([1.0, 0.75, 0.45]), ring * 0.8)
        # Spark streaks flying out.
        for a, speed, w in sparks:
            d = speed * (0.15 + 0.95 * t ** 0.6)
            length = 0.12 * (1 - t)
            along = r * np.cos(ang - a)
            across = r * np.abs(np.sin(ang - a))
            m = np.exp(-(across / w) ** 2) * np.clip(1 - np.abs(along - d) / max(length, 1e-3), 0, 1) * (along > 0)
            img = add_light(img, np.array([1.0, 0.85, 0.5]), m * (1 - t) ** 1.2)
        # Opening flash.
        flash = np.exp(-(r / (0.25 + 0.4 * t)) ** 2) * max(0.0, 1 - t * 4) ** 2
        img = add_light(img, np.array([1.0, 1.0, 0.95]), flash)
        save(img, os.path.join(ART, "effects", f"explosion_{k + 1:02d}.png"), 256)


def burst(n, t, rays, ray_rgb, ring_rgb, core_rgb, ray_width=0.028, reach=0.95):
    """One frame (t in 0..1) of an impact burst: a white-hot flash, an
    expanding shock ring and tapered sparks with hot heads flying out."""
    yy, xx = (np.mgrid[0:n, 0:n] - n / 2 + 0.5) / (n / 2)
    r = np.hypot(xx, yy)
    ang = np.arctan2(yy, xx)
    img = np.zeros((n, n, 4))
    fade = (1 - t) ** 1.3
    # Shock ring.
    ring_r = 0.18 + 0.7 * t ** 0.6
    ring = np.exp(-((r - ring_r) / (0.03 + 0.05 * t)) ** 2) * (1 - t) ** 1.6
    img = add_light(img, ring_rgb, ring * 0.45)
    # Sparks: tapered streaks, thickest at the head, white near the head.
    for a, speed, width in rays:
        head = speed * reach * (0.3 + 0.7 * (1 - (1 - t) ** 2.0))
        length = 0.55 * (1 - t) ** 0.8 + 0.08
        along = r * np.cos(ang - a)
        across = r * np.abs(np.sin(ang - a))
        u = np.clip((along - (head - length)) / length, 0, 1)  # 0 at the tail, 1 at the head
        inside = (along <= head + 0.02) & (along > 0)
        w = ray_width * width * (0.12 + 0.88 * u)
        m = np.exp(-(across / np.maximum(w, 1e-4)) ** 2) * u ** 0.5 * inside
        img = add_light(img, ray_rgb, np.clip(m * fade * 1.3, 0, 1))
        img = add_light(img, np.array([1.0, 1.0, 1.0]), m * u ** 2 * fade)
    # Core flash (first frames) and a soft glow.
    core = np.exp(-(r / (0.16 + 0.22 * t)) ** 2) * (1 - t) ** 2.4
    glow = np.exp(-(r / (0.45 + 0.2 * t)) ** 2) * (1 - t) ** 2 * 0.45
    img = add_light(img, core_rgb, glow)
    img = add_light(img, np.array([1.0, 1.0, 1.0]), np.clip(core * 1.4, 0, 1))
    return img


def hit_spark():
    """8 frames, 128 px: a bright impact burst (bullet hits; tinted by each
    effect's self_modulate, so the art is near white with a cool edge)."""
    n, frames = 256, 8
    rng = np.random.default_rng(9)
    rays = [(a + rng.uniform(-0.18, 0.18), rng.uniform(0.6, 1.0), rng.uniform(0.7, 1.3)) for a in np.linspace(0, 2 * np.pi, 11, endpoint=False)]
    for k in range(frames):
        t = k / (frames - 1)
        img = burst(n, t, rays, np.array([0.8, 0.97, 1.0]), np.array([0.7, 0.95, 1.0]), np.array([0.85, 0.97, 1.0]))
        save(img, os.path.join(ART, "effects", f"hit_spark_{k + 1:02d}.png"), 128)


def laser_impact():
    """8 frames, 128 px: where a laser meets a wall or a target (wall_impact_look,
    pierce_look): a hot flash with sparks thrown back (+X is along the beam,
    so the sparks fan out toward -X, back at the shooter)."""
    n, frames = 256, 8
    rng = np.random.default_rng(13)
    rays = [(np.pi + rng.uniform(-1.25, 1.25), rng.uniform(0.55, 1.0), rng.uniform(0.7, 1.2)) for _ in range(11)]
    for k in range(frames):
        t = k / (frames - 1)
        img = burst(n, t, rays, np.array([1.0, 0.92, 0.85]), np.array([1.0, 0.9, 0.95]), np.array([1.0, 0.95, 1.0]), ray_width=0.024, reach=0.92)
        save(img, os.path.join(ART, "effects", f"laser_impact_{k + 1:02d}.png"), 128)


def muzzle_flash():
    n, frames = 256, 4
    yy, xx = (np.mgrid[0:n, 0:n] - n / 2 + 0.5) / (n / 2)
    for k in range(frames):
        t = k / frames
        length = 0.9 - 0.5 * t
        x = (xx + 0.6) / length
        cone = np.clip(1 - x, 0, 1) * (x > 0) * np.exp(-(yy / (0.08 + 0.25 * np.clip(x, 0, 1))) ** 2)
        core = np.exp(-(((xx + 0.55) / 0.18) ** 2 + (yy / 0.12) ** 2))
        img = add_light(np.zeros((n, n, 4)), np.array([0.6, 0.97, 1.0]), cone * (1 - t * 0.7))
        img = add_light(img, np.array([1.0, 1.0, 1.0]), core * (1 - t))
        save(img, os.path.join(ART, "effects", f"muzzle_flash_{k + 1:02d}.png"), 128)


def laser_beam():
    """4 frames, 256 x 64: a white-hot core in a soft glow with energy ripples that tile
    along x (periodic noise), so a tiled, scrolling body has no seams. Tinted by the laser."""
    w, h, frames = 512, 128, 4
    y = (np.arange(h) - h / 2 + 0.5) / (h / 2)
    flow = periodic_noise(w, 1.4, 21)  # square periodic field: use rows of it
    for k in range(frames):
        t = k / frames
        img = np.zeros((h, w, 4))
        rows = flow[(np.arange(h) * 3 + int(t * w)) % w][:, :w]
        shifted = np.roll(rows, int(t * w), axis=1)
        wob = (shifted - 0.5) * 0.18
        glow = np.exp(-((y[:, None] + wob) / 0.55) ** 2) * (0.55 + 0.45 * shifted)
        core = np.exp(-((y[:, None] + wob * 0.4) / 0.16) ** 2)
        img = add_light(img, np.array([0.85, 0.9, 1.0]), glow * 0.8)
        img = add_light(img, np.array([1.0, 1.0, 1.0]), core)
        out = Image.fromarray((np.clip(img, 0, 1) * 255).astype(np.uint8), "RGBA").resize((256, 64), Image.Resampling.LANCZOS)
        path = os.path.join(ART, "effects", f"laser_beam_{k + 1:02d}.png")
        out.save(path)


def laser_orb():
    """8 frames, 128 px: a pulsing white-hot orb in a tinted halo with a slowly
    turning four-point flare (muzzle and tip looks; the laser's modulate tints it)."""
    n, frames = 256, 8
    yy, xx = (np.mgrid[0:n, 0:n] - n / 2 + 0.5) / (n / 2)
    r = np.hypot(xx, yy)
    ang = np.arctan2(yy, xx)
    for k in range(frames):
        t = k / frames
        pulse = 0.9 + 0.1 * np.sin(2 * np.pi * t)
        img = np.zeros((n, n, 4))
        halo = np.exp(-(r / (0.5 * pulse)) ** 2) * 0.75
        corona = np.exp(-(r / (0.26 * pulse)) ** 2)
        core = np.exp(-(r / (0.11 * pulse)) ** 2)
        spin = t * np.pi / 2
        flare = sum(np.exp(-(np.abs(np.sin(ang - spin - q * np.pi / 2)) * r / 0.022) ** 2) for q in range(2)) * np.clip(1 - r / 0.95, 0, 1) ** 2.2
        diag = sum(np.exp(-(np.abs(np.sin(ang - spin - np.pi / 4 - q * np.pi / 2)) * r / 0.018) ** 2) for q in range(2)) * np.clip(1 - r / 0.55, 0, 1) ** 2.5 * 0.5
        img = add_light(img, np.array([0.75, 0.88, 1.0]), halo)
        img = add_light(img, np.array([0.92, 0.97, 1.0]), corona * 0.9)
        img = add_light(img, np.array([1.0, 1.0, 1.0]), np.clip(core * 1.5 + flare * 0.85 + diag, 0, 1))
        save(img, os.path.join(ART, "effects", f"laser_orb_{k + 1:02d}.png"), 128)


# ------------------------------------------------------------------ backgrounds
def nebula(work):
    n = 2048
    a = periodic_noise(n, 1.9, 1)
    b = periodic_noise(n, 2.2, 2)
    c = periodic_noise(n, 1.5, 3)
    base = np.stack([np.full((n, n), 0.020), np.full((n, n), 0.027), np.full((n, n), 0.055)], axis=-1)
    violet = np.array([0.36, 0.14, 0.52])
    teal = np.array([0.06, 0.36, 0.48])
    rose = np.array([0.55, 0.12, 0.32])
    cloud_a = np.clip((a - 0.45) * 2.4, 0, 1) ** 1.6
    cloud_b = np.clip((b - 0.5) * 2.6, 0, 1) ** 1.8
    wisps = np.clip((c - 0.55) * 3.0, 0, 1) ** 2.2
    rgb = base + cloud_a[..., None] * violet * 0.55 + cloud_b[..., None] * teal * 0.5 + (wisps * cloud_a)[..., None] * rose * 0.6
    # Faint stars baked in (the bright ones are the parallax layer).
    rng = np.random.default_rng(4)
    for _ in range(2600):
        y, x = rng.integers(0, n, 2)
        rgb[y, x] += rng.uniform(0.15, 0.45)
    raw = os.path.join(work, "nebula_raw.png")
    Image.fromarray((np.clip(rgb, 0, 1) * 255).astype(np.uint8), "RGB").save(raw)
    out = os.path.join(ART, "background", "nebula.png")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    # PhotoCraft: a touch more colour and a fine film grain (no banding in the dark gradients).
    r = subprocess.run(["flatpak", "run", "--filesystem=" + work, "--filesystem=" + os.path.dirname(out),
                        "--command=photocraft-cli", "ai.storyteller.photocraft", "run", raw,
                        "--cmd", "image.adjustments.vibrance", "--params", '{"vibrance": 25, "saturation": 8}',
                        "--cmd", "filter.noise.addNoise", "--params", '{"amount": 1.2, "distribution": "gaussian", "monochromatic": true, "seed": 3}',
                        "--out", out], capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit("photocraft failed:\n" + r.stdout[-800:] + r.stderr[-800:])


def stars():
    n = 2048  # drawn 2x, saved at 1024
    img = np.zeros((n, n, 4))
    rng = np.random.default_rng(8)
    yy, xx = np.mgrid[-12:13, -12:13]
    for _ in range(420):
        cy, cx = rng.integers(0, n, 2)
        size = rng.choice([1.2, 1.6, 2.2, 3.2], p=[0.5, 0.3, 0.15, 0.05])
        tint = rng.choice([[1, 1, 1], [0.75, 0.9, 1.0], [1.0, 0.85, 0.7], [0.8, 1.0, 1.0]])
        glow = np.exp(-(xx ** 2 + yy ** 2) / (2 * size ** 2)) + 0.25 * np.exp(-(xx ** 2 + yy ** 2) / (2 * (size * 3) ** 2))
        if size >= 2.2:  # a soft cross on the bright ones
            glow += 0.35 * np.exp(-(yy ** 2) / 1.2 - (xx ** 2) / (size * 30)) + 0.35 * np.exp(-(xx ** 2) / 1.2 - (yy ** 2) / (size * 30))
        ys, xs = (yy + cy) % n, (xx + cx) % n  # wrap: the layer tiles
        a = np.clip(img[ys, xs, 3] + glow, 0, 1)
        img[ys, xs, :3] = tint
        img[ys, xs, 3] = a
    save(img, os.path.join(ART, "background", "stars.png"), 1024)


def main():
    import tempfile
    only = sys.argv[1:]
    work = tempfile.mkdtemp(prefix="fx_", dir=os.environ.get("ART_WORK"))
    for name, make in (("explosion", explosion), ("hit_spark", hit_spark), ("laser_impact", laser_impact), ("muzzle_flash", muzzle_flash),
                       ("laser_beam", laser_beam), ("laser_orb", laser_orb), ("nebula", lambda: nebula(work)), ("stars", stars)):
        if not only or name in only:
            make()
    print("effects and backgrounds written to", ART)


if __name__ == "__main__":
    main()
