#!/usr/bin/env python3
"""Fail when a hosted -Werror build would reject a discarded QTest::qWaitFor result.

Cluster ci-d1d791015ce10efd. Hosted `build / linux-arm64` (train 050/m0-train-41 at
ebf644847a1b) stopped at

    tests/src/core/UpdateCheckTest.cpp:394:108: error: ignoring return value of
    'bool QTest::qWaitFor(Functor, int)', declared with attribute 'nodiscard'
    [-Werror=unused-result]

Qt declares qWaitFor Q_REQUIRED_RESULT (Qt 5, qtestsupport_core.h:55) / [[nodiscard]]
(Qt 6, qtestsupport_core.h:78), and the hosted job builds with -DUSE_WERROR=ON. This box's
build tree carries `-Wno-error=unused-result` in CMAKE_CXX_FLAGS, which is why the defect
compiles locally.

The check is deterministic and independent of the host's relaxations: it takes the flags
compile_commands.json recorded for each translation unit in SOURCES, drops `-Werror` and
the unused-result relaxations, adds `-Werror=unused-result` and compiles the real source.
Two fixtures under the same flags keep it honest: a discarded qWaitFor must be REJECTED
(otherwise the check cannot see the defect and FAILS), a used one must COMPILE.

Usage:
  qwaitfor-result-check.py --build-dir <build> [--compiler <c++>]

Exit codes: 0 = every source compiles, 1 = a finding or a failed fixture, 2 = setup error.
"""

import argparse
import json
import os
import pathlib
import shlex
import subprocess
import sys
import tempfile

#! Translation units that call QTest::qWaitFor and are built on the hosted jobs.
SOURCES = ("tests/src/core/UpdateCheckTest.cpp",)

BAD = """#include <QtTest>
int main() { QTest::qWaitFor([] { return true; }, 1); return 0; }
"""

GOOD = """#include <QtTest>
int main() { return QTest::qWaitFor([] { return true; }, 1) ? 0 : 1; }
"""

OUTPUT_FLAGS = ("-o", "-MF", "-MT", "-MQ")
SOLO_FLAGS = ("-c", "-MD", "-MMD", "-MP")
RELAXATIONS = ("-Wno-error=unused-result", "-Wno-unused-result", "-Werror")


def recorded_arguments(build, source):
	"""The compile_commands.json arguments and absolute path for one translation unit."""
	path = build / "compile_commands.json"
	if not path.is_file():
		raise SystemExit(f"setup error: {path} is missing")
	for entry in json.loads(path.read_text(encoding="utf-8")):
		if pathlib.Path(entry["file"]).as_posix().endswith(source):
			arguments = entry.get("arguments") or shlex.split(entry["command"],
			                                                  posix=(os.name != "nt"))
			return list(arguments), pathlib.Path(entry["file"]).as_posix()
	return None, None


def compile_flags(arguments, source_path):
	"""The recorded command without its source, outputs and error relaxations."""
	flags = []
	skip = 0
	for argument in arguments:
		if skip:
			skip -= 1
		elif argument in OUTPUT_FLAGS:
			skip = 1
		elif argument != source_path and argument not in SOLO_FLAGS + RELAXATIONS:
			flags.append(argument)
	return flags


def compile_source(flags, source, output, work):
	command = [*flags, "-Werror=unused-result", "-c", str(source), "-o", str(output)]
	result = subprocess.run(command, cwd=work, text=True, stdout=subprocess.PIPE,
	                        stderr=subprocess.STDOUT, check=False)
	return result.returncode, result.stdout


def check_fixtures(flags, work):
	"""True when the flags reject a discarded result and accept a used one."""
	for name, text in (("bad", BAD), ("good", GOOD)):
		(work / f"{name}.cpp").write_text(text, encoding="utf-8")
	code, output = compile_source(flags, work / "bad.cpp", work / "bad.o", work)
	if code == 0:
		print("FAIL the discarded-qWaitFor fixture compiled: these flags cannot see the defect")
		return False
	print("PASS the discarded-qWaitFor fixture is rejected (the check can fail)")
	code, output = compile_source(flags, work / "good.cpp", work / "good.o", work)
	if code != 0:
		print("FAIL the checked-qWaitFor fixture was rejected (the flags are too strict):")
		print(output.strip())
		return False
	print("PASS the checked-qWaitFor fixture compiles")
	return True


def main():
	parser = argparse.ArgumentParser(description=__doc__,
	                                 formatter_class=argparse.RawDescriptionHelpFormatter)
	parser.add_argument("--build-dir", type=pathlib.Path, required=True)
	parser.add_argument("--compiler", default=None)
	args = parser.parse_args()
	build = args.build_dir.resolve()
	failures = 0
	fixtures_checked = False
	with tempfile.TemporaryDirectory(prefix="qwaitfor-result-", dir=build) as temp:
		work = pathlib.Path(temp)
		for source in SOURCES:
			recorded, source_path = recorded_arguments(build, source)
			if recorded is None:
				print(f"SKIP {source} is not built in this tree")
				continue
			flags = compile_flags(recorded, source_path)
			flags[0] = args.compiler or flags[0]
			if not fixtures_checked:
				if not check_fixtures(flags, work):
					return 1
				fixtures_checked = True
			code, output = compile_source(flags, source_path, work / "unit.o", work)
			if code != 0:
				print(f"FAIL {source} does not compile with -Werror=unused-result:")
				print(output.strip())
				failures += 1
			else:
				print(f"PASS {source} compiles with -Werror=unused-result")
	return 1 if failures else 0


if __name__ == "__main__":
	sys.exit(main())
