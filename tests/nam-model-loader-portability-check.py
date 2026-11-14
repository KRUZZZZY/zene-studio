#!/usr/bin/env python3
"""Fail when the NAM loader regression needs <filesystem>, which macOS below 10.15 lacks.

Cluster ci-9d1768ed0beb1636 (2026-11-14). The hosted macos-x86_64 job builds for a deployment
target below macOS 10.15 (this project's supported floor is 10.13, and CI's macos-env.sh
derives a target from a dependency binary). libc++ marks std::filesystem unavailable there,
so the build stopped on the then-new regression:

    tests/src/core/NamModelLoaderTest.cpp:137:46: error: 'path' is unavailable:
    introduced in macOS 10.15 unknown

Every Linux and Windows host compiled it, so the defect reached the train. The loader's other
portable regressions (NamModelMonoOutputTest, nam_harness) already use plain std::string paths
with mkdtemp/stat for exactly this reason.

This check recompiles the real translation unit from this build's own compile_commands.json
with a shadow <filesystem> that errors - the availability contract the macOS job enforces.
Two probes run first: one that includes <filesystem> must fail under the shadow, and one that
does not must compile. A check that cannot fail fails.

Usage: nam-model-loader-portability-check.py --source-root <tree> --build-dir <build>
"""

import argparse
import json
import os
import shlex
import subprocess
import sys
import tempfile

TARGET = "tests/src/core/NamModelLoaderTest.cpp"
RED_PROBE = "#include <filesystem>\nint main() { return 0; }\n"
GREEN_PROBE = "#include <cstdio>\nint main() { return 0; }\n"


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


def syntax_command(entry, shadow):
	"""The recorded compile line, with the shadow include and no output/compile action."""
	cleaned = []
	skip = False
	for argument in split_command(entry):
		if skip:
			skip = False
			continue
		if argument in ("-o", "-MF", "-MT", "-MQ"):
			skip = True
			continue
		if argument in ("-c", "-MD", "-MMD", "-MP"):
			continue
		if argument.startswith(("-o", "-MF", "-MT", "-MQ")):
			continue
		cleaned.append(argument)
	if not cleaned:
		return None
	cleaned.extend(["-I" + shadow, "-fsyntax-only"])
	return cleaned


def load_entry(build_dir, source_root):
	path = os.path.join(os.path.abspath(build_dir), "compile_commands.json")
	if not os.path.isfile(path):
		return None, "no compile_commands.json in " + os.path.abspath(build_dir)
	with open(path, encoding="utf-8") as stream:
		entries = json.load(stream)
	wanted = os.path.normpath(os.path.join(os.path.abspath(source_root), TARGET))
	for entry in entries:
		if source_of(entry) == wanted:
			return entry, ""
	return None, TARGET + " is not in compile_commands.json (was it renamed?)"


def error_lines(result):
	lines = [line for line in (result.stderr or result.stdout).splitlines() if "error" in line.lower()]
	return "; ".join(lines[:3]) or "compiler exited %d" % result.returncode


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--source-root", required=True)
	parser.add_argument("--build-dir", required=True)
	options = parser.parse_args()

	entry, error = load_entry(options.build_dir, options.source_root)
	if entry is None:
		print("FAIL:", error)
		return 1
	command = split_command(entry)
	if not command:
		print("FAIL:", TARGET, "has no compile command")
		return 1
	compiler = command[0]

	with tempfile.TemporaryDirectory(prefix="zene-nam-filesystem-") as temporary:
		shadow = os.path.join(temporary, "shadow")
		os.makedirs(shadow)
		with open(os.path.join(shadow, "filesystem"), "w", encoding="utf-8") as stream:
			stream.write("#error <filesystem> is unavailable below macOS 10.15\n")
		red = os.path.join(temporary, "red.cpp")
		green = os.path.join(temporary, "green.cpp")
		for path, text in ((red, RED_PROBE), (green, GREEN_PROBE)):
			with open(path, "w", encoding="utf-8") as stream:
				stream.write(text)

		probe = [compiler, "-std=c++20", "-fsyntax-only", "-I" + shadow]
		red_result = run(probe + [red], temporary)
		if red_result.returncode == 0:
			print("FAIL: the shadow lets <filesystem> through, so this check cannot detect the defect")
			return 1
		print("PASS: a probe that includes <filesystem> is refused under the shadow")
		green_result = run(probe + [green], temporary)
		if green_result.returncode != 0:
			print("FAIL: a probe without <filesystem> does not compile under the shadow:",
				error_lines(green_result))
			return 1
		print("PASS: a probe without <filesystem> compiles under the shadow")

		line = syntax_command(entry, shadow)
		if line is None:
			print("FAIL:", TARGET, "has no compile command")
			return 1
		result = run(line, entry["directory"])
		if result.returncode != 0:
			print("FAIL:", TARGET, "does not compile when <filesystem> is unavailable:",
				error_lines(result))
			return 1
		print("PASS:", TARGET, "compiles when <filesystem> is unavailable (macOS < 10.15)")
	return 0


if __name__ == "__main__":
	sys.exit(main())
