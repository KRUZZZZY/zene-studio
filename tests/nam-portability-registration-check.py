#!/usr/bin/env python3
"""Fail when tests/CMakeLists.txt uses PYTHON3_EXECUTABLE before it discovers it.

Cluster ci-9d1768ed0beb1636 (2026-11-14). The regression that keeps NamModelLoaderTest
buildable below macOS 10.15 was registered inside `if(... AND PYTHON3_EXECUTABLE)`, but the
first `find_program(PYTHON3_EXECUTABLE ...)` in tests/CMakeLists.txt came later in the file.
A configure whose cache already held PYTHON3_EXECUTABLE (this lane's warm build tree)
registered the check; a FRESH configure - every hosted job - evaluated the condition while
the variable was still undefined and silently omitted the CTest. A regression that a clean
configure never registers cannot protect anything.

This check enforces the ordering contract on the real file, and that the portability CTest
still exists, is guarded on Linux and on Python, and names a script that exists. Given a
build tree, it also requires the generated tests/CTestTestfile.cmake to contain the CTest -
the registration a configure actually produced. Two probes run first - a use-before-discovery
text must be refused and an ordered text must pass - so a check that cannot fail fails here
instead.

Deterministic: it reads the committed CMakeLists.txt and fixed probe texts; no clock, sleeps,
network or configure.

Usage: nam-portability-registration-check.py [--source-root <tree>] [--build-dir <build>]
"""

import argparse
import os
import re
import sys

CMAKE = os.path.join("tests", "CMakeLists.txt")
TEST_NAME = "NamModelLoaderPortabilityCheck"
DISCOVERY = re.compile(r"^\s*(?:find_program|set)\s*\(\s*PYTHON3_EXECUTABLE\b")
USE = re.compile(r"PYTHON3_EXECUTABLE")
ADD_TEST = re.compile(r"add_test\s*\(\s*NAME\s+" + TEST_NAME + r"\b")
GUARD = re.compile(r"^\s*if\s*\(", re.I)


def code_lines(text):
	"""The file's lines with CMake comments removed (a `#` outside a quoted string)."""
	lines = []
	for raw in text.splitlines():
		out = []
		quoted = False
		for character in raw:
			if character == '"':
				quoted = not quoted
			if character == "#" and not quoted:
				break
			out.append(character)
		lines.append("".join(out))
	return lines


def first_use_before_discovery(text):
	"""(use line, discovery line) of the first use before discovery, or (None, discovery)."""
	lines = code_lines(text)
	discovery = None
	for number, line in enumerate(lines, 1):
		if DISCOVERY.search(line):
			discovery = number
			break
	for number, line in enumerate(lines, 1):
		if discovery is not None and number >= discovery:
			break
		if USE.search(line):
			return number, discovery
	return None, discovery


def add_test_line(lines):
	"""The 1-based line of the portability add_test, or None."""
	for number, line in enumerate(lines, 1):
		if ADD_TEST.search(line):
			return number
	return None


def guard_line(lines, added):
	"""The 1-based line of the if() that encloses the add_test, or None."""
	for number in range(added - 1, 0, -1):
		if GUARD.search(lines[number - 1]):
			return number
	return None


def guard_problem(lines, added):
	"""'' when the enclosing if() names Python and Linux, else the reason."""
	guard = guard_line(lines, added)
	if guard is None:
		return TEST_NAME + " is registered outside any if() guard"
	condition = " ".join(lines[guard - 1:added])
	if "PYTHON3_EXECUTABLE" not in condition:
		return TEST_NAME + " is not guarded on PYTHON3_EXECUTABLE (line %d)" % guard
	if 'CMAKE_SYSTEM_NAME STREQUAL "Linux"' not in condition:
		return TEST_NAME + " is not guarded on Linux (line %d)" % guard
	return ""


def script_problem(lines, added, source_root):
	"""'' when the block names an existing tests/*.py, else the reason."""
	block = " ".join(lines[added - 1:added + 8])
	match = re.search(r"([\w./+${}-]+\.py)", block)
	if match is None:
		return TEST_NAME + " names no Python script"
	script = os.path.join(source_root, "tests", os.path.basename(match.group(1)))
	if not os.path.isfile(script):
		return "the registered script does not exist: " + script
	return ""


def registration_problem(text, source_root):
	"""'' when the portability CTest is present and properly guarded, else the reason."""
	lines = code_lines(text)
	added = add_test_line(lines)
	if added is None:
		return "no add_test(NAME " + TEST_NAME + " ...) in " + CMAKE
	return guard_problem(lines, added) or script_problem(lines, added, source_root)


def generated_problem(build_dir):
	"""'' when the configured tree registered the CTest, else the reason."""
	generated = os.path.join(build_dir, "tests", "CTestTestfile.cmake")
	if not os.path.isfile(generated):
		return "no generated " + generated + " (was this tree configured?)"
	with open(generated, encoding="utf-8") as stream:
		text = stream.read()
	if ADD_TEST.search(text) is None and ("add_test(" + TEST_NAME) not in text:
		return "the configured tree at " + build_dir + " did not register " + TEST_NAME
	return ""


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--source-root", default=".")
	parser.add_argument("--build-dir", default="")
	options = parser.parse_args()
	root = os.path.abspath(options.source_root)
	path = os.path.join(root, CMAKE)
	if not os.path.isfile(path):
		print("FAIL: no " + CMAKE + " under " + root)
		return 2
	with open(path, encoding="utf-8") as stream:
		text = stream.read()

	bad = "if(PYTHON3_EXECUTABLE)\nadd_test(NAME X COMMAND y)\nendif()\nfind_program(PYTHON3_EXECUTABLE NAMES python3)\n"
	good = "find_program(PYTHON3_EXECUTABLE NAMES python3)\nif(PYTHON3_EXECUTABLE)\nadd_test(NAME X COMMAND y)\nendif()\n"
	bad_use, _ = first_use_before_discovery(bad)
	good_use, good_discovery = first_use_before_discovery(good)
	if bad_use is None:
		print("FAIL: the ordering probe does not detect a use before discovery")
		return 2
	if good_use is not None or good_discovery is None:
		print("FAIL: the ordering probe refuses a correctly ordered file")
		return 2
	print("PASS: the ordering probe detects a use before discovery and accepts the reverse")

	use, discovery = first_use_before_discovery(text)
	if discovery is None:
		print("FAIL: " + CMAKE + " never discovers PYTHON3_EXECUTABLE")
		return 1
	if use is not None:
		print("FAIL: " + CMAKE + " uses PYTHON3_EXECUTABLE at line %d but discovers it at line %d;"
			" a fresh configure evaluates the use while the variable is undefined" % (use, discovery))
		return 1
	print("PASS: "+ CMAKE + " discovers PYTHON3_EXECUTABLE (line %d) before every use" % discovery)

	problem = registration_problem(text, root)
	if problem:
		print("FAIL:", problem)
		return 1
	print("PASS: " + TEST_NAME + " is a guarded Linux CTest naming an existing script")
	if options.build_dir:
		problem = generated_problem(os.path.abspath(options.build_dir))
		if problem:
			print("FAIL:", problem)
			return 1
		print("PASS: the configured tree registered " + TEST_NAME)
	return 0


if __name__ == "__main__":
	sys.exit(main())
