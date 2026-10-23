#!/usr/bin/env python3
"""Synthesizes the placeholder sounds in assets/source/sounds/ (ADR-0033).

Deterministic (fixed seeds), standard library only. 16-bit mono WAV at 22.05 kHz; the engine
converts to its output rate. Replace any file with a recorded / designed sound of the same name.
"""

import math
import pathlib
import random
import struct
import wave

OUT = pathlib.Path(__file__).resolve().parents[2] / "assets" / "source" / "sounds"
RATE = 22050


def write(name, samples):
    OUT.mkdir(parents=True, exist_ok=True)
    peak = max(1e-9, max(abs(s) for s in samples))
    gain = min(1.0, 0.9 / peak)
    frames = b"".join(struct.pack("<h", int(max(-1.0, min(1.0, s * gain)) * 32767)) for s in samples)
    with wave.open(str(OUT / f"{name}.wav"), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(frames)


def envelope(t, attack, decay):
    return min(1.0, t / attack) * math.exp(-t / decay) if attack > 0 else math.exp(-t / decay)


def sweep(duration, f0, f1, shape="sine", attack=0.004, decay=0.05):
    out = []
    phase = 0.0
    for i in range(int(duration * RATE)):
        t = i / RATE
        f = f0 + (f1 - f0) * (t / duration)
        phase += 2 * math.pi * f / RATE
        wave_ = math.sin(phase) if shape == "sine" else (1.0 if math.sin(phase) >= 0 else -1.0) * 0.6
        out.append(wave_ * envelope(t, attack, decay))
    return out


def noise(duration, seed, cutoff, attack=0.002, decay=0.1, level=1.0):
    rng = random.Random(seed)
    alpha = 1.0 - math.exp(-2 * math.pi * cutoff / RATE)  # one-pole low-pass
    y = 0.0
    out = []
    for i in range(int(duration * RATE)):
        t = i / RATE
        y += alpha * (rng.uniform(-1, 1) - y)
        out.append(y * level * envelope(t, attack, decay))
    return out


def mix(*tracks):
    length = max(len(t) for t in tracks)
    return [sum(t[i] for t in tracks if i < len(t)) for i in range(length)]


def chime(notes, spacing, decay):
    out = [0.0] * int((spacing * len(notes) + decay * 5) * RATE)
    for k, f in enumerate(notes):
        start = int(k * spacing * RATE)
        for i in range(len(out) - start):
            t = i / RATE
            out[start + i] += (math.sin(2 * math.pi * f * t) + 0.3 * math.sin(4 * math.pi * f * t)) * envelope(t, 0.002, decay)
    return out


def ambience(duration):
    """A quiet cave: rumbling low noise and sparse drips, crossfaded into a seamless loop."""
    n = int(duration * RATE)
    fade = int(0.5 * RATE)
    total = n + fade
    rumble = noise(total / RATE, seed=7, cutoff=180, attack=0, decay=1e9, level=0.35)
    rng = random.Random(11)
    drips = [0.0] * total
    t = 0.4
    while t < duration:
        f = rng.uniform(1100, 2100)
        start = int(t * RATE)
        for i in range(int(0.25 * RATE)):
            if start + i < total:
                tt = i / RATE
                drips[start + i] += 0.25 * math.sin(2 * math.pi * (f - 600 * tt) * tt) * envelope(tt, 0.001, 0.04)
        t += rng.uniform(0.6, 1.8)
    raw = [a + b for a, b in zip(rumble, drips)]
    # The tail fades into the head, so the end joins the start without a click.
    looped = raw[:n]
    for i in range(fade):
        w = i / fade
        looped[i] = raw[i] * w + raw[n + i] * (1 - w)
    return looped


def main():
    write("jump", sweep(0.12, 380, 900, "square", decay=0.05))
    write("land", mix(sweep(0.10, 110, 45, decay=0.03), noise(0.08, seed=1, cutoff=900, decay=0.02, level=0.5)))
    write("gem", chime([1318.5, 1661.2, 1975.5], 0.07, 0.12))
    write("hit", mix(noise(0.25, seed=2, cutoff=2500, decay=0.06), sweep(0.25, 320, 80, "square", decay=0.08)))
    write("splash", noise(0.45, seed=3, cutoff=1800, attack=0.01, decay=0.12))
    write("grab", noise(0.05, seed=4, cutoff=4000, decay=0.008))
    write("cave_ambience", ambience(6.0))
    print(f"wrote sounds to {OUT}")


if __name__ == "__main__":
    main()
