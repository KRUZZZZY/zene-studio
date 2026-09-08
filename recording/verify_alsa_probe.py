#!/usr/bin/env python3
"""Sample-exact cross-check of the REAL hardware capture probe output.

The probe (TwoTrackAlsaCaptureProbe) captured 2-channel S16_LE from a real ALSA
device, demuxed the interleaved frames into two per-track ring buffers, wrote
two mono WAVs, and also wrote a reference stereo WAV from the exact same
pre-allocated frame buffer it fed to the recorder.

This script re-reads all three files with the Python standard library and
asserts, sample by sample:

  track0_ch1.wav  ==  right channel of reference_stereo.wav
  track1_ch0.wav  ==  left  channel of reference_stereo.wav

so any lost, duplicated, reordered or altered sample in the demux -> ring
buffer -> disk-writer chain shows up as a mismatch. Decoding is exact: the WAVs
are 24-bit PCM and 24-bit ints divide by 2**23 without rounding error.

Usage: verify_alsa_probe.py [capture_dir]   (default /tmp/lmms-recording-alsa)
"""

import math
import struct
import sys
import wave
from pathlib import Path

REFERENCE = "reference_stereo.wav"
# track file -> (reference channel index, human name)
TRACKS = {
    "track0_ch1.wav": (1, "right"),
    "track1_ch0.wav": (0, "left"),
}

FNV_OFFSET = 14695981039346656037
FNV_PRIME = 1099511628211


def read_wav(path: Path):
    with wave.open(str(path), "rb") as wav:
        channels = wav.getnchannels()
        sampwidth = wav.getsampwidth()
        framerate = wav.getframerate()
        frames = wav.getnframes()
        raw = wav.readframes(frames)
    return channels, sampwidth, framerate, frames, raw


def decode_int24(raw: bytes):
    return [
        int.from_bytes(raw[i:i + 3], "little", signed=True) / 8388608.0
        for i in range(0, len(raw), 3)
    ]


def deinterleave(values, channels: int, channel: int):
    return values[channel::channels]


def rms(values) -> float:
    if not values:
        return 0.0
    return math.sqrt(sum(v * v for v in values) / len(values))


def fnv1a(data: bytes, digest: int = FNV_OFFSET) -> int:
    for byte in data:
        digest ^= byte
        digest = (digest * FNV_PRIME) & 0xFFFFFFFFFFFFFFFF
    return digest


def float_bytes(values) -> bytes:
    return b"".join(struct.pack("<f", v) for v in values)


def main() -> int:
    capture_dir = Path(sys.argv[1] if len(sys.argv) > 1 else "/tmp/lmms-recording-alsa")
    ref_path = capture_dir / REFERENCE
    if not ref_path.exists():
        print(f"FAIL: {ref_path} not found - run TwoTrackAlsaCaptureProbe first")
        return 1

    print("=== SAMPLE-EXACT CROSS-CHECK OF THE REAL HARDWARE CAPTURE ===")
    print(f"capture dir: {capture_dir}")

    ref_ch, ref_sw, ref_rate, ref_frames, ref_raw = read_wav(ref_path)
    print(f"\nreference : {REFERENCE} channels={ref_ch} bits={ref_sw * 8} "
          f"rate={ref_rate} frames={ref_frames}")

    failures = 0

    def check(ok: bool, what: str) -> None:
        nonlocal failures
        print(f"[{'OK' if ok else 'FAIL'}] {what}")
        if not ok:
            failures += 1

    check(ref_ch == 2, f"reference is stereo (got {ref_ch} channels)")
    check(ref_sw == 3, f"reference is 24-bit (got {ref_sw * 8}-bit)")

    ref_values = decode_int24(ref_raw)
    ref_channels = [deinterleave(ref_values, ref_ch, c) for c in range(ref_ch)]
    for c in range(ref_ch):
        print(f"  reference ch{c} rms = {rms(ref_channels[c]):.6f}")

    for filename, (ref_channel, name) in TRACKS.items():
        path = capture_dir / filename
        print(f"\n--- {filename} (must equal reference {name} channel {ref_channel}) ---")
        if not path.exists():
            check(False, f"{filename} exists")
            continue

        ch, sw, rate, frames, raw = read_wav(path)
        values = decode_int24(raw)
        expected = ref_channels[ref_channel]

        print(f"  channels={ch} bits={sw * 8} rate={rate} frames={frames} "
              f"rms={rms(values):.6f}")
        check(ch == 1, f"track file is mono (got {ch})")
        check(sw == 3, f"track file is 24-bit (got {sw * 8}-bit)")
        check(rate == ref_rate, f"sample rate matches reference ({rate} vs {ref_rate})")
        check(frames == ref_frames, f"frame count matches reference ({frames} vs {ref_frames})")
        check(len(values) == len(expected), "decoded sample count matches reference")

        mismatches = sum(1 for got, want in zip(values, expected) if got != want)
        max_error = max((abs(got - want) for got, want in zip(values, expected)), default=0.0)
        check(mismatches == 0,
              f"every sample equals the reference {name} channel "
              f"(mismatches={mismatches}, max|err|={max_error:g})")

        digest = fnv1a(float_bytes(values))
        ref_digest = fnv1a(float_bytes(expected))
        print(f"  track digest     : 0x{digest:016x}")
        print(f"  reference digest : 0x{ref_digest:016x}")
        check(digest == ref_digest, "digest matches the reference channel")

    print("\n=== RESULT:", "PASS ===" if failures == 0 else f"FAIL ({failures} check(s)) ===")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
