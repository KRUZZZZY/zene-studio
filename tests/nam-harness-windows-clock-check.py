#!/usr/bin/env python3
"""Fail when the NAM harness asks its own translation unit for a POSIX thread clock.

Cluster ci-443a2931bcb5dacc (2026-11-14). The hosted `build / windows-arm64` job stopped at

    [2690/6489] Linking CXX executable plugins\\nam_harness.exe
    FAILED: [code=1] plugins/nam_harness.exe
    ld.lld: error: undefined symbol: clock_gettime64
    >>> referenced by C:/a/_temp/msys64/clangarm64/include/pthread_time.h:102
    >>>               plugins/NeuralAmp/tests/CMakeFiles/nam_harness.dir/nam_harness.cpp.obj:(main)

MSYS2's mingw-w64 <time.h> includes <pthread_time.h> (the POSIX 2008 contract), and the
current winpthreads header defines clock_gettime() as a static inline that calls
clock_gettime64(). The clangarm64 driver's link line does not name libwinpthread, so the
object's reference is undefined. GCC's mingw64 driver links winpthread, which is why the same
source linked on mingw64 and only the clang-arm64 job failed.

The contract: nam_harness.cpp must not reference a platform clock itself. It calls
lmms::nam::threadCpuTimeUs() from HarnessClock.h; plugins/NeuralAmp/tests/CMakeLists.txt
selects HarnessClockPosix.cpp (clock_gettime) or HarnessClockWindows.cpp (GetThreadTimes).

This check has four deterministic halves, each reading sources or running compiled code:

  1. Two symbol fixtures prove the detector: a translation unit that calls clock_gettime
     must be SEEN, one that does not must stay unseen.
  2. The real nam_harness.cpp is recompiled with the build's own recorded compile command
     (build/compile_commands.json) and its undefined symbols are inspected. A POSIX clock
     there is the hosted failure on every host that provides the symbol, so this is red on
     Linux before the fix and green after it.
  3. HarnessClockPosix.cpp is compiled and run: the value must be finite, non-negative and
     non-decreasing across two immediate calls (no wall clock, no sleeps).
  4. HarnessClockWindows.cpp is compiled against a model of the Microsoft declarations it
     uses (the hosted Windows job compiles it natively) and run with a stub GetThreadTimes:
     kernel+user 100 ns ticks must become microseconds, and a failed call must yield 0.

Usage: nam-harness-windows-clock-check.py --source-root <tree> --build-dir <build>
Exit codes: 0 = clean, 1 = finding, 2 = usage/environment error.
"""

import argparse
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile

HARNESS = "plugins/NeuralAmp/tests/nam_harness.cpp"
WINDOWS_CLOCK = "plugins/NeuralAmp/tests/HarnessClockWindows.cpp"
POSIX_CLOCK = "plugins/NeuralAmp/tests/HarnessClockPosix.cpp"
TEST_NAME = "NamHarnessWindowsClockCheck"

# clock_gettime plus the 32/64-bit spellings winpthreads uses on Windows.
CLOCK_SYMBOL = re.compile(r"clock_gettime(?:32|64)?$")
UNDEFINED = re.compile(r"^\s+U\s+(\S+)")
VERSIONED = re.compile(r"@")


def run(command, cwd, timeout=300):
	return subprocess.run(command, cwd=cwd, capture_output=True, text=True, timeout=timeout)


def split_command(entry):
	if entry.get("command"):
		return shlex.split(entry["command"])
	return list(entry.get("arguments") or [])


def source_of(entry):
	path = entry["file"]
	if not os.path.isabs(path):
		path = os.path.join(entry["directory"], path)
	return os.path.normpath(path)


def find_entry(build_dir, source_root, relative):
	path = os.path.join(os.path.abspath(build_dir), "compile_commands.json")
	if not os.path.isfile(path):
		return None, "no compile_commands.json in " + os.path.abspath(build_dir)
	with open(path, encoding="utf-8") as stream:
		entries = json.load(stream)
	wanted = os.path.normpath(os.path.join(os.path.abspath(source_root), relative))
	for entry in entries:
		if source_of(entry) == wanted:
			return entry, ""
	return None, relative + " is not in compile_commands.json (was it renamed?)"


