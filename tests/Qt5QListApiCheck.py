#!/usr/bin/env python3
"""Fail when the Eq plug-in uses QList APIs that Qt 5 does not declare.

Cluster ci-151e338efff6843e (2026-11-14), hosted `build / windows-arm64` job: that job
builds against MSYS2's clangarm64 Qt 5.15, where QList is still the node-based Qt 4
container. It has no `QList(count, value)` constructor (that one is QVector's on Qt 5)
and no `data()`/`constData()` accessor, so

    plugins/Eq/EqSpectrumView.cpp:272:  m_spectrumBands( MAX_BANDS, 0.f )
    plugins/Eq/EqSpectrumView.cpp:296:  copySpectrum( m_spectrumBands.data(), energy )
    plugins/Eq/EqSpectrumView.cpp:315:  const float *bands = m_spectrumBands.data();

stopped the whole build:

    error: no matching constructor for initialization of 'QList<float>'
    note: candidate template ignored: deduced conflicting types for parameter
          'InputIterator' ('int' vs. 'float')
    error: no member named 'data' in 'QList<float>'

Qt 6 made QList the contiguous container, gave it both the size+value constructor and
data(), and made QVector an alias of QList - so every Qt 6 host here compiles the same
lines. QVector is the spelling with the size+value constructor and data() on BOTH:
Qt 5's QVector declares them, Qt 6's QVector is QList. It is the portable container for
the spectrum snapshot.

The check has two deterministic halves:

  * a source scan of plugins/Eq for a QList-typed name used with `.data()`/`.constData()`
    or constructed with two top-level arguments. It reads sources only, so it is red on
    the Qt 6 hosts where the defect compiles.
  * a compile probe: when pkg-config finds a Qt 5 tree, every hand-written plugins/Eq
    translation unit of build/compile_commands.json is compiled `-fsyntax-only` against
    those Qt 5 headers, which is the hosted job's compiler input. Before the fix that is
    the hosted failure; after it, all are clean.

Both halves are proved able to fail by this file's own red/green fixtures, and the probe
reports itself skipped (never passed) when the host has no Qt 5.

Usage:
  Qt5QListApiCheck.py [--root DIR] [--build-dir DIR] [--cxx CXX]
  Qt5QListApiCheck.py --self-test

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

PLUGIN_DIR = "plugins/Eq"
SOURCE_SUFFIXES = (".h", ".hpp", ".hxx", ".cpp", ".cxx", ".cc")

# Comments, string and char literals are blanked before the shape scan (line numbers kept).
NON_CODE = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'', re.S)
QLIST = re.compile(r"\bQList\s*<")
DECLARATOR = re.compile(r"\s+([A-Za-z_][A-Za-z0-9_]*)")
DATA_USE = r"\b%s\s*(?:->|\.)\s*(?:constData|data)\s*\("
CONSTRUCTION = r"\b%s\s*\("

# A Qt 5 tree must reject the QList take and accept the QVector one.
BAD_PROBE = """\
#include <QList>
int main()
{
	QList<float> bands(8, 0.f);
	return bands.data() == nullptr;
}
"""
GOOD_PROBE = """\
#include <QVector>
int main()
{
	QVector<float> bands(8, 0.f);
	return bands.data() == nullptr;
}
"""

# The source scan must flag the QList member and not the QVector one.
FIXTURE_BAD = """\
#include <QList>
struct View
{
	QList<float> m_spectrumBands;
	View() : m_spectrumBands(4, 0.f) {}
	float* bands() { return m_spectrumBands.data(); }
};
"""
FIXTURE_GOOD = """\
#include <QVector>
struct View
{
	QVector<float> m_spectrumBands;
	View() : m_spectrumBands(4, 0.f) {}
	float* bands() { return m_spectrumBands.data(); }
};
"""

QT_MODULES = {"QtCore", "QtGui", "QtWidgets", "QtXml", "QtSvg", "QtNetwork", "QtDBus", "QtTest"}
RANGE_MARKERS = (".begin()", ".end()", ".cbegin()", ".cend()", "nullptr", "NULL")


def blank_non_code(text):
	"""Comments and literals become spaces; every newline and offset is kept."""
	return NON_CODE.sub(lambda match: re.sub(r"[^\n]", " ", match.group(0)), text)


def skip_template(text, start):
	"""Index of the '>' closing the '<' at @a start, or -1."""
	depth = 0
	for index in range(start, len(text)):
		if text[index] == "<":
			depth += 1
		elif text[index] == ">":
			depth -= 1
			if depth == 0:
				return index
	return -1


def matching_paren(text, start):
	"""Index of the ')' closing the '(' at @a start, or -1."""
	depth = 0
	for index in range(start, len(text)):
		if text[index] == "(":
			depth += 1
		elif text[index] == ")":
			depth -= 1
			if depth == 0:
				return index
	return -1


def split_arguments(text):
	"""Top-level comma-separated arguments of an argument list, whitespace-stripped."""
	parts, depth, current = [], 0, ""
	for char in text:
		if char in "([{<":
			depth += 1
		elif char in ")]}>":
			depth -= 1
		if char == "," and depth == 0:
			parts.append(current.strip())
			current = ""
		else:
			current += char
	parts.append(current.strip())
	return parts


def declared_qlist_names(text):
	"""Names declared with QList<T> (members, locals and parameters)."""
	names = set()
	index = 0
	while True:
		match = QLIST.search(text, index)
		if match is None:
			return names
		close = skip_template(text, match.end() - 1)
		if close < 0:
			return names
		declarator = DECLARATOR.match(text, close + 1)
		if declarator is not None:
			names.add(declarator.group(1))
		index = close + 1


def line_of(text, offset):
	return text.count("\n", 0, offset) + 1


def data_findings(name, code, qlist_names):
	findings = []
	for qlist_name in sorted(qlist_names):
		for match in re.finditer(DATA_USE % re.escape(qlist_name), code):
			findings.append((name, line_of(code, match.start()),
				"QList has no data() in Qt 5; use QVector"))
	return findings


def is_two_argument_construction(code, match):
	"""Whether the call at @a match is `name(a, b)` with two plain top-level arguments."""
	before = code[:match.start()].rstrip()
	if not before or before[-1] not in ",:(":
		return False
	close = matching_paren(code, match.end() - 1)
	after = code[close + 1:].lstrip() if close >= 0 else ""
	if not after or after[0] not in ";{,":
		return False
	arguments = split_arguments(code[match.end():close])
	if len(arguments) != 2:
		return False
	return not any(marker in argument for argument in arguments for marker in RANGE_MARKERS)


def construction_findings(name, code, qlist_names):
	findings = []
	for qlist_name in sorted(qlist_names):
		for match in re.finditer(CONSTRUCTION % re.escape(qlist_name), code):
			if is_two_argument_construction(code, match):
				findings.append((name, line_of(code, match.start()),
					"QList(count, value) does not exist in Qt 5; use QVector"))
	return findings


def scan_text(name, text, qlist_names=None):
	"""(path, line, what) for QList APIs Qt 5 does not declare.

	@a qlist_names carries the declarations of the whole component, so a member declared
	in a header is still recognised where the implementation uses it.
	"""
	code = blank_non_code(text)
	if qlist_names is None:
		qlist_names = declared_qlist_names(code)
	return (data_findings(name, code, qlist_names)
		+ construction_findings(name, code, qlist_names))


def read_text(path):
	try:
		with open(path, encoding="utf-8", errors="replace") as handle:
			return blank_non_code(handle.read())
	except OSError:
		return None


def scan_plugin(root):
	"""The whole plug-in scanned as one component, so header declarations reach their uses."""
	directory = os.path.join(root, PLUGIN_DIR)
	if not os.path.isdir(directory):
		return []
	codes = {PLUGIN_DIR + "/" + entry: read_text(os.path.join(directory, entry))
		for entry in sorted(os.listdir(directory)) if entry.endswith(SOURCE_SUFFIXES)}
	codes = {name: code for name, code in codes.items() if code is not None}
	qlist_names = set()
	for code in codes.values():
		qlist_names |= declared_qlist_names(code)
	findings = []
	for name, code in codes.items():
		findings += scan_text(name, code, qlist_names)
	return findings


def self_test():
	failures = []
	if not scan_text("fixture-bad.cpp", FIXTURE_BAD):
		failures.append("the scan did not report the QList<float>(count, value) fixture")
	if scan_text("fixture-good.cpp", FIXTURE_GOOD):
		failures.append("the scan reported the QVector<float> fixture")
	return failures


def run(command, cwd=None, timeout=300):
	try:
		return subprocess.run(command, cwd=cwd, capture_output=True, text=True, timeout=timeout)
	except (OSError, subprocess.SubprocessError):
		return None


def qt5_flags():
	"""Compile flags of a Qt 5 development tree, or None when pkg-config has none."""
	if not shutil.which("pkg-config"):
		return None
	if run(["pkg-config", "--exists", "Qt5Widgets"]).returncode != 0:
		return None
	cflags = run(["pkg-config", "--cflags", "Qt5Widgets", "Qt5Xml", "Qt5Network"])
	if cflags is None or cflags.returncode != 0:
		return None
	return shlex.split(cflags.stdout)


def compile_probe(cxx, flags, workdir):
	"""Only a Qt 5 tree must reject BAD_PROBE and accept GOOD_PROBE; None when it accepts both."""
	probes = []
	for name, text in (("bad.cpp", BAD_PROBE), ("good.cpp", GOOD_PROBE)):
		path = os.path.join(workdir, name)
		with open(path, "w", encoding="utf-8") as handle:
			handle.write(text)
		probes.append(run([cxx, "-std=gnu++20", "-fsyntax-only"] + flags + [path]))
	if probes[0] is None or probes[0].returncode == 0:
		return None
	if probes[1] is None or probes[1].returncode != 0:
		return "probe: the Qt 5 tree rejected QVector(count, value)"
	return "probe: Qt 5 rejects QList(count, value).data() and accepts QVector"


def is_qt6_include(directory):
	parts = [p for p in re.split(r"[\\/]+", directory.rstrip("\\/")) if p]
	return "qt6" in parts or (parts and parts[-1] in QT_MODULES and any("qt6" in p for p in parts))


def qt5_command(arguments, flags, source):
	"""A recorded Qt 6 compile line retargeted at the Qt 5 headers."""
	out = []
	index = 0
	while index < len(arguments):
		argument = arguments[index]
		consumes_value = index + 1 < len(arguments) and argument in ("-isystem", "-I", "-o", "-c")
		if argument in ("-o", "-c"):
			index += 2
		elif consumes_value and argument in ("-isystem", "-I") and is_qt6_include(arguments[index + 1]):
			index += 2
		elif consumes_value and argument in ("-isystem", "-I"):
			out += [argument, arguments[index + 1]]
			index += 2
		else:
			out.append(argument)
			index += 1
	return out + flags + ["-fsyntax-only", source]


def source_file(entry, root):
	"""The entry's source path when it is a hand-written Eq file, else None.

	Only the checked-in plugins/Eq sources are tested. The generated moc_*/qrc_* units live
	in the build tree and are produced by the Qt 6 moc of this build, so compiling them
	against Qt 5 headers would test this build's own generator, not the product source.
	"""
	source = entry.get("file", "").replace("\\", "/")
	if not source.endswith(SOURCE_SUFFIXES):
		return None
	absolute = source if os.path.isabs(source) else os.path.join(entry.get("directory", root), source)
	absolute = os.path.abspath(os.path.normpath(absolute))
	plugin_root = os.path.join(os.path.abspath(root), PLUGIN_DIR) + os.sep
	if not absolute.startswith(plugin_root):
		return None
	return absolute if os.path.isfile(absolute) else None


def entries_of(database):
	with open(database, encoding="utf-8", errors="replace") as handle:
		return json.load(handle)


def probe_diagnostic(result):
	"""The error/note lines of a failed probe, bounded."""
	return "\n".join(line for line in (result.stdout + result.stderr).splitlines()
		if "error:" in line or "note:" in line)[:2000]


def probe_entry(entry, root, flags):
	source = source_file(entry, root)
	if source is None:
		return None
	arguments = entry.get("command") and shlex.split(entry["command"]) or list(entry.get("arguments") or [])
	result = run(qt5_command(arguments, flags, source), entry.get("directory") or root)
	if result is None:
		return f"{source}: the Qt 5 probe could not run"
	if result.returncode != 0:
		return f"{source}: Qt 5 rejected it\n{probe_diagnostic(result)}"
	return None


def probe_translation_units(root, build_dir, flags):
	"""Compile every hand-written Eq translation unit against Qt 5."""
	database = os.path.join(build_dir, "compile_commands.json")
	if not os.path.isfile(database):
		return [f"no compile database at {database}"]
	results = [probe_entry(entry, root, flags) for entry in entries_of(database)]
	failures = [result for result in results if result]
	if not results:
		failures.append(f"the compile database records no {PLUGIN_DIR} translation unit")
	return failures


def report(findings):
	for name, line, what in findings:
		print(f"{name}:{line}: {what}" if isinstance(line, int) else f"{name}: {what}")
	print(f"FAIL: {len(findings)} Qt-5-incompatible QList use(s)")
	return 1


def check(root, build_dir, cxx, workdir):
	"""Every finding, with the source scan and the Qt 5 probe as independent halves."""
	findings = scan_plugin(root)
	flags = qt5_flags()
	if flags is None:
		print("probe: skipped (no Qt 5 development tree found)")
		return findings
	print(compile_probe(cxx, flags, workdir) or "probe: skipped (this Qt accepts the QList take)")
	for failure in probe_translation_units(root, build_dir, flags):
		headline, _, diagnostic = failure.partition("\n")
		findings.append((headline, None, diagnostic))
	return findings


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
	print("self-test: the QList fixture is flagged and the QVector fixture is not")
	if args.self_test:
		return 0
	root = os.path.abspath(args.root)
	if not os.path.isdir(root):
		print(f"error: not a directory: {root}", file=sys.stderr)
		return 2
	build_dir = os.path.abspath(args.build_dir or os.path.join(root, "build"))
	workdir = tempfile.mkdtemp(prefix="qt5-qlist-")
	try:
		findings = check(root, build_dir, args.cxx, workdir)
	finally:
		shutil.rmtree(workdir, ignore_errors=True)
	if findings:
		return report(findings)
	print("PASS: the Eq plugin uses no QList API Qt 5 lacks, and every Eq translation unit compiles against Qt 5")
	return 0


if __name__ == "__main__":
	sys.exit(main(sys.argv))
