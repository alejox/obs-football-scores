#!/usr/bin/env python3
"""Generates data/sounds/goal.wav, the built-in goal sound: three stadium air-horn
blasts over a crowd roar that swells and fades. Fully synthesized here, so the plugin
ships no third-party audio. Needs numpy.

    python3 tools/make_goal_sound.py data/sounds/goal.wav
"""
import sys
import wave

import numpy as np

RATE = 48000
LENGTH = 3.6  # seconds
rng = np.random.default_rng(20261120)
t = np.arange(int(RATE * LENGTH)) / RATE


def envelope(start, end, attack=0.02, release=0.09):
    env = np.zeros_like(t)
    on = (t >= start) & (t < end)
    env[on] = 1.0
    rise = (t >= start) & (t < start + attack)
    env[rise] = (t[rise] - start) / attack
    fall = (t >= end) & (t < end + release)
    env[fall] = 1.0 - (t[fall] - end) / release
    return env


def horn():
    # A detuned sawtooth chord with rolled-off harmonics: the classic stadium air horn.
    tone = np.zeros_like(t)
    vibrato = 1.0 + 0.004 * np.sin(2 * np.pi * 5.5 * t)
    for base, gain in ((233.08, 1.0), (293.66, 0.8), (349.23, 0.6)):
        for detune in (0.997, 1.003):
            phase = 2 * np.pi * base * detune * np.cumsum(vibrato) / RATE
            for k in range(1, 14):
                tone += gain * np.sin(k * phase) / (k ** 1.15)
    tone /= np.max(np.abs(tone))
    blasts = envelope(0.05, 0.38) + envelope(0.50, 0.83) + envelope(0.98, 2.35, release=0.25)
    return tone * blasts


def crowd(seed_offset):
    # Band-limited noise (roughly 250 Hz - 3 kHz) with a slow wobble, swelling after the horn.
    noise = np.random.default_rng(20261120 + seed_offset).standard_normal(t.size)
    spectrum = np.fft.rfft(noise)
    freqs = np.fft.rfftfreq(t.size, 1 / RATE)
    shape = np.exp(-((np.log(np.maximum(freqs, 1)) - np.log(900)) ** 2) / (2 * 0.75 ** 2))
    shaped = np.fft.irfft(spectrum * shape, n=t.size)
    shaped /= np.max(np.abs(shaped))
    wobble = 1.0 + 0.15 * np.sin(2 * np.pi * 3.1 * t + seed_offset)
    swell = np.clip((t - 0.15) / 1.0, 0, 1) ** 1.5
    fade = np.clip((LENGTH - t) / 1.1, 0, 1)
    return shaped * wobble * swell * fade


def main(path):
    h = horn()
    left = 0.62 * h + 0.55 * crowd(1)
    right = 0.62 * h + 0.55 * crowd(2)
    peak = max(np.max(np.abs(left)), np.max(np.abs(right)))
    gain = 0.89 / peak  # -1 dBFS
    frames = np.stack([left * gain, right * gain], axis=1)
    pcm = np.round(frames * 32767).astype("<i2")
    with wave.open(path, "wb") as out:
        out.setnchannels(2)
        out.setsampwidth(2)
        out.setframerate(RATE)
        out.writeframes(pcm.tobytes())


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "data/sounds/goal.wav")
