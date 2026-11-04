"""The test project's synthesized sounds (shared/sound): deterministic, no samples needed.

New varied sets (every file: mono, 44100 Hz, 16-bit PCM WAV):
  hit_01.wav .. hit_08.wav                0.12-0.30 s bullet impacts
  laser_fire_01.wav .. laser_fire_06.wav  0.35-0.60 s sci-fi laser shots
  laser_impact_01.wav .. laser_impact_04.wav  0.20-0.40 s beam-on-surface sizzles
  explosion_01.wav .. explosion_04.wav    0.70-1.20 s explosions
The existing hit.wav, laser_fire.wav and explosion.wav are left untouched.

SoundCraft chain per file (headless flatpak `ai.storyteller.soundcraft`, `soundcraft-cli`):
  session.new -> track.new -> file.import_audio -> mix.insert (saturator, eq_7band,
  compressor, room_reverb/plate_reverb, maximizer, plus ring_mod/phaser/lofi/mod_delay/
  chorus/pitch_shifter/subharmonic on selected variants) -> file.bounce_mix (24-bit,
  44.1 kHz, dither off).  An optional subtractive_synth layer is added alongside the
  audio track through track.new/mix.instrument/midi.new_clip/midi.note_add (explosions
  use it for sub-bass).  The bounce is folded to mono, silence-trimmed, faded and
  peak-normalized to -1 dBFS, then written as the final 16-bit PCM WAV.

    python3 test_project/shared/art_src/make_sounds.py
"""
import json
import os
import shutil
import subprocess
import tempfile
import wave

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(HERE, "..", "sound"))
ROOT = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
RATE = 44100
PEAK_DB = -1.0
TARGET_PEAK = 10.0 ** (PEAK_DB / 20.0)


def frame_count(seconds):
    return int(round(seconds * RATE))


def normalize(x, peak=1.0):
    m = np.max(np.abs(x))
    return x if m < 1e-12 else x / m * peak


def attack(t, ms):
    return np.minimum(1.0, t / (ms * 1e-3))


def tail_fade(x, ms=10.0):
    x = x.copy()
    k = min(len(x), int(ms * 1e-3 * RATE))
    if k:
        x[-k:] *= np.linspace(1.0, 0.0, k) ** 3
    return x


def lowpass(x, cutoff):
    a = 1.0 - np.exp(-2 * np.pi * np.asarray(cutoff) / RATE)
    a = np.broadcast_to(a, x.shape)
    y = np.empty_like(x)
    acc = 0.0
    for i in range(len(x)):
        acc += a[i] * (x[i] - acc)
        y[i] = acc
    return y


def fft_band(x, low=None, high=None, order=2):
    n = len(x)
    spec = np.fft.rfft(x)
    f = np.fft.rfftfreq(n, 1.0 / RATE)
    h = np.ones_like(f)
    if low is not None:
        h *= 1.0 / np.sqrt(1.0 + (low / np.maximum(f, 1e-6)) ** (2 * order))
    if high is not None:
        h *= 1.0 / np.sqrt(1.0 + (f / high) ** (2 * order))
    return np.fft.irfft(spec * h, n)


def crackle(rng, n, rate, decay=0.0012):
    count = max(1, int(rate * n / RATE))
    imp = np.zeros(n)
    idx = rng.integers(0, n, count)
    imp[idx] += rng.uniform(0.3, 1.0, count) * rng.choice([-1.0, 1.0], count)
    k = max(1, int(decay * RATE * 4))
    kernel = np.exp(-np.arange(k) / max(1.0, decay * RATE))
    return normalize(np.convolve(imp, kernel)[:n])


def colored_noise(rng, n, color):
    if color == "crackle":
        return crackle(rng, n, 1200, 0.0008)
    x = rng.standard_normal(n)
    if color == "white":
        pass
    elif color == "pink":
        spec = np.fft.rfft(x)
        f = np.fft.rfftfreq(n, 1.0 / RATE)
        spec /= np.sqrt(np.maximum(f, 20.0))
        x = np.fft.irfft(spec, n)
    elif color == "bright":
        x = fft_band(x, 3000, 14000)
    elif color == "air":
        x = fft_band(x, 6000, 17000)
    elif color == "band":
        x = fft_band(x, 1200, 7000)
    elif color == "dark":
        x = fft_band(x, 80, 1800)
    else:
        raise ValueError(color)
    return normalize(x)


def metallic(t, rng, base, decay):
    ratios = np.array([1.0, 1.47, 1.83, 2.31, 2.87, 3.41])
    ratios *= 1.0 + rng.uniform(-0.008, 0.008, len(ratios))
    amps = np.array([1.0, 0.75, 0.58, 0.44, 0.32, 0.22])
    phases = rng.uniform(0, 2 * np.pi, len(ratios))
    y = np.zeros_like(t)
    for i in range(len(ratios)):
        freq = base * ratios[i]
        if freq >= RATE * 0.45:
            continue
        y += amps[i] * np.sin(2 * np.pi * freq * t + phases[i]) * np.exp(-t * decay * (1.0 + 0.18 * i))
    return normalize(y)


