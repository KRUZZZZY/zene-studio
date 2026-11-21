#!/usr/bin/env python3
"""Regression test for rejecting invalid rates in the NAM harness."""

import json
import pathlib
import struct
import subprocess
import sys
import tempfile


def make_model(path):
	model = {
		"architecture": "WaveNet",
		"sample_rate": 48000,
		"config": {
			"layers": [{
				"input_size": 1,
				"condition_size": 1,
				"head_size": 1,
				"head_bias": False,
				"channels": 1,
				"bottleneck": 1,
				"kernel_size": 1,
				"dilations": [1],
				"layer1x1": {"active": False},
			}],
		},
		"weights": [0, 0, 0, 0, 0, 1],
	}
	path.write_text(json.dumps(model), encoding="utf-8")


def make_zero_rate_wav(path):
	header = struct.pack(
		"<4sI4s4sIHHIIHH4sI",
		b"RIFF", 38, b"WAVE", b"fmt ", 16,
		1, 1, 0, 0, 2, 16, b"data", 2,
	)
	path.write_bytes(header + b"\x00\x00")


def run_case(harness, model, args):
	return subprocess.run(
		[harness, str(model), *args], check=False,
		capture_output=True, text=True,
	)


def main():
	if len(sys.argv) != 2:
		print("usage: nam_harness_rate_test.py <nam_harness>", file=sys.stderr)
		return 2
	harness = str(pathlib.Path(sys.argv[1]).resolve())
	failures = []
	with tempfile.TemporaryDirectory(prefix="nam-rate-test-") as tmp:
		root = pathlib.Path(tmp)
		model = root / "minimal.nam"
		make_model(model)
		for rate in ("-48000", "0", "nan", "inf", "-inf"):
			result = run_case(harness, model, [
				"--block", "1", "--blocks", "1", "--warmup", "0",
				"--sample-rate", rate,
			])
			output = result.stdout + result.stderr
			if result.returncode != 2 or "--sample-rate" not in output:
				failures.append(
					f"--sample-rate {rate}: expected usage error mentioning --sample-rate, "
					f"got exit {result.returncode} and output {output!r}"
				)

		wav = root / "zero-rate.wav"
		out = root / "zero-rate-output.wav"
		make_zero_rate_wav(wav)
		result = run_case(harness, model, [
			"--block", "1", "--wav-run", str(wav), "--wav-out", str(out),
		])
		output = result.stdout + result.stderr
		if result.returncode != 2:
			failures.append(
				"zero-rate WAV: expected usage error, "
				f"got exit {result.returncode} and output {output!r}"
			)
		if "could not read WAV" not in output:
			failures.append(f"zero-rate WAV: expected WAV rejection message, got {output!r}")
		if out.exists():
			failures.append("zero-rate WAV: harness wrote an output file")

	if failures:
		for failure in failures:
			print(failure, file=sys.stderr)
		return 1
	print("NAM harness rejects invalid generated and WAV sample rates")
	return 0


if __name__ == "__main__":
	sys.exit(main())
