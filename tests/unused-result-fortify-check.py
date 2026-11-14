#!/usr/bin/env python3
"""Fail when a hosted Linux build would reject tests/src/core/MountNamespaceTestSupport.h.

Cluster ci-aa184f959f204bcd. The hosted `build / linux-x86_64` job stopped at

    tests/src/core/MountNamespaceTestSupport.h:110:22: error: ignoring return value of
    'ssize_t write(int, const void*, size_t)' declared with attribute
    'warn_unused_result' [-Werror=unused-result]

and the same diagnostic at line 78. The two `(void)::write(...)` calls in the header are
not a use of the write: glibc's `write` is declared `__wur` (/usr/include/unistd.h:378),
which is `__attribute__((warn_unused_result))` exactly when fortification is on
(/usr/include/sys/cdefs.h:578-581). Ubuntu 24.04's gcc defines `_FORTIFY_SOURCE=3` by
default at -O2, the job configures -DUSE_WERROR=ON (cmake/modules/ErrorFlags.cmake:42-46)
and RelWithDebInfo (-O2), and a `(void)` cast does not silence warn_unused_result - only
using the result does. This box's build tree carries `-Wno-error=unused-result` in
CMAKE_CXX_FLAGS, which is why the defect compiles locally.

The check is deterministic and independent of the host's own hardening: it takes the
standard, defines and include paths from this build's compile_commands.json entry for
the one translation unit that includes the header (RecordToolsTest.cpp), drops that
build's `-Wno-error=unused-result` / `-Werror` / fortify defines, and compiles a probe
against the REAL header with
`-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3 -Werror=unused-result`.

Two fixtures keep it honest under the same flags:
  * a discarded `write` result must be REJECTED - if it compiles, the check cannot see
    the defect at all and FAILS rather than reporting a green run that proves nothing;
  * a used `write` result must COMPILE - otherwise the flags are too strict and the
    header verdict would be meaningless.
The header's namespace helpers exist only under `#ifdef __linux__`, so a non-Linux host
prints SKIP and exits 0: there is no write call there to check.

Usage:
  unused-result-fortify-check.py --build-dir <build> [--compiler <c++>]

Exit codes: 0 = the header compiles under hosted hardening (or the host is not Linux:
SKIP), 1 = a finding or a failed fixture, 2 = usage/setup error.
"""

import argparse
import json
import os
import pathlib
import shlex
import subprocess
import sys
import tempfile

HEADER = "tests/src/core/MountNamespaceTestSupport.h"
AFFECTED = "tests/src/core/RecordToolsTest.cpp"

#! Compiled, never executed: main CALLS every function of the header - including the two
#! namespace helpers whose write calls this cluster's defect is about - so each body is
#! compiled rather than discarded unused. An inline function nobody calls is not
#! gimplified, and -Wunused-result is emitted from the gimplified body: a probe that only
#! declared the calls would miss the very defect it exists to catch.
PROBE = """#include "MountNamespaceTestSupport.h"

int main()
{
	const bool entered = lmms::testsupport::enterOwnMountNamespace();
	const bool bound = lmms::testsupport::bindMountForTest(QStringLiteral("/a"),
	                                                       QStringLiteral("/b"));
	lmms::testsupport::unbindMountForTest(QStringLiteral("/b"));
	const bool mapped = lmms::testsupport::mapOwnUidIntoNamespace(0, -1, -1);
	const bool device = lmms::testsupport::filesystemDevice(QStringLiteral("/")) != 0;
	return (entered && bound && mapped && device) ? 0 : 1;
}
"""

BAD = """#include <unistd.h>

int main()
{
	char byte = 1;
	(void)::write(1, &byte, 1);
	return 0;
}
"""

GOOD = """#include <unistd.h>

int main()
{
	char byte = 1;
	return ::write(1, &byte, 1) == 1 ? 0 : 1;
}
"""

#! Arguments that decide where output goes or what the recorded build suppresses; the
#! check replaces them with its own. A pair of these consumes the next argument.
OUTPUT_FLAGS = ("-o", "-MF", "-MT", "-MQ")
SOLO_FLAGS = ("-c", "-MD", "-MMD", "-MP")
#! The local build's own relaxations and hardening defines: the hosted job has neither
#! the relaxation nor an explicit fortify define (gcc supplies level 3 at -O2 there).
RELAXATIONS = ("-Wno-error=unused-result", "-Wno-unused-result", "-Werror")
FORTIFY = ("-U_FORTIFY_SOURCE", "-D_FORTIFY_SOURCE=3")


