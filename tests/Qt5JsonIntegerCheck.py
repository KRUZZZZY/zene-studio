#!/usr/bin/env python3
"""Fail when first-party code calls QJsonValue::toInteger outside a Qt 6 guard.

Cluster ci-9b3cdba8549c073f (2026-11-15), hosted `build / linux-arm64` job: unlike the
other Linux jobs that configure with -DWANT_QT6=ON, `linux-arm64` builds with the
CMakeLists default, WANT_QT6=OFF, so it compiles every translation unit against Qt 5.15.
tests/src/core/ScriptClockTest.cpp calls QJsonValue::toInteger, which Qt 5 does not
declare, and the build stopped at 59%:

    tests/src/core/ScriptClockTest.cpp:408:92: error: 'class QJsonValue' has no member
        named 'toInteger'
    tests/src/core/ScriptClockTest.cpp:415:69: error: 'class QJsonValue' has no member
        named 'toInteger'

The tree already has the portable spelling: lmms::jsonInteger() in
include/DeprecationHelper.h (used by the src/core and tests files that predate this one).
It calls QJsonValue::toInteger on Qt 6 and reproduces the same "default for a value that
is not a whole, in-range integer" contract on Qt 5.

The check has two deterministic halves:

  * a source scan of the first-party trees (src, include, tests/src, plugins; vendored
    third-party trees excluded) for a `.toInteger(`/`->toInteger(` call that is not
    inside an `#if QT_VERSION >= 6...` branch. It reads sources only, so it is red on
    this Qt 6 host where the defect compiles.
  * a compile probe: where pkg-config finds a Qt 5 tree, the recorded compile command of
    the affected translation unit (build/compile_commands.json) is retargeted at those
    headers, with a Qt 5 moc standing in for the Qt 6-generated one, and compiled
    `-fsyntax-only`. Before the fix that is the hosted failure; after it, clean.

Both halves prove themselves: the scan has red/green fixtures, and the probe compiles a
known-bad `toInteger` control (must be rejected by the Qt 5 tree) and a known-good
`toInt` control (must be accepted). Without a Qt 5 tree the probe reports itself skipped,
never passed.

Usage:
  Qt5JsonIntegerCheck.py [--root DIR] [--build-dir DIR] [--cxx CXX]
  Qt5JsonIntegerCheck.py --self-test

Exit codes: 0 = clean, 1 = finding (or a failed fixture), 2 = usage error.
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

SOURCE_DIRS = ("src", "include", "tests/src", "plugins")
SOURCE_SUFFIXES = (".h", ".hpp", ".hxx", ".cpp", ".cxx", ".cc")
# Vendored trees keep their own Qt-version story and are not this fork's code.
SKIP_DIRS = {
	".git", "build", "3rdparty", "external", "vst3sdk", "clap",
	os.path.join("plugins", "CarlaBase", "carla"),
	os.path.join("plugins", "NeuralAmp", "rtneural"),
	os.path.join("plugins", "NeuralAmp", "nam"),
	os.path.join("plugins", "SfzPlayer", "sfizz"),
	os.path.join("plugins", "RnnoiseDenoiser", "rnnoise"),
}
TARGET = "tests/src/core/ScriptClockTest.cpp"

# Comments, string and char literals are blanked before the scan (line numbers kept).
NON_CODE = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'', re.S)
TO_INTEGER = re.compile(r"(?:->|\.)\s*toInteger\s*\(")
QT6_GUARD = re.compile(r"QT_VERSION\s*(?:>=|>)\s*(?:QT_VERSION_CHECK\s*\(\s*6|6)")

BAD_PROBE = """\
#include <QJsonValue>
int main()
{
	QJsonValue value(1);
	return static_cast<int>(value.toInteger(-1));
}
"""
GOOD_PROBE = """\
#include <QJsonValue>
int main()
{
	QJsonValue value(1);
	return value.toInt(-1);
}
"""
FIXTURE_BAD = """\
#include <QJsonValue>
qint64 bytes(const QJsonObject& state)
{
	return state.value(QStringLiteral("source_bytes")).toInteger(-1);
}
"""
FIXTURE_GOOD = """\
#include <QJsonValue>
qint64 bytes(const QJsonObject& state)
{
	return jsonInteger(state.value(QStringLiteral("source_bytes")), -1);
}
"""
FIXTURE_GUARDED = """\
#include <QJsonValue>
qint64 bytes(const QJsonValue& value)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
	return value.toInteger(-1);
#else
	return value.isDouble() ? static_cast<qint64>(value.toDouble()) : -1;