def sweep_freq(t, f0, f1, curve, p=1.0):
    u = t / max(t[-1], 1e-9)
    if curve == "linear":
        w = u
    elif curve == "exp":
        w = u ** p
    elif curve == "s":
        w = u * u * (3.0 - 2.0 * u)
    else:
        raise ValueError(curve)
    return f0 * (f1 / f0) ** w


def waveform(phase, kind, t, spec):
    if kind == "saw":
        y = 2.0 * np.mod(phase / (2 * np.pi), 1.0) - 1.0
    elif kind == "square":
        y = np.sign(np.sin(phase))
    elif kind == "sine":
        y = np.sin(phase)
    elif kind == "fm":
        y = np.sin(phase + spec.get("fm_index", 1.5) * np.sin(phase * spec.get("fm_ratio", 2.0)))
    elif kind == "dual":
        y = 0.65 * np.sin(phase) + 0.35 * np.sin(phase * 2.0 + 0.7)
    elif kind == "ring":
        y = np.sin(phase) * np.sin(2 * np.pi * spec["ring_hz"] * t)
    else:
        raise ValueError(kind)
    return normalize(y)


def synth_hit(s):
    n = frame_count(s["dur"])
    t = np.arange(n) / RATE
    rng = np.random.default_rng(s["seed"])
    tr = colored_noise(rng, n, s["noise_color"]) * np.exp(-t / s["transient_decay"]) * attack(t, 0.15)
    click = rng.standard_normal(n) * np.exp(-t / 0.0005)
    tail = metallic(t, rng, s["tail_hz"], s["tail_decay"]) * np.exp(-t / s["tail_env"]) * attack(t, 0.3)
    f = s["body_hz"] * (1.0 + s["body_drop"] * np.exp(-t / 0.012))
    phase = 2 * np.pi * np.cumsum(f) / RATE
    body = np.sin(phase) * np.exp(-t / s["body_decay"]) * attack(t, 0.3)
    hiss = colored_noise(rng, n, s["hiss_color"]) * np.exp(-t / s["hiss_decay"])
    x = (s["w_tr"] * tr + s["w_click"] * normalize(click) + s["w_tail"] * tail
         + s["w_body"] * normalize(body) + s["w_hiss"] * hiss)
    x *= attack(t, 0.2)
    return tail_fade(x, 8)


def synth_laser_fire(s):
    n = frame_count(s["dur"])
    t = np.arange(n) / RATE
    rng = np.random.default_rng(s["seed"])
    f = sweep_freq(t, s["f0"], s["f1"], s["curve"], s.get("p", 1.0))
    phase = 2 * np.pi * np.cumsum(f) / RATE
    env = attack(t, 0.6) * np.exp(-t / s["decay"])
    tone = waveform(phase, s["wave"], t, s)
    if s.get("sub", 0.0) > 0:
        tone = normalize(tone + s["sub"] * np.sin(phase * 0.5))
    start = colored_noise(rng, n, "air") * np.exp(-t / 0.004)
    crack = fft_band(crackle(rng, n, s["crackle_rate"], 0.0007), 3500, 16000)
    crack *= np.exp(-t / s.get("crackle_decay", 0.12))
    x = 0.9 * tone * env + 0.22 * normalize(start)
    if s["crackle_amount"] > 0:
        x += s["crackle_amount"] * normalize(crack)
    x *= attack(t, 0.25)
    return tail_fade(x, 8)


def synth_laser_impact(s):
    n = frame_count(s["dur"])
    t = np.arange(n) / RATE
    rng = np.random.default_rng(s["seed"])
    sizzle = colored_noise(rng, n, s["noise_color"])
    sizzle = fft_band(sizzle, s["center"] * 0.55, min(18000.0, s["center"] * 1.8))
    sizzle *= 0.55 + 0.45 * np.sin(2 * np.pi * s["am_hz"] * t + rng.uniform(0, 2 * np.pi))
    sizzle *= np.exp(-t / s["sizzle_decay"]) * attack(t, 0.2)
    ring = metallic(t, rng, s["ring_hz"], s["ring_decay"]) * np.exp(-t / s["ring_env"])
    thud_f = s["thud_hz"] * (1.0 + 0.8 * np.exp(-t / 0.010))
    thud = np.sin(2 * np.pi * np.cumsum(thud_f) / RATE) * np.exp(-t / s["thud_decay"]) * attack(t, 0.2)
    crack = fft_band(crackle(rng, n, s["crackle_rate"], 0.0006), 2500, 16000)
    crack *= np.exp(-t / s.get("crackle_decay", 0.15))
    x = (s["w_sizzle"] * sizzle + s["w_ring"] * ring + s["w_thud"] * thud
         + s["w_crackle"] * crack)
    x *= attack(t, 0.2)
    return tail_fade(x, 8)


