#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Make the launcher's sounds: its two pieces of music and its menu sounds, synthesised here.

    render-sounds.py OUTPUT_DIR

writes OUTPUT_DIR/music-shop.wav and music-setup.wav, loops of PS5CEMU-HAR's own music in the
spirit of a Nintendo console's shop and setup screens (light jazz: a bossa nova, and a calm piece),
written for the launcher and taken from no game; and move.wav, select.wav, back.wav, denied.wav and
launch.wav, the menu's sounds. The music is IMA ADPCM in stereo at 48 kHz (a quarter of 16-bit
PCM's size), and the menu sounds 16-bit PCM in mono at 48 kHz: what port/frontend/sound.cpp reads.
Each piece ends where it starts, its last notes' ring carried over its beginning, so it loops
without a seam.

The files are in the repository (port/frontend/ui/sounds), so a build needs nothing from here; run
this again only to change them. It needs NumPy and SciPy. Each run makes the same files (the
playing's small unevenness is seeded).
"""

import os
import struct
import sys

import numpy as np
from scipy import signal

SR = 48000


def hz(note):
    return 440.0 * 2 ** ((note - 69) / 12)


def seconds(length):
    return np.arange(int(round(length * SR))) / SR


def rise(t, length):
    return np.clip(t / length, 0, 1)


def fade_after(t, end, tau):
    """1 until end, then an exponential fade (a note let go of)."""
    return np.where(t < end, 1.0, np.exp(-np.maximum(t - end, 0) / tau))


_FILTERS = {}


def bandpass(x, low=None, high=None, order=2):
    key = (low, high, order)
    if key not in _FILTERS:
        if low and high:
            _FILTERS[key] = signal.butter(order, (low, high), "bandpass", fs=SR, output="sos")
        elif low:
            _FILTERS[key] = signal.butter(order, low, "highpass", fs=SR, output="sos")
        else:
            _FILTERS[key] = signal.butter(order, high, "lowpass", fs=SR, output="sos")
    return signal.sosfilt(_FILTERS[key], x)


class Band:
    """The instruments, and the drums, each note a mono sound; rng: the band's own randomness."""

    def __init__(self, seed):
        self.rng = np.random.default_rng(seed)

    def noise(self, n):
        return self.rng.standard_normal(n)

    # a Rhodes-like electric piano: two-operator FM whose brightness falls as the note rings, and the
    # tine's short ping
    def epiano(self, f, length, velocity):
        t = seconds(length + 0.6)
        index = velocity * 1.6 * np.exp(-t / 0.22) + 0.12
        tone = np.sin(2 * np.pi * f * t + index * np.sin(2 * np.pi * f * t))
        tone += 0.22 * velocity * np.sin(2 * np.pi * f * 7.0 * t) * np.exp(-t / 0.012)
        tau = float(np.clip(1.3 * (262 / f) ** 0.5, 0.5, 2.5))
        return tone * rise(t, 0.003) * np.exp(-t / tau) * fade_after(t, length, 0.09) * velocity

    # a vibraphone: the bar's partials and the mallet's tap (the tremolo is the mix's)
    def vibes(self, f, length, velocity):
        t = seconds(length + 1.6)
        tone = np.zeros_like(t)
        for ratio, level, tau in ((1.0, 1.0, 1.5 * (523 / f) ** 0.3), (3.99, 0.3, 0.2), (9.85, 0.06, 0.05)):
            if f * ratio < SR * 0.45:
                tone += level * np.exp(-t / tau) * np.sin(2 * np.pi * f * ratio * t)
        tone += 0.15 * bandpass(self.noise(len(t)), high=3000) * np.exp(-t / 0.003)
        return tone * rise(t, 0.002) * fade_after(t, length + 0.25, 0.25) * velocity

    # a soft flute, breath and all, its vibrato coming in as the note holds
    def flute(self, f, length, velocity):
        t = seconds(length + 0.35)
        depth = 0.0045 * np.clip((t - 0.18) / 0.35, 0, 1)
        phase = 2 * np.pi * np.cumsum(f * (1 + depth * np.sin(2 * np.pi * 5.1 * t))) / SR
        tone = np.sin(phase) + 0.1 * np.sin(2 * phase) + 0.03 * np.sin(3 * phase)
        tone += 0.05 * bandpass(self.noise(len(t)), 1800, 7000) * (0.4 + np.exp(-t / 0.05))
        return tone * (1 - np.exp(-t / 0.035)) * fade_after(t, length, 0.08) * velocity

    # a kalimba: its tine and the tine's high overtone
    def kalimba(self, f, velocity):
        t = seconds(1.3)
        tone = np.sin(2 * np.pi * f * t) * np.exp(-t / 0.55)
        if f * 5.9 < SR * 0.45:
            tone += 0.2 * np.sin(2 * np.pi * f * 5.9 * t) * np.exp(-t / 0.035)
        return tone * rise(t, 0.002) * velocity

    # a warm pad under the chords: two detuned voices of a soft sawtooth
    def pad(self, f, length, velocity):
        t = seconds(length + 1.2)
        tone = np.zeros_like(t)
        for detune in (-0.0035, 0.0035):
            for n in range(1, 7):
                if f * n > 6000:
                    break
                tone += np.sin(2 * np.pi * f * n * (1 + detune) * t + self.rng.uniform(0, 2 * np.pi)) / n ** 1.4
        return tone * (1 - np.exp(-t / 0.5)) * fade_after(t, length, 0.6) * velocity * 0.5

    # a round, plucked bass
    def bass(self, f, length, velocity):
        t = seconds(length + 0.12)
        phase = 2 * np.pi * f * t
        tone = np.sin(phase) + 0.3 * np.sin(2 * phase) * np.exp(-t / 0.25) + 0.1 * np.sin(3 * phase) * np.exp(-t / 0.12)
        return tone * rise(t, 0.005) * (0.35 + 0.65 * np.exp(-t / 0.35)) * fade_after(t, length * 0.92, 0.05) * velocity

    def shaker(self, velocity):
        t = seconds(0.09)
        return bandpass(self.noise(len(t)), low=5500) * rise(t, 0.012) ** 2 * np.exp(-t / 0.025) * velocity

    def rim(self, velocity):
        t = seconds(0.06)
        tone = 0.6 * bandpass(self.noise(len(t)), 1200, 5000) * np.exp(-t / 0.006)
        tone += 0.6 * np.sin(2 * np.pi * 1750 * t) * np.exp(-t / 0.01) + 0.3 * np.sin(2 * np.pi * 800 * t) * np.exp(-t / 0.015)
        return tone * velocity

    def kick(self, velocity):
        t = seconds(0.4)
        phase = 2 * np.pi * np.cumsum(46 + 70 * np.exp(-t / 0.03)) / SR
        return np.sin(phase) * rise(t, 0.002) * np.exp(-t / 0.16) * velocity

    def hat(self, velocity):
        t = seconds(0.07)
        return bandpass(self.noise(len(t)), low=7000) * np.exp(-t / 0.012) * velocity

    def brush(self, velocity):
        t = seconds(0.3)
        return bandpass(self.noise(len(t)), 1500, 7000) * rise(t, 0.04) * np.exp(-t / 0.09) * velocity


class Piece:
    """A piece being played: a mono bus for each part, the notes added where they fall."""

    def __init__(self, bpm, bars, seed, tail=3.0):
        self.beat = 60.0 / bpm
        self.length = int(round(bars * 4 * self.beat * SR))  # the loop
        self.total = self.length + int(tail * SR)  # and the last notes' ring
        self.buses = {}
        self.band = Band(seed)
        self.rng = np.random.default_rng(seed + 1)

    def add(self, bus, beat, sound, steady=False):
        """sound at a beat; a little early or late (steady: not), as played"""
        jitter = 0.0 if steady else self.rng.normal(0, 0.004)
        start = max(0, int(round((beat * self.beat + jitter) * SR)))
        end = min(self.total, start + len(sound))
        if end > start:
            self.buses.setdefault(bus, np.zeros(self.total))[start:end] += sound[:end - start]

    def velocity(self, value):
        return value * self.rng.uniform(0.9, 1.05)

    def mix(self, parts, reverb_time, wet):
        """the buses panned (with an optional auto-pan: depth, rate) and sent to a reverb, in stereo;
        then the ring past the loop's end laid over its start"""
        t = np.arange(self.total) / SR
        dry = np.zeros((2, self.total))
        send = np.zeros((2, self.total))
        for name, (gain, pan, reverb, wobble) in parts.items():
            if name not in self.buses:
                continue
            x = self.buses[name] * gain
            position = pan + (wobble[0] * np.sin(2 * np.pi * wobble[1] * t) if wobble else 0)
            angle = (np.clip(position, -1, 1) + 1) * np.pi / 4
            stereo = np.stack((x * np.cos(angle), x * np.sin(angle)))
            dry += stereo
            send += stereo * reverb
        out = dry + wet * reverberate(send, reverb_time, self.band)
        tail = self.total - self.length
        out[:, :tail] += out[:, self.length:]
        return out[:, :self.length]


def reverberate(x, reverb_time, band):
    """a hall: decaying noise, darker as it decays, for each side, after a short pre-delay"""
    t = seconds(reverb_time * 1.3)
    out = np.zeros_like(x)
    for channel in range(2):
        response = band.noise(len(t)) * np.exp(-6.91 * t / reverb_time)
        response = bandpass(response, high=5500)
        response[:int(0.015 * SR)] = 0
        response /= np.sqrt(np.sum(response ** 2))
        out[channel] = signal.oaconvolve(x[channel], response)[:x.shape[1]]
    return out


def master(stereo, loudness_db):
    """to a steady loudness (RMS, dBFS), its highest peaks rounded off below full scale. The filter
    starts a second before the start, on the loop's end, so it loops without a seam."""
    stereo = bandpass(np.concatenate((stereo[:, -SR:], stereo), axis=1), high=14000)[:, SR:]
    rms = np.sqrt(np.mean(stereo ** 2))
    stereo = stereo * (10 ** (loudness_db / 20) / rms)
    return np.tanh(stereo * 1.1) / 1.1


# -- the music ------------------------------------------------------------------------------------

def bossa_part(piece, chords, bar, lead=None, lead_bus="vibes"):
    """a bar of the bossa nova: the piano's comping, the bass, the drums, and a melody's notes"""
    band = piece.band
    halves = chords[bar] if isinstance(chords[bar], list) else [chords[bar], chords[bar]]
    following = chords[(bar + 1) % len(chords)]
    following = following[0] if isinstance(following, list) else following
    start = bar * 4

    def chord_at(beat):
        return halves[0] if beat < 2 else halves[1]

    # the piano: the clave's rhythm across two bars
    for beat, length in ((0, 1.4), (1.5, 0.4), (3.0, 0.9)) if bar % 2 == 0 else ((1.0, 0.4), (2.0, 1.4)):
        velocity = piece.velocity(0.55)
        for i, note in enumerate(chord_at(beat)[1]):
            piece.add("piano", start + beat + i * 0.012, band.epiano(hz(note), length * piece.beat, velocity * (0.85 + 0.05 * i)))
    # the bass: root, fifth, the next root ahead of its bar
    first, second = halves[0][0], halves[1][0]
    fifth = (lambda root: root + 7 if root + 7 <= 52 else root - 5)
    for beat, length, note in ((0, 1.5, first), (1.5, 0.5, fifth(first)),
                               (2, 1.5, second if halves[0] is not halves[1] else fifth(first)),
                               (3.5, 0.5, following[0])):
        piece.add("bass", start + beat, band.bass(hz(note), length * piece.beat, piece.velocity(0.8)))
    # the drums: a shaker's sixteenths, the rim on the clave, a soft kick
    for sixteenth in range(16):
        piece.add("shaker", start + sixteenth / 4, band.shaker(piece.velocity((0.9, 0.35, 0.6, 0.35)[sixteenth % 4])))
    for beat in (0, 1.5, 3.0) if bar % 2 == 0 else (1.0, 2.5):
        piece.add("rim", start + beat, band.rim(piece.velocity(0.7)))
    for beat, velocity in ((0, 0.8), (2, 0.6), (3.5, 0.35)):
        piece.add("kick", start + beat, band.kick(piece.velocity(velocity)), steady=True)
    for beat, length, note in lead or ():
        instrument = band.vibes if lead_bus == "vibes" else band.flute
        piece.add(lead_bus, start + beat, instrument(hz(note), length * piece.beat * 0.95, piece.velocity(0.75)))


def shop():
    """'Shop': a bossa nova in F, 116 beats a minute, 32 bars. The melody is the vibraphone's, then
    the flute's."""
    FMAJ9, GM9, C13 = (41, (57, 60, 64, 67)), (43, (58, 62, 65, 69)), (48, (58, 62, 64, 69))
    AM7, D9, C7SUS, C7 = (45, (60, 64, 67)), (38, (54, 60, 64, 69)), (48, (58, 60, 65, 67)), (48, (58, 64, 67))
    F9, BBMAJ9, BBM6 = (41, (57, 63, 67, 72)), (46, (62, 65, 69, 72)), (46, (61, 65, 67, 70))
    DM9, ABDIM7 = (38, (60, 64, 65, 69)), (44, (59, 62, 65, 68))
    chords = [FMAJ9, FMAJ9, GM9, C13, AM7, D9, GM9, [C7SUS, C7],
              FMAJ9, F9, BBMAJ9, BBM6, AM7, D9, [GM9, C13], [FMAJ9, C7],
              BBMAJ9, BBMAJ9, AM7, DM9, GM9, C13, FMAJ9, D9,
              GM9, BBM6, AM7, ABDIM7, GM9, C13, FMAJ9, [C7SUS, C7]]
    # (beat, length in beats, note) in each bar
    vibes = [
        [(1.0, 0.5, 81), (1.5, 0.5, 79), (2.0, 1.0, 76), (3.0, 0.5, 77), (3.5, 1.0, 81)],
        [(0.5, 0.5, 79), (1.0, 1.5, 76), (3.0, 0.5, 72), (3.5, 0.5, 74)],
        [(0.0, 1.0, 77), (1.0, 0.5, 81), (1.5, 0.5, 82), (2.0, 1.0, 86), (3.0, 0.5, 84), (3.5, 0.5, 81)],
        [(0.0, 1.5, 81), (1.5, 0.5, 79), (2.0, 2.0, 76)],
        [(1.0, 0.5, 76), (1.5, 0.5, 79), (2.0, 0.5, 84), (2.5, 1.0, 81), (3.5, 0.5, 79)],
        [(0.0, 1.0, 78), (1.0, 0.5, 81), (1.5, 1.0, 84), (2.5, 0.5, 83), (3.0, 1.0, 81)],
        [(0.0, 0.5, 82), (0.5, 0.5, 81), (1.0, 1.0, 77), (2.0, 0.5, 74), (2.5, 1.0, 77), (3.5, 0.5, 81)],
        [(0.0, 1.5, 79), (2.0, 0.5, 76), (2.5, 0.5, 77), (3.0, 1.0, 79)],
        [(1.0, 0.5, 81), (1.5, 0.5, 79), (2.0, 1.0, 76), (3.0, 0.5, 77), (3.5, 1.0, 84)],
        [(0.5, 0.5, 81), (1.0, 1.0, 79), (2.0, 0.5, 81), (2.5, 0.5, 79), (3.0, 1.0, 75)],
        [(0.0, 1.5, 74), (1.5, 0.5, 77), (2.0, 0.5, 81), (2.5, 0.5, 84), (3.0, 1.0, 81)],
        [(0.0, 1.5, 85), (1.5, 0.5, 82), (2.0, 1.0, 79), (3.0, 1.0, 77)],
        [(0.0, 1.0, 76), (1.0, 0.5, 79), (1.5, 1.5, 84), (3.0, 0.5, 81), (3.5, 0.5, 79)],
        [(0.0, 1.0, 78), (1.0, 1.0, 76), (2.0, 0.5, 78), (2.5, 0.5, 81), (3.0, 1.0, 84)],
        [(0.0, 1.0, 82), (1.0, 1.0, 81), (2.0, 1.0, 79), (3.0, 1.0, 76)],
        [(0.0, 1.5, 77), (3.0, 0.5, 72), (3.5, 0.5, 74)],
    ]
    flute = [
        [(0.0, 1.5, 86), (1.5, 0.5, 84), (2.0, 2.0, 81)],
        [(0.5, 0.5, 77), (1.0, 0.5, 79), (1.5, 0.5, 81), (2.0, 1.0, 84), (3.0, 1.0, 86)],
        [(0.0, 1.5, 84), (1.5, 0.5, 81), (2.0, 2.0, 79)],
        [(0.0, 1.0, 77), (1.0, 0.5, 76), (1.5, 0.5, 77), (2.0, 1.0, 81), (3.0, 1.0, 84)],
        [(0.0, 2.0, 82), (2.0, 0.5, 81), (2.5, 0.5, 79), (3.0, 1.0, 77)],
        [(0.0, 1.0, 76), (1.0, 1.0, 79), (2.0, 2.0, 81)],
        [(0.0, 3.0, 79), (3.0, 0.5, 81), (3.5, 0.5, 84)],
        [(0.0, 1.5, 86), (1.5, 0.5, 84), (2.0, 2.0, 81)],
        [(0.0, 1.0, 82), (1.0, 1.0, 86), (2.0, 1.0, 84), (3.0, 1.0, 81)],
        [(0.0, 2.0, 85), (2.0, 1.0, 82), (3.0, 1.0, 79)],
        [(0.0, 1.5, 84), (1.5, 0.5, 81), (2.0, 2.0, 76)],
        [(0.0, 1.0, 77), (1.0, 1.0, 80), (2.0, 1.0, 83), (3.0, 1.0, 86)],
        [(0.0, 1.5, 86), (1.5, 0.5, 84), (2.0, 1.0, 82), (3.0, 1.0, 81)],
        [(0.0, 2.0, 79), (2.0, 0.5, 81), (2.5, 0.5, 82), (3.0, 1.0, 76)],
        [(0.0, 3.0, 77)],
        [],
    ]
    piece = Piece(116, len(chords), seed=116)
    for bar in range(len(chords)):
        if bar < 16:
            bossa_part(piece, chords, bar, vibes[bar], "vibes")
        else:
            bossa_part(piece, chords, bar, flute[bar - 16], "flute")
    # the vibraphone leads back into the start
    for beat, length, note in ((3.0, 0.5, 82), (3.5, 0.5, 79)):
        piece.add("vibes", 31 * 4 + beat, piece.band.vibes(hz(note), length * piece.beat, piece.velocity(0.6)))
    return piece.mix({
        # gain, pan, reverb send, auto-pan (depth, rate)
        "piano": (0.13, 0.0, 0.25, (0.35, 2.8)),
        "bass": (0.42, 0.0, 0.04, None),
        "vibes": (0.24, 0.2, 0.35, (0.12, 5.5)),
        "flute": (0.2, -0.15, 0.35, None),
        "shaker": (0.05, 0.45, 0.12, None),
        "rim": (0.06, -0.35, 0.15, None),
        "kick": (0.22, 0.0, 0.0, None),
    }, reverb_time=1.8, wet=0.55)


def setup():
    """'Setup': a calm piece in D, 92 beats a minute, 32 bars: the chords and a kalimba's arpeggios,
    then the flute's melody over them."""
    DMAJ9, BM9, GMAJ9 = (38, (54, 57, 61, 64)), (47, (62, 66, 69, 73)), (43, (54, 57, 59, 62))
    A13SUS, A7, FSM9 = (45, (55, 59, 62, 66)), (45, (55, 61, 64)), (42, (57, 61, 64, 68))
    BM9B, EM9, A7SUS = (47, (57, 61, 62, 66)), (40, (55, 59, 62, 66)), (45, (55, 59, 62, 64))
    FSM7, D_F, GM6, D_A = (42, (57, 61, 64)), (42, (57, 62, 64, 66)), (43, (58, 62, 64)), (45, (54, 57, 61, 64))
    progression = [DMAJ9, BM9, GMAJ9, [A13SUS, A7], FSM9, BM9B, EM9, [A7SUS, A7],
                   GMAJ9, FSM7, EM9, D_F, GMAJ9, GM6, D_A, [A13SUS, A7]]
    chords = progression * 2
    melody = [
        [(0.0, 1.5, 78), (1.5, 0.5, 76), (2.0, 1.0, 81), (3.0, 1.0, 85)],
        [(0.0, 2.0, 86), (2.0, 1.0, 85), (3.0, 1.0, 81)],
        [(0.0, 3.0, 83), (3.0, 0.5, 81), (3.5, 0.5, 78)],
        [(0.0, 2.0, 76), (2.0, 0.5, 78), (2.5, 0.5, 79), (3.0, 1.0, 81)],
        [(0.0, 1.5, 80), (1.5, 0.5, 81), (2.0, 1.0, 85), (3.0, 1.0, 83)],
        [(0.0, 2.0, 85), (2.0, 1.0, 86), (3.0, 1.0, 78)],
        [(0.0, 1.5, 79), (1.5, 0.5, 78), (2.0, 1.0, 74), (3.0, 1.0, 71)],
        [(0.0, 2.0, 76), (2.0, 2.0, 73)],
        [(0.0, 1.0, 74), (1.0, 1.0, 78), (2.0, 1.0, 81), (3.0, 1.0, 83)],
        [(0.0, 2.0, 85), (2.0, 1.0, 81), (3.0, 1.0, 76)],
        [(0.0, 1.5, 78), (1.5, 0.5, 79), (2.0, 2.0, 83)],
        [(0.0, 2.0, 81), (2.0, 1.0, 78), (3.0, 1.0, 76)],
        [(0.0, 1.0, 74), (1.0, 1.0, 76), (2.0, 1.0, 78), (3.0, 1.0, 81)],
        [(0.0, 2.0, 82), (2.0, 1.0, 81), (3.0, 1.0, 79)],
        [(0.0, 3.0, 78)],
        [(2.0, 1.0, 76), (3.0, 1.0, 73)],
    ]
    piece = Piece(92, len(chords), seed=92)
    band = piece.band
    for bar in range(len(chords)):
        halves = chords[bar] if isinstance(chords[bar], list) else [chords[bar], chords[bar]]
        following = chords[(bar + 1) % len(chords)]
        following = following[0] if isinstance(following, list) else following
        start = bar * 4
        second_pass = bar >= len(progression)
        # the piano, softly on 1 and 3, the pad under it
        for half, beat in ((0, 0), (1, 2)):
            velocity = piece.velocity(0.42)
            for i, note in enumerate(halves[half][1]):
                piece.add("piano", start + beat + i * 0.018, band.epiano(hz(note), 1.9 * piece.beat, velocity))
        for note in halves[0][1]:
            piece.add("pad", start, band.pad(hz(note + 12), (2 if halves[0] is not halves[1] else 4) * piece.beat, 0.5), steady=True)
        if halves[0] is not halves[1]:
            for note in halves[1][1]:
                piece.add("pad", start + 2, band.pad(hz(note + 12), 2 * piece.beat, 0.5), steady=True)
        # the bass
        first, second = halves[0][0], halves[1][0]
        fifth = first + 7 if first + 7 <= 52 else first - 5
        for beat, length, note in ((0, 1.9, first), (2, 1.4, second if halves[0] is not halves[1] else fifth), (3.5, 0.45, following[0])):
            piece.add("bass", start + beat, band.bass(hz(note), length * piece.beat, piece.velocity(0.75)))
        # the kalimba's arpeggio in eighths, left and right in turn
        for eighth in range(8):
            voicing = sorted(n + 12 for n in halves[eighth // 4][1])
            tones = voicing + [voicing[0] + 12, voicing[1] + 12]
            note = tones[(0, 2, 1, 3, 2, 4, 3, 1)[eighth]]
            velocity = piece.velocity((0.35 if second_pass else 0.5) * (1.0 if eighth % 2 == 0 else 0.75))
            piece.add("bells-left" if eighth % 2 == 0 else "bells-right", start + eighth / 2, band.kalimba(hz(note), velocity))
        # the drums: hats in eighths, brushes on 2 and 4, a soft kick
        for eighth in range(8):
            piece.add("hat", start + eighth / 2, band.hat(piece.velocity(0.5 if eighth % 2 == 0 else 0.3)))
        for beat in (1, 3):
            piece.add("brush", start + beat - 0.04, band.brush(piece.velocity(0.6)))
        for beat, velocity in ((0, 0.7), (2.5, 0.4)):
            piece.add("kick", start + beat, band.kick(piece.velocity(velocity)), steady=True)
        if second_pass:
            for beat, length, note in melody[bar - len(progression)]:
                piece.add("flute", start + beat, band.flute(hz(note), length * piece.beat * 0.95, piece.velocity(0.75)))
    return piece.mix({
        "piano": (0.12, 0.0, 0.3, (0.25, 1.6)),
        "pad": (0.035, 0.0, 0.5, None),
        "bass": (0.4, 0.0, 0.04, None),
        "bells-left": (0.1, -0.5, 0.45, None),
        "bells-right": (0.1, 0.5, 0.45, None),
        "flute": (0.2, 0.1, 0.4, None),
        "hat": (0.035, 0.35, 0.1, None),
        "brush": (0.04, -0.25, 0.2, None),
        "kick": (0.2, 0.0, 0.0, None),
    }, reverb_time=2.2, wet=0.6)


# -- the menu's sounds ----------------------------------------------------------------------------

def chime(f, length, tau, harmonics=(1.0, 0.3, 0.1)):
    t = seconds(length)
    tone = np.zeros_like(t)
    for n, level in enumerate(harmonics, 1):
        tone += level * np.sin(2 * np.pi * f * n * t) * np.exp(-t * n / tau)
    return tone * rise(t, 0.002)


def place(sounds, length):
    """sounds at their times (seconds), mixed into one"""
    out = np.zeros(int(length * SR))
    for at, sound in sounds:
        start = int(at * SR)
        end = min(len(out), start + len(sound))
        out[start:end] += sound[:end - start]
    return out


def room(x, band):
    """a small room's ring, so the sounds sit softly"""
    t = seconds(0.4)
    response = band.noise(len(t)) * np.exp(-6.91 * t / 0.35)
    response = bandpass(response, high=6000)
    response /= np.sqrt(np.sum(response ** 2))
    return x + 0.18 * signal.oaconvolve(x, response)[:len(x)]


def effects():
    band = Band(7)
    sounds = {}
    # moving: a soft rising blip
    t = seconds(0.09)
    phase = 2 * np.pi * np.cumsum(1100 * (1 + 0.35 * (1 - np.exp(-t / 0.012)))) / SR
    blip = (np.sin(phase) + 0.25 * np.sin(2 * phase) * np.exp(-t / 0.01)) * rise(t, 0.0015) * np.exp(-t / 0.022)
    sounds["move"] = (blip, 0.16)
    # choosing: two bright notes up a fifth
    sounds["select"] = (place([(0, chime(hz(88), 0.5, 0.12)), (0.06, 0.9 * chime(hz(95), 0.5, 0.16))], 0.6), 0.26)
    # going back: two rounder notes down a fifth
    sounds["back"] = (place([(0, chime(hz(83), 0.35, 0.07, (1.0, 0.15))), (0.055, chime(hz(76), 0.35, 0.1, (1.0, 0.15)))], 0.45), 0.2)
    # nothing to choose there (yet): two low, muffled bumps
    def bump(f):
        t = seconds(0.09)
        tone = sum(np.sin(2 * np.pi * f * n * t) / n for n in (1, 3, 5))
        return bandpass(tone, high=1200) * rise(t, 0.003) * np.exp(-t / 0.035)
    sounds["denied"] = (place([(0, bump(196)), (0.1, bump(185))], 0.3), 0.24)
    # a game starting: a rising arpeggio and a sparkle
    arpeggio = [(i * 0.05, chime(hz(note), 1.0, 0.25)) for i, note in enumerate((84, 88, 91, 96))]
    t = seconds(0.8)
    sparkle = sum(np.sin(2 * np.pi * f * t) * np.exp(-t / 0.18) * 0.12 for f in (4186, 5274, 6272))
    sparkle *= rise(t, 0.03)
    sounds["launch"] = (place(arpeggio + [(0.2, sparkle)], 1.1), 0.28)
    out = {}
    for name, (sound, peak) in sounds.items():
        sound = room(sound, band)
        out[name] = sound * (peak / np.max(np.abs(sound)))
    return out


# -- files ----------------------------------------------------------------------------------------

IMA_STEPS = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66,
             73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449,
             494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272,
             2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493,
             10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767]
IMA_INDEX = [-1, -1, -1, -1, 2, 4, 6, 8]
BLOCK_ALIGN = 2048  # bytes a block, both channels


def to_pcm16(x):
    return np.clip(np.round(x * 32767), -32768, 32767).astype(np.int16)


def write_pcm(path, mono):
    data = to_pcm16(mono).tobytes()
    with open(path, "wb") as f:
        f.write(b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVE")
        f.write(b"fmt " + struct.pack("<IHHIIHH", 16, 1, 1, SR, SR * 2, 2, 16))
        f.write(b"data" + struct.pack("<I", len(data)) + data)


def write_adpcm(path, stereo):
    """Microsoft's IMA ADPCM WAV: blocks of a header for each channel (its first sample and step
    index), then 4-byte groups of eight 4-bit samples, a channel's at a time"""
    samples = to_pcm16(stereo).astype(np.int32)
    channels = samples.shape[0]
    per_block = (BLOCK_ALIGN - 4 * channels) * 8 // (4 * channels) + 1
    frames = samples.shape[1]
    blocks = -(-frames // per_block)
    padded = np.zeros((channels, blocks * per_block), dtype=np.int32)
    padded[:, :frames] = samples
    state = [[0, 0] for _ in range(channels)]  # predictor, step index
    out = bytearray()
    for block in range(blocks):
        chunk = padded[:, block * per_block:(block + 1) * per_block].tolist()
        for c in range(channels):
            state[c][0] = chunk[c][0]
            out += struct.pack("<hBB", chunk[c][0], state[c][1], 0)
        codes = [encode_ima(chunk[c][1:], state[c]) for c in range(channels)]
        for group in range((per_block - 1) // 8):
            for c in range(channels):
                nibbles = codes[c][group * 8:group * 8 + 8]
                out += bytes(nibbles[i] | (nibbles[i + 1] << 4) for i in range(0, 8, 2))
    with open(path, "wb") as f:
        fmt = struct.pack("<HHIIHHHH", 0x11, channels, SR, SR * BLOCK_ALIGN // per_block, BLOCK_ALIGN, 4, 2, per_block)
        f.write(b"RIFF" + struct.pack("<I", 4 + 8 + len(fmt) + 12 + 8 + len(out)) + b"WAVE")
        f.write(b"fmt " + struct.pack("<I", len(fmt)) + fmt)
        f.write(b"fact" + struct.pack("<II", 4, frames))
        f.write(b"data" + struct.pack("<I", len(out)) + out)


def encode_ima(samples, state):
    predictor, index = state
    codes = []
    for sample in samples:
        step = IMA_STEPS[index]
        difference = sample - predictor
        code = 0
        if difference < 0:
            code = 8
            difference = -difference
        delta = step >> 3
        if difference >= step:
            code |= 4
            difference -= step
            delta += step
        if difference >= step >> 1:
            code |= 2
            difference -= step >> 1
            delta += step >> 1
        if difference >= step >> 2:
            code |= 1
            delta += step >> 2
        predictor = predictor - delta if code & 8 else predictor + delta
        predictor = -32768 if predictor < -32768 else 32767 if predictor > 32767 else predictor
        index += IMA_INDEX[code & 7]
        index = 0 if index < 0 else 88 if index > 88 else index
        codes.append(code)
    state[0], state[1] = predictor, index
    return codes


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    folder = sys.argv[1]
    os.makedirs(folder, exist_ok=True)
    for name, sound in effects().items():
        write_pcm(os.path.join(folder, f"{name}.wav"), sound)
    for name, piece in (("shop", shop), ("setup", setup)):
        stereo = master(piece(), loudness_db=-20)
        write_adpcm(os.path.join(folder, f"music-{name}.wav"), stereo)
        print(f"music-{name}.wav: {stereo.shape[1] / SR:.1f} s, peak {20 * np.log10(np.max(np.abs(stereo))):.1f} dBFS")


if __name__ == "__main__":
    main()
