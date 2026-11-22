#!/usr/bin/env python3
"""Fail when a translation unit needs a C++20 synchronisation primitive macOS below 11 lacks.

Cluster ci-61ad26e4f8b135f9 (2026-11-14). The hosted macos-x86_64 job builds for a deployment
target below macOS 11 - this project's supported floor is 10.13, and CI's macos-env.sh derives
a target from a dependency binary - where Apple's libc++ marks the C++20 synchronisation
primitives unavailable (_LIBCPP_AVAILABILITY_SYNC):

    tests/src/core/RemotePluginShmFifoTest.cpp:109:16: error: 'barrier' is unavailable:
    introduced in macOS 11.0 unknown

<barrier>, <latch>, <semaphore> and std::atomic's wait/notify_one/notify_all members are all
gated that way. Every Linux and Windows host compiled the same code, so the defect reached the
train.

Raising each test target's deployment floor would hide the product's own floor instead of
fixing anything, so the tests use tests/src/core/PortableSync.h (a mutex and a condition
variable, available long before 10.13) and the AudioEngine test seam polls a flag instead of
calling std::atomic::notify_all. A product wake that may not lock or allocate keeps its own
primitive, lmms::workerwake (include/AudioWorkerWake.h).

The check has two halves, both deterministic (fixed inputs, no timing, no threads):

  1. COMPILE MODEL. A shadow <barrier>/<latch>/<semaphore> ahead of the system headers refuses
     each header exactly as libc++ does below macOS 11, and every real translation unit listed
     below is recompiled from this build's own compile_commands.json. Red and green probes run
     first, so a model that cannot fail fails the check.
  2. SOURCE SCAN. Two things the shadow cannot model: a file that includes a gated header
     through a path the shadow does not see, and the std::atomic wait/notify members, which are
     members of the real <atomic> and not a header at all. The scan (comments stripped) fails on
     any std::barrier / std::latch / std::binary_semaphore / std::counting_semaphore use, any
     #include of the three headers, and any first-party object declared as std::atomic<...> that
     is then the receiver of .wait(, .notify_one( or .notify_all(. The one audited exception is
     src/core/AudioWorkerWake.cpp: its fallback for platforms without a compare-and-wait trap is
     behind #ifndef LMMS_WORKER_WAKE_WAIT_ON_ADDRESS, which Apple and Linux both define, so no
     Apple build compiles it; the Apple path is __ulock_wait/__ulock_wake (macOS 10.12).

Usage: macos-sync-availability-check.py --root <tree> --build-dir <build>
"""

import argparse
import json
import os
import re
import shlex
import subprocess
import sys
import tempfile

#: TUs that must be in a default configure and must compile under the model.
REQUIRED_TARGETS = [
	"src/core/AudioEngine.cpp",
	"tests/src/core/ModulationAudioViewTest.cpp",
	"tests/src/core/NotePlaybackTest.cpp",
	"tests/src/core/RemotePluginShmFifoTest.cpp",
	"tests/src/core/ScriptDawModelRegistrationTest.cpp",
	"tests/src/core/TempoMapTest.cpp",
	"tests/src/plugins/PluginPortsMigrationTest.cpp",
]

#: TUs that only exist when the optional wasm sandbox is configured.
OPTIONAL_TARGETS = ["src/wasm/WasmWorkerPool.cpp", "tests/src/wasm/WasmWorkerPoolTest.cpp"]

SYNC_HEADERS = ("barrier", "latch", "semaphore")

SYNC_RED_PROBE = "#include <barrier>\nint main() { std::barrier<> b{1}; b.arrive_and_wait(); }\n"
SYNC_GREEN_PROBE = "#include <cstddef>\nint main() { return 0; }\n"

#: Vendored trees that are not this repo's code (the same exclusions tests/all-sources.txt uses).
VENDORED = (
	"src/3rdparty/",
	"tests/reference/",
	"plugins/NeuralAmp/rtneural/",
	"plugins/NeuralAmp/nam/",
	"plugins/NeuralAmp/tests/",
	"plugins/RnnoiseDenoiser/rnnoise/",
	"plugins/SfzPlayer/sfizz/",
)

#: The audited exception, with the reason stated in the module docstring.
ATOMIC_SCAN_EXCEPTIONS = {"src/core/AudioWorkerWake.cpp"}