def synth_explosion(s):
    n = frame_count(s["dur"])
    t = np.arange(n) / RATE
    rng = np.random.default_rng(s["seed"])
    f = s["boom_end"] + (s["boom_start"] - s["boom_end"]) * np.exp(-t / s["boom_pitch_decay"])
    boom = np.tanh(1.6 * np.sin(2 * np.pi * np.cumsum(f) / RATE)) * np.exp(-t / s["boom_decay"])
    sub = np.sin(2 * np.pi * s["sub_hz"] * t) * np.exp(-t / s["sub_decay"])
    noise = rng.standard_normal(n)
    cutoff = s["noise_lo"] + (s["noise_hi"] - s["noise_lo"]) * np.exp(-t / s["noise_decay"])
    rumble = lowpass(noise, cutoff) * np.exp(-t / s["rumble_decay"])
    roar = fft_band(rng.standard_normal(n), s["roar_lo"], s["roar_hi"]) * np.exp(-t / s["roar_decay"])
    debris = fft_band(crackle(rng, n, s["debris_rate"], 0.0015), s["debris_lo"], s["debris_hi"])
    debris *= np.exp(-t / s["debris_decay"])
    click = rng.standard_normal(n) * np.exp(-t / 0.003)
    x = (s["w_boom"] * normalize(boom) + s["w_sub"] * normalize(sub)
         + s["w_rumble"] * normalize(rumble) + s["w_roar"] * normalize(roar)
         + s["w_debris"] * normalize(debris) + s.get("click", 0.5) * normalize(click))
    # One overall body decay: an explosion booms, then dies away (the chain's
    # compressor and maximizer would otherwise hold it level to the end).
    x = np.tanh(x * 0.8) * attack(t, 0.3) * np.exp(-t / s.get("body_decay", 0.32))
    return tail_fade(x, 10)


def ins(plugin, **params):
    return {"plugin": plugin, "params": params}