def compiler_of(entry):
	command = split_command(entry)
	while command and os.path.basename(command[0]) in ("ccache", "sccache", "distcc"):
		command = command[1:]
	if not command:
		return None
	return command[0]


def object_command(entry, output):
	"""The recorded compile line, retargeted at `output` and without dependency files."""
	cleaned = []
	skip = False
	for argument in split_command(entry):
		if skip:
			skip = False
			continue
		if argument in ("-o", "-MF", "-MT", "-MQ"):
			skip = True
			continue
		if argument.startswith(("-o", "-MF", "-MT", "-MQ")):
			continue
		if argument in ("-c", "-MD", "-MMD", "-MP"):
			continue
		cleaned.append(argument)
	while cleaned and os.path.basename(cleaned[0]) in ("ccache", "sccache", "distcc"):
		cleaned = cleaned[1:]
	if not cleaned:
		return None
	cleaned.extend(["-c", "-o", output])
	return cleaned


def undefined_symbols(path):
	result = run(["nm", "-u", path], os.path.dirname(path) or ".")
	if result.returncode != 0:
		return None, (result.stderr or result.stdout).strip()
	names = set()
	for line in (result.stdout or "").splitlines():
		match = UNDEFINED.match(line)
		if match:
			names.add(VERSIONED.sub("", match.group(1)))
	return names, ""


def clock_symbols(names):
	return sorted(name for name in names
		if CLOCK_SYMBOL.search(name.lstrip("_")))


def compile_object(command, cwd):
	result = run(command, cwd)
	return result, (result.stderr or result.stdout).strip()


FAKE_WINDOWS_H = """\
#ifndef ZENE_PROBE_WINDOWS_H
#define ZENE_PROBE_WINDOWS_H
/* The declarations HarnessClockWindows.cpp uses, in the shape <windows.h> gives them. */
typedef unsigned long DWORD;
typedef struct _FILETIME
{
	DWORD dwLowDateTime;
	DWORD dwHighDateTime;
} FILETIME;
#define ZENE_PROBE_WINDOWS_H 1
extern "C"
{
	void* GetCurrentThread(void);
	int GetThreadTimes(void* thread, FILETIME* creation, FILETIME* exitTime,
		FILETIME* kernel, FILETIME* user);
}
#endif
"""

WINDOWS_PROBE = """\
#include "HarnessClock.h"

#include <cstdio>
#include <windows.h>

#ifndef ZENE_PROBE_WINDOWS_H
#error "the probe must compile against the model of <windows.h>"
#endif

namespace
{
unsigned long long gKernelTicks = 1000;
unsigned long long gUserTicks = 500;
bool gFail = false;

void splitTicks(unsigned long long ticks, FILETIME& time)
{
	time.dwLowDateTime = static_cast<DWORD>(ticks & 0xffffffffull);
	time.dwHighDateTime = static_cast<DWORD>(ticks >> 32);
}
}

extern "C" void* GetCurrentThread(void) { return nullptr; }

extern "C" int GetThreadTimes(void*, FILETIME*, FILETIME*, FILETIME* kernel, FILETIME* user)
{
	if (gFail) { return 0; }
	splitTicks(gKernelTicks, *kernel);
	splitTicks(gUserTicks, *user);
	return 1;
}

int main()
{
	const double ok = lmms::nam::threadCpuTimeUs();
	if (ok != 150.0)
	{
		std::printf("FAIL: 1000+500 FILETIME ticks must be 150 us, got %.3f\\n", ok);
		return 1;
	}
	gFail = true;
	const double failed = lmms::nam::threadCpuTimeUs();
	if (failed != 0.0)
	{
		std::printf("FAIL: a failed GetThreadTimes must yield 0 us, got %.3f\\n", failed);
		return 1;
	}
	std::printf("PASS: Win32 clock: kernel+user 100 ns ticks are microseconds; failure yields 0\\n");
	return 0;
}
"""

