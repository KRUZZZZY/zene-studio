#!/usr/bin/env python3
"""ci-d14f7617bae6160d: the VST2 test instrument must keep the C99 snprintf declaration
visible on Darwin.

The hosted macos-x86_64 build (run 42805458633, job 129793314406) failed at
tests/data/vst2-test-instrument/vst2-test-instrument.c:173:2 with

    error: call to undeclared library function 'snprintf' ...
    [-Wimplicit-function-declaration]

Cause. The fixture defined _POSIX_C_SOURCE 199309L before any system header. Darwin's
<sys/cdefs.h> maps that to __DARWIN_C_LEVEL = 199309L, and Darwin's <_stdio.h> declares
snprintf only when

    __DARWIN_C_LEVEL >= 200112L || defined(_C99_SOURCE) || defined(__cplusplus)

so the C99 prototype was hidden and Apple clang rejected the call. nanosleep (POSIX.1b,
199309L) stayed visible, which is why the error surfaced at the first snprintf, not at
stall().

This host has no macOS SDK, so the check takes the fixture's real feature-test
preamble (before its first system header) and preprocesses it twice with an assertion.
That keeps the check independent of platform headers while still detecting a change to
the feature-test macro:

  * Darwin simulation: removes the host's _WIN32 macro, defines __APPLE__, and asserts
    __DARWIN_C_LEVEL >= 200112L. The rule is reproduced from Apple's <sys/cdefs.h>.
  * Native platform: asserts _POSIX_C_SOURCE >= 199309L on non-Apple POSIX, so the
    nanosleep path remains guarded there too.

GNU-style compilers preprocess stdin. An MSVC-style frontend uses /EP on a temporary
source file because cl.exe does not accept the GNU command-line options or stdin input.

Exit 0 = both probes compile; exit 1 = a probe failed (compiler output on stdout).
"""

import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
FIXTURE = os.path.join(HERE, "vst2-test-instrument.c")

DARWIN_PROBE = """
/*
 * ci-d14f7617bae6160d probe, appended by darwin-feature-level-check.py.
 * On Darwin the SDK's <sys/cdefs.h> already defined __DARWIN_C_LEVEL; elsewhere the
 * rule below is Apple's, verbatim.
 */
#ifndef __DARWIN_C_LEVEL
#if defined(_POSIX_C_SOURCE) && !defined(_DARWIN_C_SOURCE) && !defined(_NONSTD_SOURCE)
#define __DARWIN_C_LEVEL _POSIX_C_SOURCE
#else
#define __DARWIN_C_LEVEL 900000L
#endif
#endif
#if defined(__APPLE__) && __DARWIN_C_LEVEL < 200112L
#error "vst2-test-instrument: the Darwin feature level hides the C99 snprintf declaration (ci-d14f7617bae6160d)"
#endif
"""

POSIX_PROBE = """
/*
 * ci-d14f7617bae6160d probe, appended by darwin-feature-level-check.py.
 * The non-Apple POSIX path needs nanosleep, which is POSIX.1b (199309L).
 */
#if !defined(__APPLE__) && !defined(_WIN32)
#if !defined(_POSIX_C_SOURCE) || _POSIX_C_SOURCE < 199309L
#error "vst2-test-instrument: non-Apple POSIX needs _POSIX_C_SOURCE >= 199309L for nanosleep (ci-d14f7617bae6160d)"
#endif
#endif
"""

DARWIN_SIMULATION = """
/* Simulate Darwin even when the host compiler predefines its native platform. */
#undef _WIN32
#ifndef __APPLE__
#define __APPLE__ 1
#endif
"""


def compile_probe(compiler, compiler_id, frontend, source, label):
	"""Preprocess source; return True when the probe is accepted."""
	msvc_frontend = frontend.upper() == "MSVC" or compiler_id.upper() == "MSVC"
	if msvc_frontend:
		with tempfile.NamedTemporaryFile("w", suffix=".c", delete=False) as handle:
			handle.write(source)
			path = handle.name
		cmd = [compiler, "/nologo", "/EP", "/TC", path]
		try:
			proc = subprocess.run(cmd, text=True, capture_output=True)
		finally:
			os.unlink(path)
	else:
		cmd = [compiler, "-fsyntax-only", "-x", "c", "-"]
		proc = subprocess.run(cmd, input=source, text=True, capture_output=True)
	if proc.returncode != 0:
		sys.stdout.write("FAIL %s\n%s\n%s%s\n" % (label, " ".join(cmd), proc.stdout, proc.stderr))
		return False
	sys.stdout.write("PASS %s\n" % label)
	return True


def main(argv):
	compiler = argv[1] if len(argv) > 1 else os.environ.get("CC", "cc")
	compiler_id = argv[2] if len(argv) > 2 else ""
	frontend = argv[3] if len(argv) > 3 else ""
	with open(FIXTURE, "r") as handle:
		fixture = handle.read()
	include_marker = "\n#include <stdio.h>"
	if include_marker not in fixture:
		sys.stdout.write("FAIL feature-preamble: fixture header boundary not found\n")
		return 1
	preamble = fixture.split(include_marker, 1)[0]
	ok = compile_probe(compiler, compiler_id, frontend,
		DARWIN_SIMULATION + preamble + DARWIN_PROBE, "darwin-feature-level")
	ok = compile_probe(compiler, compiler_id, frontend,
		preamble + POSIX_PROBE, "posix-feature-level") and ok
	return 0 if ok else 1


if __name__ == "__main__":
	sys.exit(main(sys.argv))
