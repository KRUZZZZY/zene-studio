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


def write_fixture(directory):
	"""Write a minimal valid WaveNet model the harness can load."""
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
	path = Path(directory) / "minimal-valid.nam"
	path.write_text(json.dumps(model), encoding="utf-8")
	return path


def rejection_failure(harness, model_path, args, expected):
	"""Return why the run was not rejected cleanly, or None when it was."""
	result = run_harness(harness, model_path, *args)
	output = result.stdout + result.stderr
	if result.returncode != 2:
		return "args={} exit={} (expected 2): {}".format(
			" ".join(args), result.returncode, output)
	if "terminate called" in output:
		return "args={} aborted instead of reporting a usage error: {}".format(
			" ".join(args), output)
	if expected not in output:
		return "args={} did not report {!r}: {}".format(" ".join(args), expected, output)
	return None


def main():
	harness = sys.argv[1]
	cases = [
		# The reported case: the block total overflowed a 32-bit addition and
		# asked for a huge allocation.
		(["--blocks", "2147483647"], "generated run exceeds 4194304 samples"),
		# Both counts at INT_MAX: the sum alone overflows the narrow type.
		(["--blocks", "2147483647", "--warmup", "2147483647"],
			"generated run exceeds 4194304 samples"),
		# A count that no longer fits the narrow type must be refused, never wrapped.
		(["--blocks", "2147483648"], "generated run exceeds 4194304 samples"),
		# Counts that are not numbers at all must be refused as usage errors.
		(["--blocks", "99999999999999999999"], "--blocks must be an integer"),
		(["--blocks", "not-a-number"], "--blocks must be an integer"),
		(["--warmup", "99999999999999999999"], "--warmup must be an integer"),
		(["--block", "2147483648"], "--block must be an integer"),
		# Counts that are numbers but out of range keep their own message.
		(["--blocks", "0"], "--blocks must be >= 1"),
		(["--warmup", "-1"], "--warmup must be >= 0"),
	]
	with tempfile.TemporaryDirectory(prefix="zene-nam-block-count-") as temp_dir:
		model_path = write_fixture(temp_dir)

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

		for args, expected in cases:
			failure = rejection_failure(harness, model_path, args, expected)
			if failure is not None:
				sys.stderr.write("FAIL: {}\n".format(failure))
				return 1
	print("PASS: oversized generated run rejected with a valid NAM model")
	return 0


if __name__ == "__main__":
	if len(sys.argv) != 2:
		sys.stderr.write("usage: neuralamp-harness-block-count.py <nam_harness>\n")
		raise SystemExit(2)
	raise SystemExit(main())