HITS = [
    dict(seed=1101, dur=0.140, tail_hz=420, tail_decay=16.0, tail_env=0.055,
         body_hz=105, body_drop=0.60, body_decay=0.028,
         noise_color="bright", transient_decay=0.0035, click=0.55,
         hiss_color="air", hiss_decay=0.030,
         w_tr=1.00, w_click=0.45, w_tail=0.95, w_body=0.60, w_hiss=0.22,
         chain=[ins("saturator", drive=8, bias=5, tone=11000, mix=100, output=-2),
                ins("eq_7band", hpf_on=1, hpf_freq=80, low_mid_freq=240, low_mid_gain=-2,
                    mid_freq=1200, mid_gain=3, high_mid_freq=4200, high_mid_gain=2, output_gain=-1),
                ins("compressor", threshold=-18, ratio=3, attack=2, release=80, makeup=2),
                ins("plate_reverb", mix=5, decay=0.25, size=20, damping=8000, low_cut=200),
                ins("maximizer", threshold=-3, ceiling=-1.0, release=60)]),
    dict(seed=1102, dur=0.165, tail_hz=560, tail_decay=14.0, tail_env=0.070,
         body_hz=130, body_drop=0.40, body_decay=0.035,
         noise_color="pink", transient_decay=0.005, click=0.35,
         hiss_color="band", hiss_decay=0.040,
         w_tr=0.90, w_click=0.30, w_tail=0.80, w_body=0.70, w_hiss=0.22,
         chain=[ins("saturator", drive=12, bias=10, tone=9000, mix=100, output=-3),
                ins("eq_7band", hpf_on=1, hpf_freq=100, low_mid_freq=300, low_mid_gain=1,
                    mid_freq=800, mid_gain=4, high_mid_freq=3500, high_mid_gain=-2),
                ins("compressor", threshold=-20, ratio=4, attack=1, release=60, makeup=3),
                ins("room_reverb", mix=7, decay=0.35, size=28, damping=6000, low_cut=180),
                ins("maximizer", threshold=-3, ceiling=-1.0, release=60)]),
    dict(seed=1103, dur=0.190, tail_hz=780, tail_decay=12.0, tail_env=0.085,
         body_hz=95, body_drop=0.80, body_decay=0.028,
         noise_color="band", transient_decay=0.006, click=0.60,
         hiss_color="bright", hiss_decay=0.035,
         w_tr=1.00, w_click=0.50, w_tail=0.70, w_body=0.95, w_hiss=0.20,
         chain=[ins("saturator", drive=16, bias=18, tone=7000, mix=100, output=-4),
                ins("eq_7band", hpf_on=1, hpf_freq=140, mid_freq=2000, mid_gain=3,
                    high_shelf_freq=7000, high_shelf_gain=3),
                ins("compressor", threshold=-22, ratio=5, attack=1, release=50, makeup=3),
                ins("plate_reverb", mix=9, decay=0.40, size=35, damping=9000, low_cut=250),
                ins("maximizer", threshold=-3, ceiling=-1.0, release=60)]),
    dict(seed=1104, dur=0.220, tail_hz=950, tail_decay=10.0, tail_env=0.100,
         body_hz=160, body_drop=0.30, body_decay=0.045,
         noise_color="crackle", transient_decay=0.008, click=0.50,
         hiss_color="air", hiss_decay=0.050,
         w_tr=0.85, w_click=0.40, w_tail=0.85, w_body=0.60, w_hiss=0.28,
         chain=[ins("saturator", drive=5, bias=0, tone=16000, mix=100, output=-1),
                ins("ring_mod", effect=0, frequency=1300, mix=12),
                ins("eq_7band", hpf_on=1, hpf_freq=150, low_mid_gain=-1,
                    mid_freq=1500, mid_gain=2, high_mid_freq=5000, high_mid_gain=2),
                ins("compressor", threshold=-18, ratio=3, attack=2, release=70, makeup=2),
                ins("room_reverb", mix=6, decay=0.30, size=25, damping=7000, low_cut=220),
                ins("maximizer", threshold=-3, ceiling=-1.0, release=60)]),
    dict(seed=1105, dur=0.250, tail_hz=1200, tail_decay=9.0, tail_env=0.120,
         body_hz=190, body_drop=0.50, body_decay=0.050,
         noise_color="white", transient_decay=0.004, click=0.70,
         hiss_color="band", hiss_decay=0.060,
         w_tr=0.95, w_click=0.60, w_tail=0.80, w_body=0.65, w_hiss=0.22,
         chain=[ins("saturator", drive=10, bias=5, tone=13000, mix=100, output=-2),
                ins("eq_7band", hpf_on=1, hpf_freq=180, mid_freq=2500, mid_gain=3,
                    high_shelf_freq=9000, high_shelf_gain=2),
                ins("compressor", threshold=-20, ratio=4, attack=2, release=80, makeup=2),
                ins("plate_reverb", mix=8, decay=0.35, size=30, damping=10000, low_cut=250),
                ins("maximizer", threshold=-3, ceiling=-1.0, release=60)]),
    dict(seed=1106, dur=0.270, tail_hz=1500, tail_decay=8.0, tail_env=0.140,
         body_hz=240, body_drop=0.25, body_decay=0.055,
         noise_color="dark", transient_decay=0.006, click=0.80,
         hiss_color="pink", hiss_decay=0.070,
         w_tr=0.80, w_click=0.75, w_tail=0.90, w_body=0.50, w_hiss=0.20,
         chain=[ins("saturator", drive=14, bias=20, tone=8000, mix=100, output=-4),
                ins("lofi", bits=10, sample_rate=22050, noise=4, mix=12),
                ins("eq_7band", hpf_on=1, hpf_freq=120, low_mid_gain=2,
                    high_mid_gain=-1, lpf_on=1, lpf_freq=12000),
                ins("compressor", threshold=-20, ratio=4, attack=2, release=70, makeup=3),
                ins("room_reverb", mix=9, decay=0.40, size=32, damping=6000, low_cut=200),
                ins("maximizer", threshold=-3, ceiling=-1.0, release=60)]),
    dict(seed=1107, dur=0.290, tail_hz=1900, tail_decay=7.5, tail_env=0.160,
         body_hz=210, body_drop=0.70, body_decay=0.060,
         noise_color="crackle", transient_decay=0.010, click=0.40,
         hiss_color="air", hiss_decay=0.080,
         w_tr=0.90, w_click=0.35, w_tail=1.00, w_body=0.55, w_hiss=0.30,
         chain=[ins("saturator", drive=7, bias=0, tone=15000, mix=100, output=-2),
                ins("eq_7band", hpf_on=1, hpf_freq=160, mid_freq=1800, mid_gain=2,
                    high_mid_freq=5500, high_mid_gain=3),
                ins("phaser", rate=0.4, depth=20, mix=8),
                ins("compressor", threshold=-18, ratio=3, attack=2, release=90, makeup=2),
                ins("plate_reverb", mix=8, decay=0.40, size=35, damping=9000, low_cut=250),
                ins("maximizer", threshold=-3, ceiling=-1.0, release=60)]),
    dict(seed=1108, dur=0.300, tail_hz=2300, tail_decay=7.0, tail_env=0.180,
         body_hz=260, body_drop=0.40, body_decay=0.070,
         noise_color="air", transient_decay=0.0055, click=0.75,
         hiss_color="bright", hiss_decay=0.090,
         w_tr=0.80, w_click=0.60, w_tail=1.00, w_body=0.50, w_hiss=0.25,
         chain=[ins("saturator", drive=18, bias=30, tone=6000, mix=100, output=-5),
                ins("eq_7band", hpf_on=1, hpf_freq=200, high_mid_freq=6000, high_mid_gain=3,
                    high_shelf_gain=4),
                ins("compressor", threshold=-24, ratio=6, attack=1, release=50, makeup=4),
                ins("room_reverb", mix=10, decay=0.50, size=40, damping=5000, low_cut=300),
                ins("maximizer", threshold=-3, ceiling=-1.0, release=60)]),
]

