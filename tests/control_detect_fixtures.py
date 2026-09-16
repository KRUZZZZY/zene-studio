#!/usr/bin/env python3
"""The detect.* transcript's fixtures: synthesised with the standard library.

Split out of tests/control-detect-commands.py (2026-09-15, 030/ratchet-decision,
REPO-58) so that driver has room for the fix of the worst fork-authored function in
the tree: `run_checks` measured CCN 75 at the whole-tree scope, the driver sits at
the 500-line limit, and splitting that function needs lines the fixtures were
holding. The fixtures are the same synthesis, and the driver's own equivalence
check compares the samples this module returns with the ones it built in place
before the split.

Why they are synthesised rather than recorded: every check in the driver is that
the engine recovers a KNOWN answer - a click track at exactly `CLICK_BPM` whose
first click lands at `CLICK_FIRST_BEAT_S`, and an A major scale over an A bass. A
fixture the engine's own decoder wrote would make those assertions a round trip
through the thing under test.

The constants the fixture and the answer share live here with it (`SAMPLE_RATE`,
`CLICK_BPM`, `CLICK_FIRST_BEAT_S`, `TONE_HZ`); the tolerances and the method names
stay in the driver, because they are what the driver asserts, not what it builds.

Written with the standard library, NOT by the engine's decoder: the engine's own
libsndfile path is what reads these files back, so a wrong header fails loudly in
every check that follows.
"""

import math
import struct
import wave

SAMPLE_RATE = 44100
CLICK_BPM = 128.0
CLICK_FIRST_BEAT_S = 0.5
CLICK_SECONDS = 10.0
TONE_HZ = 220.0


def write_wave(path, samples):
    """A canonical 16-bit mono RIFF/WAVE. The engine's own libsndfile path is
    what reads it back, so the header this writes is validated by every check
    that follows (a wrong header would make decode fail loudly)."""
    with wave.open(path, "wb") as handle:
        handle.setnchannels(1)
        handle.setsampwidth(2)
        handle.setframerate(SAMPLE_RATE)
        handle.writeframes(b"".join(
            struct.pack("<h", max(-32767, min(32767, int(value * 32767.0)))) for value in samples))


def click_track():
    """A click track at CLICK_BPM whose first click is at CLICK_FIRST_BEAT_S."""
    total = int(CLICK_SECONDS * SAMPLE_RATE)
    samples = [0.0] * total
    burst = SAMPLE_RATE // 40  # 25 ms
    beat = CLICK_FIRST_BEAT_S
    while beat < CLICK_SECONDS:
        start = int(beat * SAMPLE_RATE)
        for i in range(burst):
            if start + i >= total:
                break
            envelope = math.exp(-8.0 * i / burst)
            samples[start + i] += 0.8 * envelope * math.sin(2.0 * math.pi * 1000.0 * i / SAMPLE_RATE)
        beat += 60.0 / CLICK_BPM
    return samples


def major_fixture():
    """An A major scale (A B C# D E F# G# A) over an A bass: the answer is
    'A, major' by construction. The bass is what makes the TONIC measurable -
    see the note on the tonic weighting in docs/IMPORT-DETECTION.md."""
    total = int(8.0 * SAMPLE_RATE)
    samples = [0.0] * total
    scale = [220.00, 246.94, 277.18, 293.66, 329.63, 369.99, 415.30, 440.00]

    def add_tone(hz, start_s, length_s, amplitude):
        start = int(start_s * SAMPLE_RATE)
        length = int(length_s * SAMPLE_RATE)
        fade = int(0.02 * SAMPLE_RATE)
        for i in range(length):
            if start + i >= total:
                break
            envelope = 1.0
            if i < fade:
                envelope = i / fade
            elif i + fade >= length:
                envelope = (length - i) / fade
            samples[start + i] += amplitude * envelope * math.sin(
                2.0 * math.pi * hz * i / SAMPLE_RATE)

    for repeat in range(2):
        for note, hz in enumerate(scale):
            add_tone(hz, repeat * 3.0 + note * 0.35, 0.32, 0.35)
    add_tone(110.0, 0.0, 8.0, 0.45)
    return samples


def steady_tone():
    """A tone with no transients at all: the refusal fixture."""
    return [0.4 * math.sin(2.0 * math.pi * TONE_HZ * i / SAMPLE_RATE)
            for i in range(int(6.0 * SAMPLE_RATE))]