ATOMIC_DECLARATION = re.compile(
	r"std::atomic\s*<[^;{}]*>\s*[\s*&]*(?:const\s+)?[\s*&]*([A-Za-z_]\w*)\s*(?=[=;{[(])")
ATOMIC_RECEIVER_CALL = re.compile(r"\b([A-Za-z_]\w*)\s*(?:\.|->)\s*(?:wait|notify_one|notify_all)\s*\(")
GATED_TYPE = re.compile(r"\bstd::(barrier|latch|binary_semaphore|counting_semaphore)\b")
GATED_INCLUDE = re.compile(r"^\s*#\s*include\s*<(barrier|latch|semaphore)>")


def strip_comments(text):
	"""Text with // and /* */ comments blanked, so prose about a primitive is not a use."""
	out = []
	index = 0
	length = len(text)
	while index < length:
		if text.startswith("//", index):
			end = text.find("\n", index)
			index = length if end < 0 else end
		elif text.startswith("/*", index):
			end = text.find("*/", index + 2)
			stop = length if end < 0 else end + 2
			# Keep every newline the comment held, so reported line numbers stay exact.
			out.append(" " + "\n" * text.count("\n", index, stop))
			index = stop
		elif text[index] in "\"'":
			quote = text[index]
			end = index + 1
			while end < length and text[end] != quote:
				end += 2 if text[end] == "\\" else 1
			out.append(text[index:end + 1])
			index = end + 1
		else:
			out.append(text[index])
			index += 1
	return "".join(out)


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


def syntax_command(entry, extra):
	"""The recorded compile line, plus extras, without output or compile action."""
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
	cleaned.extend(extra)
	cleaned.append("-fsyntax-only")
	return cleaned


def error_lines(result):
	lines = [line for line in (result.stderr or result.stdout).splitlines() if "error" in line.lower()]
	return " | ".join(lines[:3]) or "compiler exited %d" % result.returncode


def first_party_sources(root):
	result = run(["git", "ls-files", "*.c", "*.cc", "*.cpp", "*.cxx", "*.h", "*.hpp"], root)
	if result.returncode != 0:
		raise RuntimeError("git ls-files failed: " + result.stderr.strip())
	return [path for path in result.stdout.splitlines()
		if path and not path.startswith(VENDORED)]


def scan_line(relative, number, line, declarations):
	"""One hit tuple for a line that uses a gated primitive, or None."""
	include = GATED_INCLUDE.match(line)
	if include:
		return (relative, number, "#include <%s> is macOS 11 only" % include.group(1), line.strip())
	use = GATED_TYPE.search(line)
	if use:
		return (relative, number, "std::%s is macOS 11 only" % use.group(1), line.strip())
	for name in ATOMIC_RECEIVER_CALL.findall(line):
		if name in declarations:
			return (relative, number,
				"std::atomic %r is waited/notified (macOS 11 only)" % name, line.strip())
	return None


def scan_source(relative, text, hits):
	"""Append every hit in one file's comment-stripped text."""
	declarations = set(ATOMIC_DECLARATION.findall(text))
	for number, line in enumerate(text.splitlines(), 1):
		hit = scan_line(relative, number, line, declarations)
		if hit is not None:
			hits.append(hit)


def source_scan(root):
	"""(relative, line, message, text) for every gated header, type or atomic wait/notify use."""
	hits = []
	for relative in first_party_sources(root):
		try:
			with open(os.path.join(root, relative), encoding="utf-8", errors="replace") as stream:
				text = strip_comments(stream.read())
		except OSError:
			continue
		scan_source(relative, text, hits)
	return [hit for hit in hits if hit[0] not in ATOMIC_SCAN_EXCEPTIONS]


def write_shadow(temporary):
	"""A shadow include directory that refuses the three headers to a translation unit."""
	shadow = os.path.join(temporary, "shadow")
	os.makedirs(shadow)
	for header in SYNC_HEADERS:
		# Refuse a header the translation unit includes ITSELF, then defer to the real one.
		# libc++ refuses the declarations rather than the header, so a standard header that
		# includes <semaphore> internally (libstdc++'s <stop_token> does) must keep working:
		# below macOS 11 what breaks is a use, and a use needs the direct include.
		with open(os.path.join(shadow, header), "w", encoding="utf-8") as stream:
			stream.write("#if __INCLUDE_LEVEL__ <= 1\n"
				"#error <%s> is unavailable below macOS 11\n"
				"#endif\n"
				"#include_next <%s>\n" % (header, header))
	return shadow