LASER_FIRE = [
    dict(seed=2101, dur=0.38, f0=2600, f1=180, curve="exp", p=1.0, wave="saw",
         decay=0.22, sub=0.15, crackle_rate=35, crackle_amount=0.06, crackle_decay=0.10,
         chain=[ins("saturator", drive=6, bias=0, tone=18000, mix=100, output=-1),
                ins("eq_7band", hpf_on=1, hpf_freq=120, high_mid_freq=6000, high_mid_gain=2,
                    high_shelf_freq=10000, high_shelf_gain=2),
                ins("compressor", threshold=-18, ratio=3, attack=2, release=80, makeup=1),
                ins("room_reverb", mix=4, decay=0.25, size=20, damping=9000, low_cut=200),
                ins("maximizer", threshold=-3, ceiling=-1.0, release=60)]),
    dict(seed=2102, dur=0.42, f0=2200, f1=300, curve="linear", p=1.0, wave="square",
         decay=0.24, sub=0.0, crackle_rate=90, crackle_amount=0.24, crackle_decay=0.14,
         chain=[ins("saturator", drive=10, bias=10, tone=14000, mix=100, output=-2),
                ins("eq_7band", hpf_on=1, hpf_freq=100, mid_freq=3000, mid_gain=2,
                    high_shelf_freq=9000, high_shelf_gain=2),
                ins("compressor", threshold=-20, ratio=4, attack=2, release=70, makeup=2),
                ins("mod_delay", time=55, feedback=12, mix=7, mod_rate=0.3, mod_depth=10, lpf=12000),
                ins("maximizer", threshold=-3, ceiling=-1.0, release=60)]),
    dict(seed=2103, dur=0.46, f0=3200, f1=240, curve="exp", p=2.2, wave="fm",
         fm_index=2.2, fm_ratio=1.7, decay=0.26, sub=0.10,
         crackle_rate=220, crackle_amount=0.50, crackle_decay=0.18,
         chain=[ins("saturator", drive=8, bias=0, tone=16000, mix=100, output=-1),
                ins("eq_7band", hpf_on=1, hpf_freq=130, mid_freq=4000, mid_gain=2,
                    high_shelf_freq=11000, high_shelf_gain=2),
                ins("phaser", rate=0.6, depth=25, mix=10),
                ins("compressor", threshold=-20, ratio=4, attack=1, release=60, makeup=2),
                ins("plate_reverb", mix=5, decay=0.30, size=25, damping=10000, low_cut=250),
                ins("maximizer", threshold=-3, ceiling=-1.0, release=60)]),
    dict(seed=2104, dur=0.50, f0=1800, f1=420, curve="exp", p=0.65, wave="dual",
         decay=0.28, sub=0.25, crackle_rate=20, crackle_amount=0.10, crackle_decay=0.12,
         chain=[ins("saturator", drive=5, bias=0, tone=19000, mix=100, output=-1),
                ins("eq_7band", hpf_on=1, hpf_freq=90, low_mid_gain=1, mid_freq=2200, mid_gain=2,
                    high_shelf_freq=12000, high_shelf_gain=1),
                ins("chorus", rate=0.5, depth=20, mix=8),
                ins("compressor", threshold=-18, ratio=3, attack=3, release=100, makeup=1),
                ins("room_reverb", mix=5, decay=0.30, size=25, damping=8000, low_cut=200),
                ins("maximizer", threshold=-3, ceiling=-1.0, release=60)]),
    dict(seed=2105, dur=0.54, f0=2800, f1=160, curve="s", p=1.0, wave="ring",
         ring_hz=720, decay=0.30, sub=0.0,
         crackle_rate=380, crackle_amount=0.70, crackle_decay=0.22,
         chain=[ins("saturator", drive=12, bias=25, tone=12000, mix=100, output=-3),
                ins("ring_mod", effect=0, frequency=700, mix=14),
                ins("eq_7band", hpf_on=1, hpf_freq=140, mid_freq=3500, mid_gain=2,
                    high_shelf_freq=10000, high_shelf_gain=2),
                ins("compressor", threshold=-22, ratio=5, attack=1, release=50, makeup=3),
                ins("plate_reverb", mix=6, decay=0.35, size=30, damping=9000, low_cut=250),
                ins("maximizer", threshold=-3, ceiling=-1.0, release=60)]),
    dict(seed=2106, dur=0.58, f0=2400, f1=350, curve="exp", p=1.4, wave="square",
         decay=0.32, sub=0.18, crackle_rate=140, crackle_amount=0.34, crackle_decay=0.20,
         chain=[ins("saturator", drive=9, bias=5, tone=15000, mix=100, output=-2),
                ins("pitch_shifter", semitones=-12, mix=8),
                ins("eq_7band", hpf_on=1, hpf_freq=110, mid_freq=2600, mid_gain=3,
                    high_shelf_freq=9500, high_shelf_gain=2),
                ins("compressor", threshold=-20, ratio=4, attack=2, release=80, makeup=2),
                ins("room_reverb", mix=6, decay=0.35, size=28, damping=7000, low_cut=200),
                ins("maximizer", threshold=-3, ceiling=-1.0, release=60)]),
]