POSIX_PROBE = """\
#include "HarnessClock.h"

#include <cmath>
#include <cstdio>

int main()
{
	const double first = lmms::nam::threadCpuTimeUs();
	const double second = lmms::nam::threadCpuTimeUs();
	if (!std::isfinite(first) || first < 0.0 || !std::isfinite(second) || second < first)
	{
		std::printf("FAIL: thread CPU time must be finite, non-negative and not go backwards"
			" (%.3f, %.3f)\\n", first, second);
		return 1;
	}
	std::printf("PASS: POSIX clock: finite, non-negative, non-decreasing thread CPU time\\n");
	return 0;
}
"""

CLOCK_FIXTURE = """\
#include <ctime>
int main()
{
	timespec now{};
	return clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now);
}
"""

PLAIN_FIXTURE = """\
double threadCpuTimeUs();
int main() { return threadCpuTimeUs() < 0.0; }
"""


def self_test(cxx):
	"""Prove the undefined-symbol detector can see the defect and leave a clean TU alone."""
	with tempfile.TemporaryDirectory(prefix="nam-clock-selftest-") as scratch:
		seen = []
		for name, text in (("red", CLOCK_FIXTURE), ("green", PLAIN_FIXTURE)):
			source = os.path.join(scratch, name + ".cpp")
			output = os.path.join(scratch, name + ".o")
			with open(source, "w", encoding="utf-8") as stream:
				stream.write(text)
			result, detail = compile_object(
				[cxx, "-std=c++20", "-c", source, "-o", output], scratch)
			if result.returncode != 0:
				print("FAIL: the " + name + " fixture did not compile:\n" + detail)
				return False
			names, error = undefined_symbols(output)
			if names is None:
				print("FAIL: nm could not read the " + name + " fixture: " + error)
				return False
			seen.append(clock_symbols(names))
		if not seen[0]:
			print("FAIL: the detector does not see clock_gettime in the red fixture")
			return False
		if seen[1]:
			print("FAIL: the detector reports a clock in the clean fixture: " + " ".join(seen[1]))
			return False
	print("PASS: the detector sees clock_gettime in a red fixture and stays silent on a clean one")
	return True


def check_harness_symbols(entry, scratch):
	"""Recompile the real harness TU and refuse any POSIX clock in its undefined symbols."""
	output = os.path.join(scratch, "nam_harness.o")
	command = object_command(entry, output)
	if command is None:
		print("FAIL: the recorded command for " + HARNESS + " is empty")
		return 2
	result, detail = compile_object(command, entry["directory"])
	if result.returncode != 0:
		print("FAIL: the real " + HARNESS + " did not compile:\n" + detail)
		return 1
	names, error = undefined_symbols(output)
	if names is None:
		print("FAIL: nm could not read the real " + HARNESS + ": " + error)
		return 2
	found = clock_symbols(names)
	if found:
		print("FAIL: " + HARNESS + " itself references a POSIX clock: " + " ".join(found))
		print("      The Windows jobs cannot link that (MSYS2 clangarm64's <pthread_time.h>")
		print("      calls clock_gettime64 and the link line has no libwinpthread); use")
		print("      lmms::nam::threadCpuTimeUs() from HarnessClock.h instead.")
		return 1
	print("PASS: " + HARNESS + " references no POSIX clock symbol")
	return 0


def include_flags(paths):
	flags = []
	for path in paths:
		flags.extend(["-I", path])
	return flags


def compile_source(cxx, source, output, include_args, scratch, label):
	result, detail = compile_object(
		[cxx, "-std=c++20"] + include_args + ["-c", source, "-o", output], scratch)
	if result.returncode != 0:
		print("FAIL: " + label + " did not compile:\n" + detail)
		return False
	return True