def split_command(command):
	"""A recorded command line as an argument list."""
	return shlex.split(command, posix=(os.name != "nt"))


def recorded_arguments(build, source):
	"""The compile_commands.json arguments for one translation unit, minus the source."""
	path = build / "compile_commands.json"
	if not path.is_file():
		raise SystemExit(f"setup error: {path} is missing (configure with "
		                 "CMAKE_EXPORT_COMPILE_COMMANDS=ON)")
	entries = json.loads(path.read_text(encoding="utf-8"))
	for entry in entries:
		if pathlib.Path(entry["file"]).as_posix().endswith(source):
			arguments = entry.get("arguments") or split_command(entry["command"])
			return list(arguments), pathlib.Path(entry["file"]).as_posix()
	raise SystemExit(f"setup error: {source} is not in {path}")


def compile_only_arguments(arguments, source_path):
	"""The recorded command as reusable compile flags (no source, no -o, no -Werror)."""
	flags = [arguments[0]]
	index = 1
	while index < len(arguments):
		argument = arguments[index]
		if argument == source_path:
			index += 1
			continue
		if argument in OUTPUT_FLAGS:
			index += 2
			continue
		if argument in SOLO_FLAGS or argument in RELAXATIONS or \
		   argument.startswith(("-D_FORTIFY_SOURCE", "-U_FORTIFY_SOURCE")):
			index += 1
			continue
		flags.append(argument)
		index += 1
	return flags


def compile_probe(flags, source, output, work):
	command = [flags[0], *flags[1:], *FORTIFY, "-Werror=unused-result", "-c",
	           str(source), "-o", str(output)]
	result = subprocess.run(command, cwd=work, text=True, stdout=subprocess.PIPE,
	                        stderr=subprocess.STDOUT, check=False)
	return result.returncode, result.stdout


def diagnose(label, output):
	print(f"FAIL {label}:")
	print(output.strip() or "the compiler produced no diagnostic")


def main():
	parser = argparse.ArgumentParser(description=__doc__,
	                                 formatter_class=argparse.RawDescriptionHelpFormatter)
	parser.add_argument("--build-dir", type=pathlib.Path, required=True)
	parser.add_argument("--compiler", default=os.environ.get("CXX") or "c++")
	args = parser.parse_args()
	if not sys.platform.startswith("linux"):
		print("SKIP the header's namespace helpers are Linux-only (__linux__); "
		      "there is no write call to harden on this host")
		return 0
	build = args.build_dir.resolve()
	root = pathlib.Path(__file__).resolve().parents[1]
	header = root / HEADER
	if not header.is_file():
		print(f"setup error: {header} is missing")
		return 2

	recorded, source_path = recorded_arguments(build, AFFECTED)
	flags = compile_only_arguments(recorded, source_path)
	flags[0] = args.compiler
	# The header is included by name from its own directory by the test source; the probe
	# is written elsewhere, so name that directory explicitly.
	flags += ["-I", str(root / "tests/src/core")]

	with tempfile.TemporaryDirectory(prefix="unused-result-fortify-") as temp:
		work = pathlib.Path(temp)
		fixtures = {"bad": BAD, "good": GOOD}
		for name, text in fixtures.items():
			(work / f"{name}.cpp").write_text(text, encoding="utf-8")

		# 1. Can these flags see the defect at all? On a libc whose write carries no
		# __wur under fortification this check cannot fail, so it must not pretend.
		code, output = compile_probe(flags, work / "bad.cpp", work / "bad.o", work)
		if code == 0:
			diagnose("the discarded-write fixture compiled: these flags cannot see the "
			         "defect, so a green header verdict here would prove nothing", output)
			return 1
		print("PASS the discarded-write fixture is rejected (the check can fail)")

		# 2. Are the flags fair? A used result must compile.
		code, output = compile_probe(flags, work / "good.cpp", work / "good.o", work)
		if code != 0:
			diagnose("the checked-write fixture was rejected (the flags are too strict)", output)
			return 1
		print("PASS the checked-write fixture compiles")

		# 3. The real header, through the affected translation unit's own flags.
		(work / "probe.cpp").write_text(PROBE, encoding="utf-8")
		code, output = compile_probe(flags, work / "probe.cpp", work / "probe.o", work)
		if code != 0:
			diagnose(f"{HEADER} does not compile under hosted hardening "
			         "(-D_FORTIFY_SOURCE=3 -Werror=unused-result)", output)
			return 1
		print(f"PASS {HEADER} compiles under hosted hardening")

	print("RESULT: every write result in the header is used, not cast away")
	return 0


if __name__ == "__main__":
	sys.exit(main())