LASER_IMPACT = [
    dict(seed=3101, dur=0.22, center=3200, am_hz=42, sizzle_decay=0.060,
         ring_hz=720, ring_decay=20, ring_env=0.050,
         thud_hz=120, thud_decay=0.035, crackle_rate=90, crackle_amount=0.25,
         crackle_decay=0.10, noise_color="bright",
         w_sizzle=1.00, w_ring=0.35, w_thud=0.55, w_crackle=0.25,
         chain=[ins("saturator", drive=6, bias=0, tone=16000, mix=100, output=-1),
                ins("eq_7band", hpf_on=1, hpf_freq=150, mid_freq=3000, mid_gain=2,
                    high_shelf_freq=8000, high_shelf_gain=2),
                ins("compressor", threshold=-18, ratio=3, attack=1, release=60, makeup=1),
                ins("plate_reverb", mix=6, decay=0.20, size=20, damping=10000, low_cut=250),
                ins("maximizer", threshold=-3, ceiling=-1.0, release=60)]),
    dict(seed=3102, dur=0.28, center=5000, am_hz=68, sizzle_decay=0.090,
         ring_hz=1100, ring_decay=15, ring_env=0.070,
         thud_hz=150, thud_decay=0.045, crackle_rate=160, crackle_amount=0.40,
         crackle_decay=0.14, noise_color="air",
         w_sizzle=1.00, w_ring=0.30, w_thud=0.45, w_crackle=0.40,
         chain=[ins("saturator", drive=10, bias=10, tone=13000, mix=100, output=-2),
                ins("eq_7band", hpf_on=1, hpf_freq=180, mid_freq=4500, mid_gain=2,
                    high_shelf_freq=10000, high_shelf_gain=2),
                ins("ring_mod", effect=0, frequency=900, mix=10),
                ins("compressor", threshold=-20, ratio=4, attack=1, release=50, makeup=2),
                ins("room_reverb", mix=5, decay=0.30, size=25, damping=8000, low_cut=200),
                ins("maximizer", threshold=-3, ceiling=-1.0, release=60)]),
    dict(seed=3103, dur=0.34, center=2200, am_hz=28, sizzle_decay=0.120,
         ring_hz=480, ring_decay=12, ring_env=0.090,
         thud_hz=90, thud_decay=0.060, crackle_rate=60, crackle_amount=0.15,
         crackle_decay=0.18, noise_color="band",
         w_sizzle=0.90, w_ring=0.50, w_thud=0.80, w_crackle=0.15,
         chain=[ins("saturator", drive=14, bias=20, tone=9000, mix=100, output=-3),
                ins("eq_7band", hpf_on=1, hpf_freq=120, low_mid_freq=400, low_mid_gain=2,
                    mid_freq=1500, mid_gain=2),
                ins("phaser", rate=0.35, depth=15, mix=8),
                ins("compressor", threshold=-22, ratio=5, attack=2, release=70, makeup=3),
                ins("plate_reverb", mix=7, decay=0.35, size=30, damping=9000, low_cut=200),
                ins("maximizer", threshold=-3, ceiling=-1.0, release=60)]),
    dict(seed=3104, dur=0.39, center=6500, am_hz=95, sizzle_decay=0.140,
         ring_hz=1500, ring_decay=10, ring_env=0.100,
         thud_hz=180, thud_decay=0.050, crackle_rate=260, crackle_amount=0.55,
         crackle_decay=0.20, noise_color="white",
         w_sizzle=1.00, w_ring=0.25, w_thud=0.35, w_crackle=0.50,
         chain=[ins("saturator", drive=8, bias=5, tone=17000, mix=100, output=-1),
                ins("lofi", bits=9, sample_rate=16000, noise=6, mix=8),
                ins("eq_7band", hpf_on=1, hpf_freq=180, mid_freq=6000, mid_gain=3,
                    high_shelf_gain=3),
                ins("compressor", threshold=-20, ratio=4, attack=1, release=45, makeup=2),
                ins("room_reverb", mix=8, decay=0.25, size=25, damping=7000, low_cut=300),
                ins("maximizer", threshold=-3, ceiling=-1.0, release=60)]),
]