def probe_shadow(compiler, temporary, shadow):
	"""Failures of the model's own red/green probes: a model that cannot fail fails here."""
	failures = []
	probe = os.path.join(temporary, "probe.cpp")
	for text, expected, label in (
		(SYNC_RED_PROBE, False, "includes <barrier>"),
		(SYNC_GREEN_PROBE, True, "does not include a gated header"),
	):
		with open(probe, "w", encoding="utf-8") as stream:
			stream.write(text)
		result = run([compiler, "-std=c++20", "-I" + shadow, probe], temporary)
		if (result.returncode == 0) != expected:
			failures.append("the shadow model is broken: a probe that %s %s" % (
				label, "compiled" if result.returncode == 0 else "did not compile"))
	return failures


def select_targets(root, by_source):
	"""(absolute targets, optional labels, failure) from the compile database."""
	required = [os.path.normpath(os.path.join(root, target)) for target in REQUIRED_TARGETS]
	missing = [target for target in required if target not in by_source]
	if missing:
		return [], [], "not in compile_commands.json: " + ", ".join(
			os.path.relpath(target, root) for target in missing)
	targets = list(required)
	optional = []
	for target in OPTIONAL_TARGETS:
		candidate = os.path.normpath(os.path.join(root, target))
		if candidate in by_source:
			targets.append(candidate)
			optional.append(target)
	return targets, optional, ""


def compile_one(root, by_source, target, shadow):
	"""One failure string for a target that does not compile under the shadow, or ''."""
	entry = by_source[target]
	name = os.path.relpath(target, root)
	command = syntax_command(entry, ["-I" + shadow])
	if command is None:
		return name + " has no compile command"
	result = run(command, entry["directory"])
	if result.returncode == 0:
		return ""
	return ("%s does not compile when <barrier>/<latch>/<semaphore> are unavailable "
		"(macOS < 11): %s" % (name, error_lines(result)))


def compile_model(root, build_dir):
	"""(failures, number of targets compiled) for the compile half of the check."""
	path = os.path.join(os.path.abspath(build_dir), "compile_commands.json")
	if not os.path.isfile(path):
		return ["no compile_commands.json in " + os.path.abspath(build_dir)], 0
	with open(path, encoding="utf-8") as stream:
		by_source = {source_of(entry): entry for entry in json.load(stream)}
	targets, optional, failure = select_targets(root, by_source)
	if failure:
		return [failure], 0
	first_command = split_command(by_source[targets[0]])
	if not first_command:
		return [REQUIRED_TARGETS[0] + " has no compile command"], 0
	compiler = first_command[0]
	with tempfile.TemporaryDirectory(prefix="zene-macos-sync-") as temporary:
		shadow = write_shadow(temporary)
		failures = probe_shadow(compiler, temporary, shadow)
		for target in targets:
			failure = compile_one(root, by_source, target, shadow)
			if failure:
				failures.append(failure)
	if optional:
		print("Also checked the configured optional wasm translation units:", ", ".join(optional))
	else:
		print("The optional wasm translation units are not in this configure (WANT_WASM off)")
	return failures, len(targets)


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--root", required=True)
	parser.add_argument("--build-dir", required=True)
	options = parser.parse_args()
	root = os.path.abspath(options.root)

	failures, compiled = compile_model(root, options.build_dir)
	if compiled and not failures:
		print("PASS: %d translation units compile when the macOS-11-only headers are unavailable"
			% compiled)
	for failure in failures:
		print("FAIL:", failure)

	try:
		hits = source_scan(root)
	except RuntimeError as error:
		print("FAIL:", error)
		hits = []
	if hits:
		for relative, number, message, line in hits[:20]:
			print("FAIL: %s:%d %s: %s" % (relative, number, message, line))
		if len(hits) > 20:
			print("FAIL: ... and %d more" % (len(hits) - 20))
	else:
		print("PASS: no first-party source uses a macOS-11-only header, type or atomic wait/notify "
			"(except the audited %s fallback)" % ", ".join(sorted(ATOMIC_SCAN_EXCEPTIONS)))

	if failures or hits:
		return 1
	return 0


if __name__ == "__main__":
	sys.exit(main())
