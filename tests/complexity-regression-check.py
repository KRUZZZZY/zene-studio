#!/usr/bin/env python3
"""Fail when a function the ci-6d61162ce0abfd39 cluster brought under CCN 10 drifts back over it.

Cluster ci-6d61162ce0abfd39 (2026-11-10). Hosted CI failed Gate 4 - the per-method complexity
ratchet (CCN <= 10) - at a1a98b67b2 with eight regressions, reproduced locally with CI's own
lizard (1.25.1). The repair is a behaviour-preserving extraction in each function; the only
observable change IS the complexity, so that is what this check holds in place.

The check measures the repaired functions with lizard and asserts each is at or under the target.
It runs its own red/green fixtures first (a 12-branch function must be flagged, a one-liner must
not), so a scanner that cannot fail fails here instead. No build, no clock, no I/O beyond
reading the sources and running lizard.

Exit 0 = every function at or under the target, 1 = a finding (or a failed fixture),
77 = lizard is not installed (the host cannot run the check at all).
"""

import csv
import io
import os
import subprocess
import sys
import tempfile

CCN_LIMIT = 10

# (repo-relative file, lizard's name for the function). Also holds the train-29
# mousePressEvent regression: empty-editor activation must not grow the input dispatcher.
TARGETS = [
	("src/wasm/WasmSandboxModules.cpp", "lmms::wasm::WasmSandbox::loadModuleBytes"),
	("tests/src/gui/XyPadPairTest.cpp", "dotCentreAlong"),
	("src/core/DawProjectReadTracks.cpp", "lmms::interchange::readdetail::parseNotes"),
	("src/core/ControlCommandsMixerStrip.cpp", "lmms::setStrip"),
	("src/core/ControlCommandsMixerTools.cpp", "lmms::adjust"),
	("src/core/ControlCommandsSessionRecordLand.cpp", "lmms::sessionrecord::landPairState"),
	("tests/stem_commands_lib.py", "wav_facts"),
	("src/core/VideoAudioExtract.cpp", "lmms::extract"),
	("src/gui/noteeditor/NoteCanvasInput.cpp", "lmms::gui::NoteCanvas::mousePressEvent"),
]


def lizard_functions(path):
	"""{function name: CCN} for one file, or SystemExit when lizard cannot read it."""
	proc = subprocess.run([sys.executable, "-m", "lizard", "--csv", path],
		capture_output=True, text=True)
	if proc.returncode != 0:
		raise SystemExit(f"lizard failed on {path}: {proc.stderr.strip()}")
	functions = {}
	for row in csv.reader(io.StringIO(proc.stdout)):
		if len(row) >= 8 and row[0].isdigit():
			functions[row[7]] = int(row[1])
	return functions


def fixtures():
	"""A scanner that cannot fail must fail: a 12-branch function is over, a one-liner is not."""
	with tempfile.TemporaryDirectory() as tmp:
		red = os.path.join(tmp, "red.cpp")
		with open(red, "w") as handle:
			handle.write("int red(int a)\n{\n\tint n = 0;\n")
			for i in range(1, 13):
				handle.write(f"\tif (a > {i}) {{ n += {i}; }}\n")
			handle.write("\treturn n;\n}\n")
		green = os.path.join(tmp, "green.cpp")
		with open(green, "w") as handle:
			handle.write("int green(int a)\n{\n\treturn a + 1;\n}\n")
		red_ccn = lizard_functions(red).get("red")
		green_ccn = lizard_functions(green).get("green")
	if red_ccn is None or red_ccn <= CCN_LIMIT:
		print(f"FIXTURE FAILED: 'red' measured {red_ccn}, expected over {CCN_LIMIT}")
		return False
	if green_ccn is None or green_ccn > CCN_LIMIT:
		print(f"FIXTURE FAILED: 'green' measured {green_ccn}, expected at most {CCN_LIMIT}")
		return False
	return True


def main():
	root = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.dirname(
		os.path.abspath(__file__)))
	try:
		import lizard  # noqa: F401
	except ImportError:
		print("SKIP: lizard is not installed (pip install lizard) - cannot measure complexity")
		return 77
	if not fixtures():
		return 1
	findings = []
	for path, function in TARGETS:
		full = os.path.join(root, path)
		if not os.path.isfile(full):
			findings.append(f"{function}@{path}: file is missing")
			continue
		measured = lizard_functions(full).get(function)
		if measured is None:
			findings.append(f"{function}@{path}: lizard does not report this function")
		elif measured > CCN_LIMIT:
			findings.append(f"{function}@{path}: CCN {measured} > {CCN_LIMIT}")
	if findings:
		print(f"complexity regression (cluster ci-6d61162ce0abfd39): "
			f"a function this cluster brought under CCN {CCN_LIMIT} is over it again")
		for line in findings:
			print(f"  {line}")
		return 1
	print(f"complexity-regression-check: {len(TARGETS)} functions at or under CCN {CCN_LIMIT}")
	return 0


if __name__ == "__main__":
	sys.exit(main())