EXPLOSIONS = [
    dict(seed=4101, dur=0.72, body_decay=0.16, boom_start=110, boom_end=45, boom_pitch_decay=0.06, boom_decay=0.20,
         sub_hz=36, sub_decay=0.25, noise_hi=9000, noise_lo=250, noise_decay=0.20, rumble_decay=0.25,
         roar_lo=300, roar_hi=1800, roar_decay=0.40, debris_rate=180, debris_lo=3500, debris_hi=11000,
         debris_decay=0.40, w_boom=0.70, w_sub=0.20, w_rumble=0.75, w_roar=0.60, w_debris=1.20,
         click=0.80,
         chain=[ins("saturator", drive=6, bias=0, tone=18000, mix=100, output=-2),
                ins("eq_7band", hpf_on=1, hpf_freq=90, low_shelf_gain=-2,
                    high_shelf_freq=5000, high_shelf_gain=1, lpf_on=1, lpf_freq=10000),
                ins("compressor", threshold=-12, ratio=2, attack=10, release=120, makeup=2),
                ins("room_reverb", mix=6, decay=0.8, size=40, damping=12000, low_cut=200),
                ins("maximizer", threshold=-3, ceiling=-1.0, release=60)]),
    dict(seed=4102, dur=0.85, boom_start=70, boom_end=28, boom_pitch_decay=0.10, boom_decay=0.45,
         sub_hz=20, sub_decay=1.00, noise_hi=1200, noise_lo=30, noise_decay=0.50, rumble_decay=0.70,
         roar_lo=80, roar_hi=450, roar_decay=0.60, debris_rate=20, debris_lo=300, debris_hi=1800,
         debris_decay=0.50, w_boom=1.30, w_sub=1.00, w_rumble=0.65, w_roar=0.15, w_debris=0.03,
         click=0.30,
         layer=dict(note=22, velocity=105, level_db=-6,
                    params=dict(osc1_wave=2, osc2_wave=2, osc_mix=0, cutoff=180, resonance=0,
                                filter_env_amount=0, amp_attack=2, amp_decay=1400, amp_sustain=0,
                                amp_release=300, level=-6),
                    chain=[ins("eq_7band", hpf_on=1, hpf_freq=20, low_shelf_gain=4,
                               lpf_on=1, lpf_freq=400)]),
         chain=[ins("subharmonic", range=0, mix=65, direct_gain=0, lower_gain=3, upper_gain=-12,
                    character=0),
                ins("saturator", drive=10, bias=5, tone=4000, mix=100, output=-3),
                ins("eq_7band", low_shelf_freq=60, low_shelf_gain=6, high_shelf_gain=-6,
                    hpf_on=1, hpf_freq=20, lpf_on=1, lpf_freq=1800),
                ins("compressor", threshold=-22, ratio=4, attack=15, release=180, makeup=4),
                ins("room_reverb", mix=10, decay=1.8, size=55, damping=6000, low_cut=20),
                ins("maximizer", threshold=-4, ceiling=-1.0, release=80)]),
    dict(seed=4103, dur=1.00, body_decay=0.40, boom_start=65, boom_end=35, boom_pitch_decay=0.12, boom_decay=0.35,
         sub_hz=28, sub_decay=0.50, noise_hi=3500, noise_lo=150, noise_decay=0.40, rumble_decay=0.50,
         roar_lo=800, roar_hi=4000, roar_decay=0.70, debris_rate=160, debris_lo=3000, debris_hi=7000,
         debris_decay=0.50, w_boom=0.60, w_sub=0.20, w_rumble=0.90, w_roar=1.30, w_debris=0.40,
         click=0.60,
         layer=dict(note=26, velocity=112, level_db=-16,
                    params=dict(osc1_wave=2, osc2_wave=2, osc_mix=0, cutoff=220, resonance=0,
                                filter_env_amount=0, amp_attack=2, amp_decay=1100, amp_sustain=0,
                                amp_release=250, level=-16),
                    chain=[ins("eq_7band", hpf_on=1, hpf_freq=20, low_shelf_gain=1,
                               lpf_on=1, lpf_freq=700)]),
         chain=[ins("subharmonic", range=1, mix=10, direct_gain=0, lower_gain=0, upper_gain=-6,
                    character=2),
                ins("saturator", drive=8, bias=0, tone=12000, mix=100, output=-2),
                ins("eq_7band", hpf_on=1, hpf_freq=150, low_shelf_gain=-3,
                    mid_freq=450, mid_gain=5, high_shelf_gain=-8, lpf_on=1, lpf_freq=2200),
                ins("compressor", threshold=-12, ratio=2, attack=12, release=160, makeup=3),
                ins("plate_reverb", mix=10, decay=1.5, size=55, damping=9000, low_cut=150),
                ins("maximizer", threshold=-4, ceiling=-1.0, release=80)]),
    dict(seed=4104, dur=1.15, body_decay=0.55, boom_start=55, boom_end=25, boom_pitch_decay=0.16, boom_decay=0.40,
         sub_hz=24, sub_decay=0.45, noise_hi=7000, noise_lo=300, noise_decay=0.50, rumble_decay=0.45,
         roar_lo=400, roar_hi=3000, roar_decay=0.80, debris_rate=900, debris_lo=1800, debris_hi=6000,
         debris_decay=0.70, w_boom=0.60, w_sub=0.20, w_rumble=0.50, w_roar=0.40, w_debris=1.70,
         click=0.90,
         layer=dict(note=28, velocity=108, level_db=-20,
                    params=dict(osc1_wave=2, osc2_wave=2, osc_mix=0, cutoff=240, resonance=0,
                                filter_env_amount=0, amp_attack=2, amp_decay=900, amp_sustain=0,
                                amp_release=250, level=-20),
                    chain=[ins("eq_7band", hpf_on=1, hpf_freq=20, lpf_on=1, lpf_freq=800)]),
         chain=[ins("subharmonic", range=3, mix=8, direct_gain=0, lower_gain=0, upper_gain=-6,
                    character=3),
                ins("saturator", drive=6, bias=0, tone=19000, mix=100, output=-1),
                ins("eq_7band", hpf_on=1, hpf_freq=180, low_shelf_gain=-3,
                    high_shelf_freq=8000, high_shelf_gain=-3, lpf_on=1, lpf_freq=6500),
                ins("compressor", threshold=-12, ratio=2, attack=8, release=120, makeup=2),
                ins("room_reverb", mix=8, decay=1.0, size=45, damping=14000, low_cut=400),
                ins("maximizer", threshold=-4, ceiling=-1.0, release=80)]),
]

SETS = [
    ("hit", HITS, synth_hit),
    ("laser_fire", LASER_FIRE, synth_laser_fire),
    ("laser_impact", LASER_IMPACT, synth_laser_impact),
    ("explosion", EXPLOSIONS, synth_explosion),
]


def write_wav(path, x, peak):
    x = np.asarray(x, dtype=np.float64)
    m = np.max(np.abs(x))
    if m > 1e-12:
        x = x / m * peak
    pcm = np.round(np.clip(x, -1.0, 1.0) * 32767.0).astype("<i2")
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(pcm.tobytes())


