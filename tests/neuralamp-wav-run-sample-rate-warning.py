#!/usr/bin/env python3
"""Check that WAV-run compares the model rate with the input WAV rate."""

import subprocess
import sys
import tempfile
import wave
from pathlib import Path


def main() -> int:
	if len(sys.argv) != 2:
		print("usage: neuralamp-wav-run-sample-rate-warning.py <nam_harness>", file=sys.stderr)
		return 2

	with tempfile.TemporaryDirectory(prefix="zene-nam-rate-") as temp_dir:
		root = Path(temp_dir)
		model_path = root / "model.nam"
		model_path.write_text(
			'{"architecture":"WaveNet","version":"0.5.0","sample_rate":48000,'
			'"config":{"layers":[{"input_size":1,"condition_size":1,"head_size":1,'
			'"head_bias":true,"channels":1,"dilations":[1],"kernel_size":1,'
			'"layer1x1":{"active":false}}]},"weights":[0,0,0,0,0,0,0]}',
			encoding="utf-8",
		)
		wav_path = root / "input.wav"
		with wave.open(str(wav_path), "wb") as wav:
			wav.setnchannels(1)
			wav.setsampwidth(2)
			wav.setframerate(44100)
			wav.writeframes(b"\x00\x00\x00\x00")

		result = subprocess.run(
			[sys.argv[1], str(model_path), "--wav-run", str(wav_path)],
			check=False,
			stdout=subprocess.PIPE,
			stderr=subprocess.STDOUT,
			text=True,
		)
		expected = "WARNING: model trained at 48000 Hz, harness running at 44100 Hz"
		if result.returncode != 0 or expected not in result.stdout:
			print(
				f"WAV-run sample-rate warning missing (exit={result.returncode}):\n{result.stdout}",
				file=sys.stderr,
			)
			return 1

	print("PASS: WAV-run warning reports the input WAV's 44100 Hz rate")
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
