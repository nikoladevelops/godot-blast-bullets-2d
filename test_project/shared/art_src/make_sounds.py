"""The test project's synthesized sounds (shared/sound): deterministic, no samples needed.

  laser_fire.wav  0.45 s: a bright zap sweeping down, with a short noise crackle
  explosion.wav   0.9 s: a low boom (noise through a closing low-pass) over a thump

    python3 test_project/shared/art_src/make_sounds.py
"""
import os
import wave

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(HERE, "..", "sound"))
RATE = 44100


def write(name, x):
    x = x / max(1e-9, np.max(np.abs(x))) * 0.8
    pcm = (np.clip(x, -1, 1) * 32767).astype("<i2")
    with wave.open(os.path.join(OUT, name), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(pcm.tobytes())


def lowpass(x, cutoff):
    """One-pole low-pass with a per-sample cutoff (Hz)."""
    a = 1.0 - np.exp(-2 * np.pi * np.asarray(cutoff) / RATE)
    a = np.broadcast_to(a, x.shape)
    y = np.empty_like(x)
    acc = 0.0
    for i in range(len(x)):
        acc += a[i] * (x[i] - acc)
        y[i] = acc
    return y


def laser_fire():
    t = np.arange(int(0.45 * RATE)) / RATE
    f = 1800 * np.exp(-t * 7) + 220
    phase = 2 * np.pi * np.cumsum(f) / RATE
    tone = np.sign(np.sin(phase)) * 0.35 + np.sin(phase * 2.01) * 0.4 + np.sin(phase * 0.5) * 0.3
    env = np.minimum(1, t / 0.004) * np.exp(-t * 6.5)
    crackle = np.random.default_rng(3).standard_normal(len(t)) * np.exp(-t * 40) * 0.5
    write("laser_fire.wav", lowpass(tone * env + crackle, 7000))


def explosion():
    t = np.arange(int(0.9 * RATE)) / RATE
    noise = np.random.default_rng(7).standard_normal(len(t))
    boom = lowpass(noise, 3500 * np.exp(-t * 5) + 120) * np.exp(-t * 4.2)
    thump = np.sin(2 * np.pi * np.cumsum(90 * np.exp(-t * 6) + 40) / RATE) * np.exp(-t * 9)
    env = np.minimum(1, t / 0.003)
    write("explosion.wav", (boom * 1.6 + thump) * env)


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    laser_fire()
    explosion()
    print("sounds written to", OUT)
