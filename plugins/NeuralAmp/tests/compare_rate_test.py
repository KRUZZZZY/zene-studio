#!/usr/bin/env python3
"""Regression test for WAV sample-rate validation in nam_harness."""

import json
import pathlib
import struct
import subprocess
import sys
import tempfile
import wave


def _run(harness, model, input_wav, reference=None, output_wav=None):
	command = [str(harness), str(model), "--wav-run", str(input_wav), "--block", "3"]
	if reference is not None:
		command.extend(["--compare-ref", str(reference)])
	if output_wav is not None:
		command.extend(["--wav-out", str(output_wav)])
	return subprocess.run(command, capture_output=True, text=True, check=False)


def main():
	if len(sys.argv) != 2:
		print(f"usage: {sys.argv[0]} <nam_harness>", file=sys.stderr)
		return 2

	harness = pathlib.Path(sys.argv[1]).resolve()
	with tempfile.TemporaryDirectory(prefix="nam-rate-test-") as temp_dir:
		temp = pathlib.Path(temp_dir)
		model = temp / "identity.nam"
		input_wav = temp / "input.wav"
		output_wav = temp / "output.wav"
		reference = temp / "reference.wav"
		model.write_text(json.dumps({
			"architecture": "WaveNet",
			"version": "test",
			"sample_rate": 48000,
			"config": {"layers": [{
				"input_size": 1,
				"condition_size": 1,
				"head_size": 1,
				"head_bias": False,
				"channels": 1,
				"bottleneck": 1,
				"layer1x1": {"active": False},
				"dilations": [1],
				"kernel_size": 1,
			}]},
			"weights": [1.0, 0.0, 0.0, 1.0, 1.0, 1.0],
		}), encoding="utf-8")
		with wave.open(str(input_wav), "wb") as wav:
			wav.setnchannels(1)
			wav.setsampwidth(2)
			wav.setframerate(48000)
			wav.writeframes(struct.pack("<4h", 4000, -8000, 12000, -16000))

		generated = _run(harness, model, input_wav, output_wav=output_wav)
		if generated.returncode != 0:
			print("FAIL: could not generate the reference render", file=sys.stderr)
			print(generated.stdout, generated.stderr, file=sys.stderr)
			return 1

		matching = _run(harness, model, input_wav, reference=output_wav)
		if matching.returncode != 0:
			print("FAIL: a same-rate reference was rejected", file=sys.stderr)
			print(matching.stdout, matching.stderr, file=sys.stderr)
			return 1

		ref_bytes = bytearray(output_wav.read_bytes())
		struct.pack_into("<I", ref_bytes, 24, 44100)
		struct.pack_into("<I", ref_bytes, 28, 44100 * 4)
		reference.write_bytes(ref_bytes)
		mismatched = _run(harness, model, input_wav, reference=reference)
		message = mismatched.stdout + mismatched.stderr
		if (mismatched.returncode != 2 or "reference sample rate mismatch" not in message):
			print("FAIL: different-rate reference was not rejected", file=sys.stderr)
			print(f"exit code: {mismatched.returncode}", file=sys.stderr)
			print(message, file=sys.stderr)
			return 1

	print("PASS: same-rate comparison succeeds and a different-rate reference is rejected")
	return 0


if __name__ == "__main__":
	sys.exit(main())
