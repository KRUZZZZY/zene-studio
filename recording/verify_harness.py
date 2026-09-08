#!/usr/bin/env python3
"""Independent cross-check of the two-track recording harness output.

Reads the two WAV files the C++ harness produced and re-derives the expected
synthetic signal from scratch using only the Python standard library (wave
module + struct). Cross-checks the frame counts, the exact sample values, the
24-bit header fields and the FNV-1a digests the harness recorded.

This verifies a SYNTHETIC source, not a hardware capture.
"""

import struct
import sys
import wave
from pathlib import Path

SAMPLE_RATE = 48000
TOTAL_FRAMES = 96000

# track index -> (wav file, input channel the track must have recorded)
TRACKS = {
    0: ("track0_ch1.wav", 1),
    1: ("track1_ch0.wav", 0),
}

FNV_OFFSET = 14695981039346656037
FNV_PRIME = 1099511628211


def channel_value(channel: int, frame: int) -> float:
    if channel == 0:
        return struct.unpack("<f", struct.pack("<f", (frame % 64 - 32) / 64.0))[0]
    return struct.unpack("<f", struct.pack("<f", (frame % 30 - 15) * 0.046875))[0]


def fnv1a(data: bytes, digest: int = FNV_OFFSET) -> int:
    for byte in data:
        digest ^= byte
        digest = (digest * FNV_PRIME) & 0xFFFFFFFFFFFFFFFF
    return digest


def decode_int24(raw: bytes) -> list[float]:
    values = []
    for i in range(0, len(raw), 3):
        sample = int.from_bytes(raw[i:i + 3], "little", signed=True)
        values.append(sample / 8388608.0)  # exact: power-of-two divisor
    return values


def main() -> int:
    out_dir = Path(sys.argv[1] if len(sys.argv) > 1 else "/tmp/lmms-recording-harness")
    digest_file = out_dir / "harness-digests.txt"
    if not digest_file.exists():
        print(f"FAIL: {digest_file} not found - run the harness first")
        return 1

    harness = {}
    for line in digest_file.read_text().splitlines():
        parts = line.split()
        harness[parts[0]] = parts[1:]

    failures = 0

    def check(ok: bool, what: str) -> None:
        nonlocal failures
        print(f"[{'OK' if ok else 'FAIL'}] {what}")
        if not ok:
            failures += 1

    print("=== INDEPENDENT PYTHON CROSS-CHECK OF THE SYNTHETIC HARNESS OUTPUT ===")

    for index, (filename, input_channel) in TRACKS.items():
        path = out_dir / filename
        print(f"\n--- track {index}: {path} (expected input channel {input_channel}) ---")
        with wave.open(str(path), "rb") as wav:
            channels = wav.getnchannels()
            sampwidth = wav.getsampwidth()
            framerate = wav.getframerate()
            frames = wav.getnframes()
            raw = wav.readframes(frames)

        check(channels == 1, f"header: channels == 1 (got {channels})")
        check(sampwidth == 3, f"header: 24-bit samples (got {sampwidth * 8}-bit)")
        check(framerate == SAMPLE_RATE, f"header: sample rate == {SAMPLE_RATE} (got {framerate})")
        check(frames == TOTAL_FRAMES, f"header: frames == {TOTAL_FRAMES} (got {frames})")

        decoded = decode_int24(raw)
        expected = [channel_value(input_channel, i) for i in range(TOTAL_FRAMES)]

        mismatches = sum(1 for got, want in zip(decoded, expected) if got != want)
        max_error = max(abs(got - want) for got, want in zip(decoded, expected))
        check(mismatches == 0, f"every sample equals the synthetic source (mismatches={mismatches}, max|err|={max_error:g})")

        digest = fnv1a(b"".join(struct.pack("<f", value) for value in decoded))
        expected_digest = fnv1a(b"".join(struct.pack("<f", value) for value in expected))
        recorded = harness.get(f"track{index}", [None, None])
        expected_recorded = harness.get(f"expected_track{index}", [None])

        print(f"  python digest          : 0x{digest:016x}")
        print(f"  python expected digest : 0x{expected_digest:016x}")
        print(f"  harness digest         : 0x{int(recorded[0], 16):016x}" if recorded[0] else "  harness digest: missing")
        check(digest == expected_digest, "python digest matches the re-derived expected signal")
        if recorded[0]:
            check(digest == int(recorded[0], 16), "python digest matches the harness's digest")
        if expected_recorded[0]:
            check(expected_digest == int(expected_recorded[0], 16), "python expected digest matches the harness's expected digest")
        if recorded[1]:
            check(int(recorded[1]) == TOTAL_FRAMES, "harness reported frames recorded == expected")

    allocations = harness.get("allocations", [None])[0]
    check(allocations == "0", f"harness reported 0 producer-thread allocations (got {allocations})")

    print("\n=== PYTHON CROSS-CHECK RESULT:", "PASS ===" if failures == 0 else f"FAIL ({failures}) ===")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