def read_wav(path):
    with wave.open(path, "rb") as w:
        channels = w.getnchannels()
        width = w.getsampwidth()
        frames = w.getnframes()
        raw = w.readframes(frames)
    if width == 2:
        x = np.frombuffer(raw, dtype="<i2").astype(np.float64) / 32768.0
    elif width == 3:
        b = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3).astype(np.int32)
        v = b[:, 0] | (b[:, 1] << 8) | (b[:, 2] << 16)
        v = np.where(v & 0x800000, v - 0x1000000, v)
        x = v.astype(np.float64) / 8388608.0
    else:
        raise ValueError(f"unsupported sample width {width}")
    x = x.reshape(-1, channels)
    return x.mean(axis=1) if channels > 1 else x


def render(name, dry_path, frames, chain, layer, work):
    cmds = [("session.new", {"name": name, "sample_rate": RATE})]
    if layer:
        cmds.append(("track.new", {"kind": "instrument", "name": "Layer",
                                   "instrument": "subtractive_synth", "format": "mono"}))
        cmds.append(("mix.instrument", {"track": "Layer", "plugin": "subtractive_synth",
                                        "params": layer["params"]}))
        cmds.append(("midi.new_clip", {"track": "Layer", "start": 0, "length": frames}))
        ticks = int(frames / RATE / 0.5 * 960) + 240
        cmds.append(("midi.note_add", {"clip": 2, "pitch": layer["note"], "start_ticks": 0,
                                       "length_ticks": ticks, "velocity": layer["velocity"]}))
        for slot, item in enumerate(layer.get("chain", [])):
            cmds.append(("mix.insert", {"track": "Layer", "slot": slot,
                                        "plugin": item["plugin"], "params": item["params"]}))
        cmds.append(("mix.volume", {"tracks": ["Layer"], "db": layer["level_db"]}))
        cmds.append(("track.new", {"kind": "audio", "name": "Dry", "format": "mono"}))
    else:
        cmds.append(("track.new", {"kind": "audio", "name": "Dry", "format": "mono"}))
    cmds.append(("file.import_audio", {"path": dry_path, "new_tracks": False, "track": "Dry"}))
    for slot, item in enumerate(chain):
        cmds.append(("mix.insert", {"track": "Dry", "slot": slot,
                                    "plugin": item["plugin"], "params": item["params"]}))
    bounce = os.path.join(work, name + "_bounce.wav")
    cmds.append(("file.bounce_mix", {"path": bounce, "format": "wav", "bit_depth": 24,
                                     "start": 0, "end": frames, "dither": False}))
    script_path = os.path.join(work, name + ".txt")
    with open(script_path, "w") as f:
        for cid, params in cmds:
            f.write(cid + " " + json.dumps(params, separators=(",", ":")) + "\n")
    cmd = ["flatpak", "run", f"--filesystem={ROOT}", f"--filesystem={work}",
           f"--filesystem={OUT}", "--command=soundcraft-cli", "ai.storyteller.soundcraft",
           "script", script_path]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"SoundCraft failed for {name}\n{result.stdout}\n{result.stderr}")
    if not os.path.exists(bounce):
        raise RuntimeError(f"SoundCraft produced no bounce for {name}")
    return bounce


def finalize(bounce_path, out_path, frames):
    x = read_wav(bounce_path)
    loud = np.flatnonzero(np.abs(x) > 1e-3)
    if not len(loud):
        raise RuntimeError(f"silent bounce: {bounce_path}")
    start = int(loud[0])
    if start > int(0.002 * RATE):
        raise RuntimeError(f"leading silence {start / RATE * 1000.0:.2f} ms in {bounce_path}")
    x = x[start:start + frames]
    if len(x) < frames:
        x = np.pad(x, (0, frames - len(x)))
    # Fade out over the longer of 60 ms and 15% of the sound (a raised cosine):
    # a sound cut while still loud (an explosion's boom) never ends in a click.
    k = min(len(x), max(int(0.060 * RATE), int(0.15 * len(x))))
    x[-k:] *= 0.5 + 0.5 * np.cos(np.linspace(0.0, np.pi, k))
    x[-1] = 0.0
    write_wav(out_path, x, TARGET_PEAK)


def main():
    os.makedirs(OUT, exist_ok=True)
    work = tempfile.mkdtemp(prefix="make_sounds_")
    try:
        for prefix, specs, synth in SETS:
            for index, spec in enumerate(specs, 1):
                name = f"{prefix}_{index:02d}"
                frames = frame_count(spec["dur"])
                dry_path = os.path.join(work, name + "_dry.wav")
                write_wav(dry_path, synth(spec), 0.45)
                bounce = render(name, dry_path, frames, spec["chain"], spec.get("layer"), work)
                finalize(bounce, os.path.join(OUT, name + ".wav"), frames)
                plugins = [item["plugin"] for item in spec["chain"]]
                if spec.get("layer"):
                    plugins.append("subtractive_synth")
                print(f"{name}.wav  {spec['dur']:.3f}s  {' > '.join(plugins)}")
    finally:
        shutil.rmtree(work, ignore_errors=True)
    print("sounds written to", OUT)


if __name__ == "__main__":
    main()