#endif
}
"""


def blank_non_code(text):
	"""Comments and literals become spaces; every newline and offset is kept."""
	return NON_CODE.sub(lambda match: re.sub(r"[^\n]", " ", match.group(0)), text)


def qt6_guarded_lines(lines):
	"""0-based indices of the lines inside an `#if QT_VERSION >= 6...` branch."""
	guarded, stack = set(), []
	for index, line in enumerate(lines):
		stripped = line.strip()
		if stripped.startswith("#if"):
			stack.append(bool(QT6_GUARD.search(stripped[3:])))
		elif stripped.startswith("#elif") and stack:
			stack[-1] = bool(QT6_GUARD.search(stripped[5:]))
		elif stripped.startswith("#else") and stack:
			stack[-1] = False
		elif stripped.startswith("#endif") and stack:
			stack.pop()
		elif any(stack):
			guarded.add(index)
	return guarded


def scan_text(name, text):
	"""(path, line, what) for QJsonValue::toInteger outside a Qt 6 guard."""
	code = blank_non_code(text)
	lines = code.splitlines()
	guarded = qt6_guarded_lines(lines)
	findings = []
	for match in TO_INTEGER.finditer(code):
		line = code.count("\n", 0, match.start())
		if line in guarded:
			continue
		findings.append((name, line + 1, "QJsonValue::toInteger is Qt 6 only; use jsonInteger()"))
	return findings


def read_text(path):
	try:
		with open(path, encoding="utf-8", errors="replace") as handle:
			return handle.read()
	except OSError:
		return None


def skipped(name):
	name = name.replace("\\", "/").strip("/")
	for prefix in SKIP_DIRS:
		prefix = prefix.replace("\\", "/").strip("/")
		if name == prefix or name.startswith(prefix + "/"):
			return True
	return False


def scan_tree(root):
	findings = []
	for source_dir in SOURCE_DIRS:
		base = os.path.join(root, source_dir)
		if not os.path.isdir(base):
			continue
		for directory, subdirs, files in os.walk(base):
			relative_dir = os.path.relpath(directory, root).replace("\\", "/")
			subdirs[:] = sorted(d for d in subdirs
				if not skipped(os.path.join(relative_dir, d)))
			for entry in sorted(files):
				if not entry.endswith(SOURCE_SUFFIXES):
					continue
				name = os.path.join(relative_dir, entry).replace("\\", "/")
				text = read_text(os.path.join(directory, entry))
				if text is not None:
					findings += scan_text(name, text)
	return findings


def self_test():
	failures = []
	if not scan_text("fixture-bad.cpp", FIXTURE_BAD):
		failures.append("the scan did not report the unguarded .toInteger() fixture")
	if scan_text("fixture-good.cpp", FIXTURE_GOOD):
		failures.append("the scan reported the jsonInteger() fixture")
	if scan_text("fixture-guarded.cpp", FIXTURE_GUARDED):
		failures.append("the scan reported toInteger() inside a QT_VERSION >= 6 guard")
	return failures


def run(command, cwd=None, timeout=300):
	try:
		return subprocess.run(command, cwd=cwd, capture_output=True, text=True, timeout=timeout)
	except (OSError, subprocess.SubprocessError):
		return None


def pkg_flags(packages, option):
	if not shutil.which("pkg-config"):
		return None
	result = run(["pkg-config", option, *packages])
	if result is None or result.returncode != 0:
		return None
	return shlex.split(result.stdout)


def qt5_flags():
	if not shutil.which("pkg-config"):
		return None
	exists = run(["pkg-config", "--exists", "Qt5Widgets"])
	if exists is None or exists.returncode != 0:
		return None
	return pkg_flags(("Qt5Widgets", "Qt5Xml", "Qt5Network", "Qt5Test"), "--cflags")


def qt5_moc():
	candidates = []
	host_bins = pkg_flags(("Qt5Core",), "--variable=host_bins")
	if host_bins:
		candidates.append(os.path.join(host_bins[0], "moc"))
	candidates += [shutil.which("moc-qt5"), shutil.which("moc")]
	for candidate in candidates:
		if not candidate or not os.path.isfile(candidate):
			continue
		version = run([candidate, "--version"])
		if version is not None and version.returncode == 0 and version.stdout.strip().startswith("moc 5."):
			return candidate
	return None


def is_qt6_include(directory):
	parts = [p for p in re.split(r"[\\/]+", directory.rstrip("\\/")) if p]
	return "qt6" in (p.lower() for p in parts)


def include_argument(argument, following):
	"""(value, consumed tokens, separate form) for a -I/-isystem argument, else (None, 0, False)."""
	for prefix in ("-isystem", "-I"):
		if argument == prefix and following is not None:
			return following, 2, True
		if argument.startswith(prefix) and len(argument) > len(prefix):
			return argument[len(prefix):], 1, False
	return None, 0, False


def without_output_arguments(arguments):
	"""The compile line with -o <file> and -c removed (the probe writes no object)."""
	out, index = [], 0
	while index < len(arguments):
		if arguments[index] == "-o":
			index += 2
		elif arguments[index] == "-c":
			index += 1
		else:
			out.append(arguments[index])
			index += 1
	return out


def retargeted_command(arguments, flags, source, moc_dir):
	"""The recorded Qt 6 compile line, rebuilt against the Qt 5 headers and moc."""
	out, index = [], 0
	arguments = without_output_arguments(arguments)
	while index < len(arguments):
		argument = arguments[index]
		following = arguments[index + 1] if index + 1 < len(arguments) else None
		value, consumed, separate = include_argument(argument, following)
		if value is None:
			out.append(argument)
		elif is_qt6_include(value):
			pass
		elif "ScriptClockTest_autogen/include" in value.replace("\\", "/"):
			out += [argument, moc_dir] if separate else ["-I", moc_dir]
		else:
			out += [argument, value] if separate else [argument]
		index += consumed or 1
	return out + flags + ["-fsyntax-only", source]


def entry_for(database):
	if not os.path.isfile(database):
		return None
	with open(database, encoding="utf-8", errors="replace") as handle:
		entries = json.load(handle)
	for entry in entries:
		if entry.get("file", "").replace("\\", "/").endswith(TARGET):
			return entry
	return None


def diagnostic(result):
	lines = (result.stdout + result.stderr).splitlines()
	return "\n".join(dict.fromkeys(line for line in lines
		if "error:" in line or "fatal error:" in line))[:2000]


def control_failure(cxx, flags, workdir):
	"""None when the Qt 5 tree rejects toInteger and accepts toInt, else the failure text."""
	checks = []
	for name, text in (("bad.cpp", BAD_PROBE), ("good.cpp", GOOD_PROBE)):
		path = os.path.join(workdir, name)
		with open(path, "w", encoding="utf-8") as handle:
			handle.write(text)
		checks.append(run([cxx, "-std=gnu++20", "-fsyntax-only", *flags, path]))
	if checks[0] is None or checks[0].returncode == 0:
		return "probe: this Qt 5 tree accepts QJsonValue::toInteger; the control did not fail"
	if checks[1] is None or checks[1].returncode != 0:
		return "probe: the Qt 5 tree rejected the QJsonValue::toInt control"
	return None


def translation_unit_failure(entry, flags, moc, workdir):
	"""None when the affected translation unit compiles against Qt 5, else the failure text."""
	source = entry.get("file", "")
	moc_file = os.path.join(workdir, os.path.splitext(os.path.basename(source))[0] + ".moc")
	generated = run([moc, source, "-o", moc_file], cwd=entry.get("directory"))
	if generated is None or generated.returncode != 0:
		return f"probe: the Qt 5 moc could not process {source}"
	arguments = shlex.split(entry["command"]) if entry.get("command") else list(entry.get("arguments") or [])
	compiled = run(retargeted_command(arguments, flags, source, workdir),
		cwd=entry.get("directory"))
	if compiled is None:
		return f"probe: the Qt 5 compile of {source} could not run"
	if compiled.returncode != 0:
		return f"{source}: Qt 5 rejected it\n{diagnostic(compiled)}"
	return None


def compile_probe(entry, flags, moc, workdir):
	"""Compile the affected translation unit against Qt 5, after its own controls."""
	failure = control_failure(entry.get("cxx", "c++"), flags, workdir)
	if failure is not None:
		return [failure]
	failure = translation_unit_failure(entry, flags, moc, workdir)
	return [failure] if failure is not None else []


def check(root, build_dir, cxx):
	findings = scan_tree(root)
	flags = qt5_flags()
	moc = qt5_moc() if flags is not None else None
	if flags is None or moc is None:
		print("probe: skipped (no Qt 5 development tree and moc found)")
		return findings
	entry = entry_for(os.path.join(build_dir, "compile_commands.json"))
	if entry is None:
		findings.append((TARGET, None, f"no compile_commands.json entry under {build_dir}"))
		return findings
	entry.setdefault("cxx", cxx)
	workdir = tempfile.mkdtemp(prefix="qt5-json-integer-")
	try:
		failures = compile_probe(entry, flags, moc, workdir)
	finally:
		shutil.rmtree(workdir, ignore_errors=True)
	for failure in failures:
		headline, _, detail = failure.partition("\n")
		findings.append((headline, None, detail))
	return findings


def report(findings):
	for name, line, what in findings:
		print(f"{name}:{line}: {what}" if isinstance(line, int) else f"{name}: {what}")
	print(f"FAIL: {len(findings)} Qt-5-incompatible QJsonValue::toInteger use(s)")
	return 1


def main(argv):
	parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
	parser.add_argument("--root", default=os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
	parser.add_argument("--build-dir")
	parser.add_argument("--cxx", default=os.environ.get("CXX") or shutil.which("c++") or "c++")
	parser.add_argument("--self-test", action="store_true")
	args = parser.parse_args(argv[1:])

	failures = self_test()
	for failure in failures:
		print(f"SELF-TEST FAIL: {failure}")
	if failures:
		return 1
	print("self-test: the unguarded .toInteger() fixture is flagged, jsonInteger() and a Qt 6 guard are not")
	if args.self_test:
		return 0
	root = os.path.abspath(args.root)
	if not os.path.isdir(root):
		print(f"error: not a directory: {root}", file=sys.stderr)
		return 2
	build_dir = os.path.abspath(args.build_dir or os.path.join(root, "build"))
	findings = check(root, build_dir, args.cxx)
	if findings:
		return report(findings)
	print("PASS: first-party code reaches JSON integers through jsonInteger(), and the affected translation unit compiles against Qt 5")
	return 0


if __name__ == "__main__":
	sys.exit(main(sys.argv))
