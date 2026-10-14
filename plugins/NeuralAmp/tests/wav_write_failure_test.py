#!/usr/bin/env python3
"""Exercise NAM harness output failures through its command-line paths."""

import json
import math
import struct
import subprocess
import sys
import tempfile
from pathlib import Path


def run_case(harness, model, args, label, expected_output, expected_error=None,
             require_signal_pass=False):
    result = subprocess.run(
        [str(harness), str(model), *args], capture_output=True, text=True
    )
    if result.returncode != 2:
        raise AssertionError(
            f"{label} expected exit 2 for a failed WAV write, got {result.returncode}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    if expected_output not in result.stdout:
        raise AssertionError(
            f"{label} did not reach the expected WAV output path: {expected_output!r}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    if expected_error is not None and expected_error not in result.stderr:
        raise AssertionError(
            f"{label} did not report the expected write failure: {expected_error!r}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    if require_signal_pass and not all(
        marker in result.stdout
        for marker in (
            "output finite: yes",
            "output non-zero: yes",
            "signal colouring (not a pass-through): PASS",
        )
    ):
        raise AssertionError(
            f"{label} did not pass the signal checks\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    print(f"PASS {label}: exit={result.returncode}")


def make_input_wav(path):
    samples = [0.2 * math.sin(2.0 * math.pi * 220.0 * i / 48000.0) for i in range(512)]
    data = b"".join(struct.pack("<f", sample) for sample in samples)
    path.write_bytes(
        b"RIFF"
        + struct.pack("<I", 36 + len(data))
        + b"WAVEfmt "
        + struct.pack("<IHHIIHH", 16, 3, 1, 48000, 192000, 4, 32)
        + b"data"
        + struct.pack("<I", len(data))
        + data
    )


def main():
    if len(sys.argv) != 3:
        raise SystemExit(
            "usage: wav_write_failure_test.py /path/to/nam_harness "
            "{legacy-wav,write-input-wav,wav-run}"
        )
    harness = Path(sys.argv[1]).resolve()
    case = sys.argv[2]
    full_device = Path("/dev/full")
    if not harness.is_file():
        raise SystemExit(f"NAM harness does not exist: {harness}")
    if not full_device.exists():
        raise SystemExit("this regression requires the Linux /dev/full device")

    with tempfile.TemporaryDirectory(prefix="nam-wav-write-") as tmp:
        model = Path(tmp) / "minimal.nam"
        model.write_text(
            json.dumps(
                {
                    "architecture": "WaveNet",
                    "version": "0.5.0",
                    "sample_rate": 48000,
                    "config": {
                        "layers": [
                            {
                                "input_size": 1,
                                "condition_size": 1,
                                "head_size": 1,
                                "head_bias": False,
                                "channels": 1,
                                "dilations": [1],
                                "kernel_size": 1,
                                "layer1x1": {"active": False},
                            }
                        ]
                    },
                    "weights": [1.0, 1.0, 0.0, 0.0, 1.0, 3.0],
                }
            ),
            encoding="utf-8",
        )
        if case == "legacy-wav":
            run_case(
                harness,
                model,
                ["--blocks", "8", "--warmup", "1", "--wav", str(full_device)],
                "legacy --wav",
                f"wrote {full_device}: FAILED",
                require_signal_pass=True,
            )
        elif case == "write-input-wav":
            run_case(
                harness,
                model,
                ["--blocks", "8", "--warmup", "1", "--write-input-wav", str(full_device)],
                "--write-input-wav",
                "== NeuralAmp harness ==",
                f"error: could not write {full_device}",
            )
        elif case == "wav-run":
            input_wav = Path(tmp) / "input.wav"
            make_input_wav(input_wav)
            run_case(
                harness,
                model,
                ["--wav-run", str(input_wav), "--wav-out", str(full_device)],
                "--wav-run --wav-out",
                f"wav-out: {input_wav} -> {full_device}",
            )
        else:
            raise SystemExit(f"unknown WAV write failure case: {case}")
    print(f"PASS {case}: WAV write failure detected")


if __name__ == "__main__":
    main()
