#!/usr/bin/env python3
"""Fail when a CMake target that compiles the NeuralAmp NAM engine lacks the Eigen warning suppression.

Cluster ci-c487f7d0c9f0a1ac (2026-11-14). The vendored Eigen under plugins/NeuralAmp/rtneural
emits -Wdeprecated-enum-enum-conversion on aarch64, and -DUSE_WERROR turns that into an error in
code this repo does not modify. The linux-arm64 job failed because NamModelLoaderTest compiles
plugins/NeuralAmp/nam/NamModel.cpp but did not carry -Wno-error=deprecated-enum-enum-conversion;
every other NAM target already did, and only the CI architecture that emits the warning showed it.

Every target that gets nam/NamModel.cpp must carry that option, through ${ZENE_NAM_EIGEN_OPTIONS}
(tests/CMakeLists.txt), ${NEURALAMP_ENGINE_OPTIONS} (plugins/NeuralAmp/tests/CMakeLists.txt) or a
literal in its own target_compile_options. This check reads the three first-party CMake files
(never a vendored tree), expands the simple set() variables of each file, and reports one line per
NAM target. It then reads each target's generated flags.make, when this build configured it.
A target floor makes a parser that finds nothing fail instead of pass.

Usage: nam-eigen-options-coverage-check.py --source-root <tree> --build-dir <build>
"""

import argparse
import bisect
import os
import re
import sys

CMAKE_FILES = (
	"tests/CMakeLists.txt",
	"plugins/NeuralAmp/CMakeLists.txt",
	"plugins/NeuralAmp/tests/CMakeLists.txt",
)
NAM_SOURCE_SUFFIX = "nam/NamModel.cpp"
OPTION = "-Wno-error=deprecated-enum-enum-conversion"
FLAG_MARKER = "deprecated-enum-enum-conversion"
REQUIRED = ("neuralamp", "nam_harness", "NamModelLoaderTest", "NamModelPrewarmTest",
	"NamModelMonoOutputTest", "NeuralAmpModelLoadTest", "NeuralAmpWavInputTest")
SOURCE_COMMANDS = ("add_executable", "add_library", "build_plugin", "target_sources")
COMMAND_START = re.compile(r"[A-Za-z_]\w*[ \t]*\(")
TOKEN = re.compile(r'"((?:[^"\\]|\\.)*)"|([^\s"]+)')
PAREN_TOKEN = re.compile(r'"(?:[^"\\]|\\.)*"|#[^\n]*|[()]')
VARIABLE = re.compile(r"\$\{(\w+)\}")
TEST_NAME_IF = re.compile(r'LMMS_TEST_NAME\s+STREQUAL\s+"([^"]+)"')


def close_paren(text, start):
	"""Index of the ')' that closes a call whose arguments begin at start, quotes and comments aware."""
	depth = 1
	for match in PAREN_TOKEN.finditer(text, start):
		if match.group() == "(":
			depth += 1
		elif match.group() == ")":
			depth -= 1
			if depth == 0:
				return match.start()
	return len(text)


def commands(text):
	"""(lowercase name, argument text, line) for each call, in file order."""
	newlines = [m.start() for m in re.finditer("\n", text)]
	found, index = [], 0
	while index < len(text):
		if text[index] == "#":
			index = text.find("\n", index)
			if index < 0:
				break
			continue
		match = COMMAND_START.match(text, index)
		if match is None:
			word = re.compile(r"\w+").match(text, index)
			index = word.end() if word else index + 1
			continue
		end = close_paren(text, match.end())
		name = match.group(0).rstrip("( \t").lower()
		found.append((name, text[match.end():end], bisect.bisect_left(newlines, match.end()) + 1))
		index = end + 1
	return found


def tokens(args, line):
	"""(token, line) pairs of an argument text; the line is that of the token itself."""
	return [(m.group(1) if m.group(1) is not None else m.group(2), line + args.count("\n", 0, m.start()))
		for m in TOKEN.finditer(args)]


