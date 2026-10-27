#!/usr/bin/env python3
"""Q050-062.3: tools/stem_quality_bench.py `run`, checked hermetically (ctest StemQualityBench).

The real-model measurement (doc/STEM-SPLIT.md "End-to-end §5") is a run of that tool on the pinned
HTDemucs model and MUSDB18-HQ excerpts; this test proves the tool computes what the doc says from it,
without the 166 MB model or the network. It builds one synthetic excerpt whose reference stems are
fixed multiples of the mix and separates it with the committed stub model
(tests/data/stub-4stem-linear.onnx: every stem = 0.4 / 0.3 / 0.2 / 0.1 x the mix). With ref = g x mix
and est = s x mix the SDR is 20 log10(g / |g - s|) whatever the mix is, so every figure the tool
records has a known value. It then checks that all three qualities were run, that their model pass
counts rise with the overlap, that each SDR matches, and that the record carries the model / CLI
identity and run conditions the doc relies on.

Usage: stem-quality-bench.py <stub model>. Exit 0 pass, 1 fail, 77 skip (no numpy / onnxruntime).
"""
import json
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, os.pardir, "tools", "stem_quality_bench.py")
STEMS = ("drums", "bass", "other", "vocals")
QUALITIES = ("fast", "standard", "high")
SAMPLE_RATE = 44100
REFERENCE_GAINS = {"drums": 0.5, "bass": 0.3, "other": 0.1, "vocals": 0.1}
STUB_GAINS = {"drums": 0.4, "bass": 0.3, "other": 0.2, "vocals": 0.1}


def write_f32(path, data):
    data.T.astype("<f4").tofile(path)


def make_reference_set(refset):
    """One 3 s excerpt; returns the SDR each stem must score (None: an exact
    estimate, which must score very high)."""
    import numpy as np
    excerpt = os.path.join(refset, "synthetic")
    os.makedirs(excerpt)
    mix = (0.3 * np.random.default_rng(20261027).standard_normal((2, 3 * SAMPLE_RATE))).astype(np.float32)
    write_f32(os.path.join(excerpt, "mix.f32"), mix)
    expected = {}
    for stem in STEMS:
        gain = REFERENCE_GAINS[stem]
        write_f32(os.path.join(excerpt, stem + ".f32"), (gain * mix).astype(np.float32))
        miss = abs(gain - STUB_GAINS[stem])
        expected[stem] = None if miss == 0 else 20.0 * np.log10(gain / miss)
    return expected


def row_failures(row, expected):
    failures = []
    for stem, want in expected.items():
        got = row["sdr"][stem]
        if want is None and got < 60.0:
            failures.append(f"{row['quality']}/{stem}: exact estimate scored {got} dB, want >= 60")
        if want is not None and abs(got - want) > 0.01:
            failures.append(f"{row['quality']}/{stem}: SDR {got} dB, want {want:.4f}")
    if not row["elapsed_s"] > 0 or row["hop"] < 1:
        failures.append(f"{row['quality']}: elapsed {row['elapsed_s']} hop {row['hop']}")
    return failures


def record_failures(record):
    failures = []
    rows = record["rows"]
    if sorted({row["quality"] for row in rows}) != sorted(QUALITIES):
        failures.append(f"qualities measured {[r['quality'] for r in rows]}")
    chunks = {row["quality"]: row["chunks"] for row in rows}
    if not chunks.get("fast", 0) < chunks.get("standard", 0) < chunks.get("high", 0):
        failures.append(f"chunk counts do not rise with quality: {chunks}")
    return failures + identity_failures(record)


def identity_failures(record):
    missing = [key for key in ("model_sha256", "cli_sha256", "onnxruntime", "python")
               if not record["conditions"].get(key)]
    failures = [f"run conditions lack {key}" for key in missing]
    if set(record["summary"]) != set(QUALITIES):
        failures.append(f"summary covers {sorted(record['summary'])}")
    if set(record["data"].get("synthetic", {})) != set(STEMS) | {"mix"}:
        failures.append(f"data identity covers {sorted(record['data'])}")
    return failures


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    try:
        import numpy  # noqa: F401
        import onnxruntime  # noqa: F401
    except ImportError as exc:
        print(f"SKIP: {exc}")
        return 77
    with tempfile.TemporaryDirectory() as scratch:
        expected = make_reference_set(os.path.join(scratch, "refset"))
        out = os.path.join(scratch, "record.json")
        command = [sys.executable, TOOL, "run", "--refset", os.path.join(scratch, "refset"),
                   "--model", sys.argv[1], "--work", os.path.join(scratch, "work"),
                   "--out", out, "--segment", "16384"]
        result = subprocess.run(command, capture_output=True, text=True, check=False)
        if result.returncode != 0 or not os.path.exists(out):
            print(f"FAIL: run exited {result.returncode}\n{result.stdout[-800:]}{result.stderr[-800:]}")
            return 1
        with open(out) as fh:
            record = json.load(fh)
    failures = record_failures(record)
    for row in record["rows"]:
        failures += row_failures(row, expected)
    for failure in failures:
        print("FAIL:", failure)
    print("PASS" if not failures else f"{len(failures)} failure(s)")
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())