def build_probe(cxx, tests_dir, scratch, name, includes, probe_text):
	"""Compile and link one platform clock implementation with its probe, then run it."""
	relative = "plugins/NeuralAmp/tests/" + name + ".cpp"
	source = os.path.join(tests_dir, name + ".cpp")
	if not os.path.isfile(source):
		print("FAIL: no " + relative)
		return 1
	probe = os.path.join(scratch, name.lower() + "_probe.cpp")
	with open(probe, "w", encoding="utf-8") as stream:
		stream.write(probe_text)
	include_args = include_flags(includes)
	source_object = os.path.join(scratch, name + ".o")
	probe_object = os.path.join(scratch, name.lower() + "_probe.o")
	binary = os.path.join(scratch, name.lower() + "_probe")
	if not compile_source(
		cxx, source, source_object, include_args, scratch, relative):
		return 1
	if not compile_source(
		cxx, probe, probe_object, include_args, scratch, "the " + name + " probe"):
		return 1
	result = run([cxx, source_object, probe_object, "-o", binary], scratch)
	if result.returncode != 0:
		print("FAIL: the " + name + " probe did not link:\n" +
			(result.stderr or result.stdout).strip())
		return 1
	result = run([binary], scratch)
	if result.returncode != 0:
		print("FAIL: " + (result.stdout or result.stderr).strip())
		return 1
	print((result.stdout or "").strip())
	return 0


def check_posix_clock(cxx, root, scratch):
	"""The POSIX implementation must still measure this thread on this host."""
	tests_dir = os.path.join(root, "plugins", "NeuralAmp", "tests")
	return build_probe(cxx, tests_dir, scratch, "HarnessClockPosix", [tests_dir], POSIX_PROBE)


def check_windows_clock(cxx, root, scratch):
	"""Compile the Win32 implementation against the Microsoft declarations and run its stub."""
	tests_dir = os.path.join(root, "plugins", "NeuralAmp", "tests")
	fake = os.path.join(scratch, "win32")
	os.makedirs(fake, exist_ok=True)
	with open(os.path.join(fake, "windows.h"), "w", encoding="utf-8") as stream:
		stream.write(FAKE_WINDOWS_H)
	return build_probe(
		cxx, tests_dir, scratch, "HarnessClockWindows", [fake, tests_dir], WINDOWS_PROBE)


def check_registration(build):
	"""The configured tree must actually register this CTest."""
	generated = os.path.join(build, "tests", "CTestTestfile.cmake")
	if not os.path.isfile(generated):
		print("FAIL: no generated " + generated + " (was this tree configured?)")
		return 1
	with open(generated, encoding="utf-8") as stream:
		if ("add_test(" + TEST_NAME) not in stream.read():
			print("FAIL: the configured tree did not register " + TEST_NAME)
			return 1
	print("PASS: the configured tree registered " + TEST_NAME)
	return 0


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--source-root", default=".")
	parser.add_argument("--build-dir", required=True)
	options = parser.parse_args()
	root = os.path.abspath(options.source_root)
	build = os.path.abspath(options.build_dir)

	if shutil.which("nm") is None:
		print("FAIL: nm is required to inspect undefined symbols")
		return 2
	entry, problem = find_entry(build, root, HARNESS)
	if entry is None:
		print("FAIL: " + problem)
		return 2
	cxx = compiler_of(entry)
	if cxx is None:
		print("FAIL: the recorded command for " + HARNESS + " names no compiler")
		return 2
	if not self_test(cxx):
		return 1

	with tempfile.TemporaryDirectory(prefix="nam-clock-check-") as scratch:
		status = check_harness_symbols(entry, scratch)
		if status != 0:
			return status
		status = check_posix_clock(cxx, root, scratch)
		if status != 0:
			return status
		status = check_windows_clock(cxx, root, scratch)
		if status != 0:
			return status
	return check_registration(build)


if __name__ == "__main__":
	sys.exit(main())