def expand(value, variables, depth=3):
	for _ in range(depth):
		value = VARIABLE.sub(lambda m: ";".join(variables.get(m.group(1), [])), value)
	return value


def if_name(condition):
	match = TEST_NAME_IF.search(condition)
	return match.group(1) if match else None


def track_if(stack, name, args):
	if name == "if":
		stack.append(if_name(args))
	elif name in ("elseif", "else") and stack:
		stack[-1] = if_name(args)
	elif name == "endif" and stack:
		stack.pop()


def target_of(raw, stack):
	if raw == "${LMMS_TEST_NAME}":
		return next((n for n in reversed(stack) if n), raw)
	return raw


def record(targets, name, relative, field, entry):
	target = targets.setdefault(name, {"file": relative, "sources": [], "options": []})
	target[field].append(entry)


def read_file(root, relative):
	"""(targets, variables) of one CMake file: raw source and option tokens per target."""
	with open(os.path.join(root, relative), encoding="utf-8") as stream:
		text = stream.read()
	targets, variables, stack = {}, {}, []
	for name, args, line in commands(text):
		track_if(stack, name, args)
		words = tokens(args, line)
		if not words:
			continue
		if name == "set":
			variables[words[0][0]] = [w for w, _ in words[1:]]
		elif name in SOURCE_COMMANDS:
			for word, word_line in words[1:]:
				record(targets, target_of(words[0][0], stack), relative, "sources", (word, word_line))
		elif name == "target_compile_options":
			record(targets, target_of(words[0][0], stack), relative, "options",
				(";".join(w for w, _ in words[1:]), line))
	return targets, variables


def nam_line(target, variables):
	"""Line where the target gets NamModel.cpp, or None when it does not compile the engine."""
	for word, line in target["sources"]:
		if any(part.endswith(NAM_SOURCE_SUFFIX) for part in expand(word, variables).split(";")):
			return line
	return None


def option_line(target, variables):
	for raw, line in target["options"]:
		if OPTION in expand(raw, variables):
			return line
	return None


def discover(root):
	"""{target: (file, nam_line, option_line or None)} for every NAM target in the three files."""
	found = {}
	for relative in CMAKE_FILES:
		targets, variables = read_file(root, relative)
		for name, target in sorted(targets.items()):
			line = nam_line(target, variables)
			if line is not None:
				found[name] = (relative, line, option_line(target, variables))
	return found


def static_check(found):
	failures = []
	for name, (relative, line, option) in sorted(found.items()):
		if option is None:
			print("MISSING %s (%s:%d)" % (name, relative, line))
			failures.append("%s compiles %s without %s" % (name, NAM_SOURCE_SUFFIX, OPTION))
		else:
			print("OK %s (%s:%d)" % (name, relative, option))
	return failures


def generated_check(found, build_dir):
	failures = []
	for name, (relative, _, _) in sorted(found.items()):
		flags = os.path.join(build_dir, os.path.dirname(relative), "CMakeFiles", name + ".dir", "flags.make")
		if not os.path.isfile(flags):
			print("SKIP-GENERATED", name)
			continue
		with open(flags, encoding="utf-8", errors="replace") as stream:
			present = FLAG_MARKER in stream.read()
		print("%s-GENERATED %s (%s)" % ("OK" if present else "MISSING", name, os.path.relpath(flags, build_dir)))
		if not present:
			failures.append("%s: generated flags.make lacks %s" % (name, FLAG_MARKER))
	return failures


def floor_check(found):
	return ["%s is not discovered as a NAM target (parser or CMake layout changed)" % name
		for name in REQUIRED if name not in found]


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--source-root", required=True)
	parser.add_argument("--build-dir", required=True)
	options = parser.parse_args()

	root = os.path.abspath(options.source_root)
	found = discover(root)
	failures = floor_check(found) + static_check(found)
	failures += generated_check(found, os.path.abspath(options.build_dir))
	for failure in failures:
		print("FAIL:", failure)
	if failures:
		return 1
	print("PASS")
	return 0


if __name__ == "__main__":
	sys.exit(main())
