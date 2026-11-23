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

Cluster ci-f561a7d751228cfe. The same hosted `build / linux-arm64` job (train
050/m0-train-49 at 21c2f3ee2e0d) stopped at

    tests/src/gui/ShortcutsPageTest.cpp:58:45: error: ignoring return value of
    'bool QTest::qWaitForWindowExposed(QWidget*, int)', declared with attribute 'nodiscard'
    [-Werror=unused-result]

A sibling of the qWaitFor defect: the whole QTest::qWaitFor* family is nodiscard. The check now
also compiles the GUI shortcuts tests, carries a qWaitForWindowExposed fixture pair, and runs a
cheap textual sweep over tests/src for any statement that starts with QTest::qWaitFor*( and so
discards the bool result, which catches future siblings without compiling every unit.

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
import re
import tempfile

#! Translation units that call the QTest::qWaitFor* family and are built on the hosted jobs.
SOURCES = (
	"tests/src/core/UpdateCheckTest.cpp",
	"tests/src/gui/ShortcutsPageTest.cpp",
	"tests/src/gui/ShortcutsCommandsTest.cpp",
)

#! (label, discarded result source, used result source); compiled only, never run.
FIXTURES = (
	("qWaitFor",
	 """#include <QtTest>
int main() { QTest::qWaitFor([] { return true; }, 1); return 0; }
""",
	 """#include <QtTest>
int main() { return QTest::qWaitFor([] { return true; }, 1) ? 0 : 1; }
"""),
	("qWaitForWindowExposed",
	 """#include <QtTest>
#include <QWidget>
int main() { QWidget* w = nullptr; QTest::qWaitForWindowExposed(w, 1); return 0; }
""",
	 """#include <QtTest>
#include <QWidget>
int main() { QWidget* w = nullptr; return QTest::qWaitForWindowExposed(w, 1) ? 0 : 1; }
"""),
)

#! Directory swept for statements that discard a QTest::qWaitFor* result.
SWEEP_DIR = "tests/src"
SWEEP_SUFFIXES = (".cpp", ".h")
DISCARDED_CALL = re.compile(r"^\s*QTest::qWaitFor\w*\s*\(")

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


def check_fixture(flags, work, label, bad, good):
	"""True when the flags reject a discarded `label` result and accept a used one."""
	(work / f"{label}-bad.cpp").write_text(bad, encoding="utf-8")
	(work / f"{label}-good.cpp").write_text(good, encoding="utf-8")
	code, output = compile_source(flags, work / f"{label}-bad.cpp", work / "bad.o", work)
	if code == 0:
		print(f"FAIL the discarded-{label} fixture compiled: these flags cannot see the defect")
		return False
	print(f"PASS the discarded-{label} fixture is rejected (the check can fail)")
	code, output = compile_source(flags, work / f"{label}-good.cpp", work / "good.o", work)
	if code != 0:
		print(f"FAIL the checked-{label} fixture was rejected (the flags are too strict):")
		print(output.strip())
		return False
	print(f"PASS the checked-{label} fixture compiles")
	return True


def check_fixtures(flags, work):
	"""True when every fixture pair behaves: discarded rejected, used accepted."""
	return all([check_fixture(flags, work, *fixture) for fixture in FIXTURES])


def sweep_discarded_results(root):
	"""Count statements under tests/src that start with QTest::qWaitFor*( and drop the result."""
	failures = 0
	base = root / SWEEP_DIR
	for path in sorted(base.rglob("*")):
		if not path.is_file() or path.suffix not in SWEEP_SUFFIXES:
			continue
		lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
		for number, line in enumerate(lines, 1):
			if DISCARDED_CALL.match(line):
				print(f"FAIL {path.relative_to(root).as_posix()}:{number} "
				      "discards a QTest::qWaitFor* result")
				failures += 1
	if not failures:
		print(f"PASS no {SWEEP_DIR} statement discards a QTest::qWaitFor* result")
	return failures


def check_source(flags, source, source_path, work):
	"""True when one translation unit compiles under -Werror=unused-result."""
	code, output = compile_source(flags, source_path, work / "unit.o", work)
	if code != 0:
		print(f"FAIL {source} does not compile with -Werror=unused-result:")
		print(output.strip())
		return False
	print(f"PASS {source} compiles with -Werror=unused-result")
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
			if not check_source(flags, source, source_path, work):
				failures += 1
	failures += sweep_discarded_results(pathlib.Path(__file__).resolve().parent.parent)
	return 1 if failures else 0


if __name__ == "__main__":
	sys.exit(main())
