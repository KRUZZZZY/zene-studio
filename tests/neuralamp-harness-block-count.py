"""Regression for oversized generated-signal block counts in nam_harness."""

import json
import subprocess
import sys
import tempfile
from pathlib import Path


def run_harness(harness, model_path, *args):
	return subprocess.run(
		[harness, str(model_path), *args],
		capture_output=True,
		text=True,
		check=False,
	)


def main():
	harness = sys.argv[1]
	model = {
		"architecture": "WaveNet",
		"version": "0.5.0",
		"config": {
			"layers": [{
				"input_size": 1,
				"condition_size": 1,
				"head_size": 1,
				"head_bias": True,
				"channels": 1,
				"bottleneck": 1,
				"dilations": [1],
				"kernel_size": 1,
			}],
		},
		"weights": [1.0, 1.0, 0.0, 0.0, 1.0, 0.0, 1.0, 0.0, 1.0],
	}
	with tempfile.TemporaryDirectory(prefix="zene-nam-block-count-") as temp_dir:
		model_path = Path(temp_dir) / "minimal-valid.nam"
		model_path.write_text(json.dumps(model), encoding="utf-8")

		# This small generated run proves the fixture loads and processes before
		# the oversized count is exercised through that same code path. The tiny
		# model can trip quality thresholds, so require its load and output metrics.
		smoke = run_harness(
			harness, model_path, "--block", "64", "--blocks", "1", "--warmup", "0")
		smoke_output = smoke.stdout + smoke.stderr
		if (smoke.returncode not in (0, 1) or
				"architecture: WaveNet" not in smoke.stdout or
				"block size: 64  blocks: 1" not in smoke.stdout or
				"finite=1 nonzero=1" not in smoke.stdout):
			sys.stderr.write("FAIL: minimal NAM fixture did not complete a generated run\n")
			sys.stderr.write(smoke_output)
			return 1

		result = run_harness(harness, model_path, "--blocks", "2147483647")
		output = result.stdout + result.stderr
		expected = "error: generated run exceeds 4194304 samples"
		if result.returncode != 2 or expected not in output:
			sys.stderr.write("FAIL: oversized block count was not rejected before allocation\n")
			sys.stderr.write(output)
			return 1
	print("PASS: oversized generated run rejected with a valid NAM model")
	return 0


if __name__ == "__main__":
	if len(sys.argv) != 2:
		sys.stderr.write("usage: neuralamp-harness-block-count.py <nam_harness>\n")
		raise SystemExit(2)
	raise SystemExit(main())
