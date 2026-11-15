#!/usr/bin/env python3
"""Regression for cluster ci-539b88713935565b: first-party sources must build on a Qt whose
QThread::create() factory is compiled out.

The hosted `mingw64` job builds against the tobydox PPA's Qt 5.15.6, whose
`QtCore/qtcore-config.h` carries `QT_FEATURE_cxx11_future -1` (the PPA package is pinned by
`.github/workflows/deps-ubuntu-24.04-mingw.txt`). Qt's own `qthread.h` wraps the factory in

    #if QT_CONFIG(cxx11_future)
    template <typename Function, typename... Args>
    static QThread *create(Function &&f, Args &&... args);
    ...
    #endif // QT_CONFIG(cxx11_future)

so on that Qt the name does not exist. Two fork-new test files used it and the job stopped:

    tests/src/gui/Warp/SampleClipWarpEditTest.cpp:433:58: error: 'create' is not a member of 'QThread'
    tests/src/gui/Warp/SampleClipWarpEditTest.cpp:463:58: error: 'create' is not a member of 'QThread'
    tests/src/gui/Warp/SampleClipWarpEditTest.cpp:551:56: error: 'create' is not a member of 'QThread'

with `tests/src/core/WarpMarkersTest.cpp:131` the same defect one target away (the job stopped
before it got there). `tests/src/core/TestWorkerThread.h` is the portable spelling: a QThread
subclass whose `run()` calls the callable, with `start()`/`wait()` unchanged - QThread::create()
is only that, plus std::async machinery.

The check has two deterministic halves:

  * a source scan of the first-party tree for `QThread::create`, which is red on the Qt 6 hosts
    too (that is the point: a host with the factory never notices);
  * a compile probe: every recorded translation unit that used the factory is compiled from this
    build's `compile_commands.json` against real Qt 5 headers plus a SHADOW
    `QtCore/qtcore-config.h` whose `cxx11_future` feature is off - the failing platform's
    configuration, under which Qt's headers stay coherent. Two controls keep it honest: the old
    spelling must NOT compile under the shadow, and the host spelling (`TestWorkerThread.h`) must
    compile under it. The old spelling must still compile on a host whose Qt has the feature, so
    the shadow - not the fixture - is what hides it.

The probe reports itself skipped (never passed) when the host has no Qt 5 development tree; the
source scan still measures there.

Usage:
  qt5-worker-thread-check.py --build-dir <build> [--root <tree>] [--moc <moc>]
  qt5-worker-thread-check.py --self-test
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

SCAN_DIRS = ("src", "include", "plugins", "tests")
SOURCE_SUFFIXES = (".h", ".hpp", ".hxx", ".cpp", ".cxx", ".cc")
EXCLUDED = (
    "src/3rdparty/",
    "tests/reference/",
    "plugins/NeuralAmp/rtneural/",
    "plugins/NeuralAmp/nam/",
    "plugins/NeuralAmp/tests/",
    "plugins/RnnoiseDenoiser/rnnoise/",
    "plugins/SfzPlayer/sfizz/",
)
FACTORY = re.compile(r"\bQThread\s*::\s*create\s*\(")
NON_CODE = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'', re.S)

# The two translation units the hosted job found (one build, one next in the queue). A rename is
# a failure, not a skip: the check must keep watching the code that had the defect.
AFFECTED = (
    "tests/src/gui/Warp/SampleClipWarpEditTest.cpp",
    "tests/src/core/WarpMarkersTest.cpp",
)

OLD_SPELLING = """\
#include <QThread>
#include <memory>
int main()
{
	std::unique_ptr<QThread> worker(QThread::create([] {}));
	return worker ? 0 : 1;
}
"""

NEW_SPELLING = """\
#include "TestWorkerThread.h"
#include <memory>
int main()
{
	auto worker = std::make_unique<lmms::test::TestWorkerThread>([] {});
	worker->start();
	worker->wait();
	return 0;
}
"""

# Compiles only where the feature is ON, so the same fixture is the shadow's negative control
# and the host's positive one.
FEATURE_PROBE = """\
#include <QtCore/qglobal.h>
static_assert(QT_CONFIG(cxx11_future), "this Qt was configured without cxx11_future");
int main() { return 0; }
"""

FIXTURE_BAD = """\
void start()
{
	auto* worker = QThread::create([] {});
	worker->start();
}
"""

FIXTURE_GOOD = """\
void start(TestWorkerThread* worker)
{
	worker->start();
}
"""


def blank_non_code(text):
	"""Comments and literals become spaces; every newline and offset is kept."""
	return NON_CODE.sub(lambda match: re.sub(r"[^\n]", " ", match.group(0)), text)


def factory_findings(name, text):
	code = blank_non_code(text)
	return [(name, code.count("\n", 0, match.start()) + 1)
		for match in FACTORY.finditer(code)]


def self_test():
	failures = []
	if not factory_findings("fixture-bad.cpp", FIXTURE_BAD):
		failures.append("the scan did not flag the QThread::create fixture")
	if factory_findings("fixture-good.cpp", FIXTURE_GOOD):
		failures.append("the scan flagged the QThread subclass fixture")
	if factory_findings("fixture-comment.cpp", "// QThread::create(...)\n"):
		failures.append("the scan flagged a comment")
	return failures


def run(command, cwd=None, timeout=300):
	try:
		return subprocess.run(command, cwd=cwd, capture_output=True, text=True, timeout=timeout)
	except (OSError, subprocess.SubprocessError):
		return None


def tracked_sources(root):
	"""The first-party sources a supported build compiles (git ls-files, same scope as Gate 9)."""
	result = run(["git", "-C", root, "ls-files", "--", *SCAN_DIRS])
	if result is None or result.returncode != 0:
		raise SystemExit("FAIL: cannot list tracked sources under " + root)
	paths = []
	for path in result.stdout.splitlines():
		if not path.endswith(SOURCE_SUFFIXES):
			continue
		if any(path.startswith(prefix) for prefix in EXCLUDED):
			continue
		paths.append(path)
	return paths


def scan_tree(root):
	findings = []
	for path in tracked_sources(root):
		try:
			with open(os.path.join(root, path), encoding="utf-8", errors="replace") as handle:
				text = handle.read()
		except OSError:
			continue
		findings += factory_findings(path, text)
	return findings


def split_command(command):
	return shlex.split(command)


def entry_arguments(entry):
	if entry.get("command"):
		return split_command(entry["command"])
	return list(entry.get("arguments") or [])


def strip_qt6_includes(arguments):
	"""A recorded Qt 6 compile line without its Qt 6 include paths; everything else is kept."""
	out, index = [], 0
	while index < len(arguments):
		arg = arguments[index]
		paired = arg in ("-isystem", "-I", "/I", "/external:I") and index + 1 < len(arguments)
		if paired:
			value = arguments[index + 1]
			if not is_qt6_path(value):
				out += [arg, value]
			index += 2
			continue
		if arg.startswith(("-isystem", "-I", "/I", "/external:I")) and arg not in ("-I", "/I"):
			value = arg.split(":", 1)[1] if arg.lower().startswith("/external:i") else arg[2:]
			if not is_qt6_path(value):
				out.append(arg)
			index += 1
			continue
		out.append(arg)
		index += 1
	return out


def is_qt6_path(path):
	parts = [part for part in re.split(r"[\\/]+", path.lower()) if part]
	return "qt6" in parts


def compile_flags(arguments):
	out, index = [], 0
	while index < len(arguments):
		arg = arguments[index]
		if arg in ("-o", "/Fo"):
			index += 2
			continue
		if arg.startswith("/Fo"):
			index += 1
			continue
		if arg in ("-c", "/c"):
			index += 1
			continue
		out.append(arg)
		index += 1
	return out


def include_dirs(arguments):
	out, index = [], 0
	while index < len(arguments):
		arg = arguments[index]
		if arg in ("-I", "-isystem", "/I") and index + 1 < len(arguments):
			out.append(arguments[index + 1])
			index += 2
			continue
		if arg.startswith(("-I", "/I")) and len(arg) > 2:
			out.append(arg[2:])
		index += 1
	return out


def source_of(entry):
	path = entry["file"]
	if not os.path.isabs(path):
		path = os.path.join(entry.get("directory", "."), path)
	return os.path.normpath(path)


class Toolchain:
	"""One recorded compile line, reused for the shadow and the host compiles."""

	def __init__(self, entry):
		self.directory = entry["directory"]
		self.source = source_of(entry)
		self.args = compile_flags(strip_qt6_includes(entry_arguments(entry)))
		if not self.args:
			raise SystemExit("FAIL: %s has no compile command" % entry["file"])
		self.msvc = os.path.basename(self.args[0]).lower() in ("cl", "cl.exe")
		self.syntax_only = "/Zs" if self.msvc else "-fsyntax-only"
		self.include_flag = "/I" if self.msvc else "-I"
		self.includes = include_dirs(self.args)

	def command(self, extra, source):
		"""@a extra entries are include dirs, except a literal flag (leading '-') which is kept as is."""
		flags = list(self.args[1:])
		# The recorded line ends with the TU it compiles; it is replaced by @a source.
		while flags and os.path.normpath(flags[-1]) in (os.path.normpath(source), os.path.normpath(self.source)):
			flags.pop()
		prefix = [self.args[0], self.syntax_only]
		for entry in extra:
			prefix.append(entry if entry.startswith("-") else self.include_flag + entry)
		return prefix + flags + [source]
	def moc_include(self, source, tempdir, moc):
		"""Run moc for a TU that includes its own .moc; (include dir or None, detail)."""
		with open(source, encoding="utf-8", errors="replace") as handle:
			match = re.search(r'#include\s+"([\w.+-]+\.moc)"', handle.read())
		if match is None:
			return None, ""
		if not moc:
			return None, "needs %s but no moc was given" % match.group(1)
		output = os.path.join(tempdir, match.group(1))
		command = [moc, source, "-o", output]
		for directory in self.includes:
			command += ["-I", directory]
		produced = run(command, self.directory)
		if produced is None or produced.returncode != 0:
			detail = "moc could not run" if produced is None else (produced.stderr or produced.stdout).strip()
			return None, "moc failed: " + (detail.splitlines()[-1] if detail else "no output")
		return tempdir, ""

	def compile_source(self, source, extra_includes, tempdir, moc):
		includes = list(extra_includes)
		moc_dir, detail = self.moc_include(source, tempdir, moc)
		if moc_dir is None and detail:
			return False, detail
		if moc_dir:
			includes.append(moc_dir)
		result = run(self.command(includes, source), self.directory)
		if result is None:
			return False, "the compiler could not run"
		if result.returncode == 0:
			return True, ""
		lines = [line for line in (result.stderr or result.stdout).splitlines() if "error" in line.lower()]
		return False, "; ".join(lines[:3]) or "compile failed (exit %d)" % result.returncode


def qt5_flags():
	"""Compile flags of a Qt 5 development tree, or None when pkg-config has none."""
	if not shutil.which("pkg-config"):
		return None
	modules = ["Qt5Core", "Qt5Gui", "Qt5Widgets", "Qt5Xml", "Qt5Network", "Qt5Test", "Qt5DBus"]
	if run(["pkg-config", "--exists", *modules]) is None or run(["pkg-config", "--exists", *modules]).returncode != 0:
		return None
	cflags = run(["pkg-config", "--cflags", *modules])
	if cflags is None or cflags.returncode != 0:
		return None
	flags = shlex.split(cflags.stdout)
	if run(["pkg-config", "--exists", "Qt5Svg"]).returncode == 0:
		svg = run(["pkg-config", "--cflags", "Qt5Svg"])
		if svg is not None and svg.returncode == 0:
			flags += shlex.split(svg.stdout)
	return flags


def qt5_config(flags):
	for directory in flags:
		if directory.startswith("-I"):
			candidate = os.path.join(directory[2:], "QtCore", "qtcore-config.h")
			if os.path.isfile(candidate):
				return candidate
	return None


def qt5_moc(fallback):
	if shutil.which("pkg-config"):
		result = run(["pkg-config", "--variable=host_bins", "Qt5Core"])
		if result is not None and result.returncode == 0 and result.stdout.strip():
			candidate = os.path.join(result.stdout.strip(), "moc")
			if os.path.isfile(candidate) and os.access(candidate, os.X_OK):
				return candidate
	return fallback or None


def write_shadow(config, directory):
	"""A QtCore/qtcore-config.h with the cxx11_future feature off, as Qt's configure writes one."""
	with open(config, encoding="utf-8", errors="replace") as handle:
		text = handle.read()
	if not re.search(r"^#\s*define\s+QT_FEATURE_cxx11_future\b", text, re.M):
		raise SystemExit("FAIL: %s declares no QT_FEATURE_cxx11_future" % config)
	text = re.sub(r"^#\s*define\s+QT_FEATURE_cxx11_future\b.*$", "#define QT_FEATURE_cxx11_future -1",
		text, flags=re.M)
	destination = os.path.join(directory, "QtCore", "qtcore-config.h")
	os.makedirs(os.path.dirname(destination), exist_ok=True)
	with open(destination, "w", encoding="utf-8") as handle:
		handle.write(text)
	return destination


def load_build(build_dir):
	commands = os.path.join(os.path.abspath(build_dir), "compile_commands.json")
	if not os.path.isfile(commands):
		return "no compile_commands.json in " + build_dir
	with open(commands, encoding="utf-8", errors="replace") as handle:
		entries = json.load(handle)
	if not entries:
		return "compile_commands.json is empty"
	return entries


class Report:
	"""Collects the probe's cases so one failed compile does not hide the next."""

	def __init__(self):
		self.failures = []

	def __call__(self, ok, label, detail=""):
		print("%s %s%s" % ("PASS" if ok else "FAIL", label, "" if ok else ": " + detail))
		if not ok:
			self.failures.append(label)


def write_fixture(tempdir, name, text):
	path = os.path.join(tempdir, name)
	with open(path, "w", encoding="utf-8") as handle:
		handle.write(text)
	return path


def scan(root, report):
	findings = scan_tree(root)
	for path, line in findings:
		print("%s:%d: uses QThread::create(), which a Qt built without cxx11_future does not declare;"
			" use tests/src/core/TestWorkerThread.h" % (path, line))
	report(not findings, "no first-party source uses QThread::create()")


def shadow_cases(toolchain, shadow, flags, tempdir, report):
	"""The two negative controls and the portable header's positive case."""
	include = [shadow] + flags
	feature = write_fixture(tempdir, "feature.cpp", FEATURE_PROBE)
	ok, detail = toolchain.compile_source(feature, include, tempdir, None)
	report(not ok, "the shadow really is future-less (QT_CONFIG(cxx11_future) must be false)",
		"it compiled, so the probe does not detect the feature: " + detail)
	old = write_fixture(tempdir, "old.cpp", OLD_SPELLING)
	ok, detail = toolchain.compile_source(old, include, tempdir, None)
	report(not ok, "QThread::create() is absent under the shadow (the fixture can fail)",
		"it compiled, so the shadow does not hide the factory: " + detail)
	new = write_fixture(tempdir, "new.cpp", NEW_SPELLING)
	source_root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "src", "core")
	ok, detail = toolchain.compile_source(new, [shadow, source_root] + flags, tempdir, None)
	report(ok, "tests/src/core/TestWorkerThread.h builds on a Qt without cxx11_future", detail)


def host_control(toolchain, flags, tempdir, report):
	"""A Qt that HAS the feature must still accept the old spelling, so the shadow is the cause."""
	feature = write_fixture(tempdir, "host-feature.cpp", FEATURE_PROBE)
	available, _ = toolchain.compile_source(feature, flags, tempdir, None)
	if not available:
		print("SKIP this host's Qt has no cxx11_future, so the host control does not exist here")
		return
	old = write_fixture(tempdir, "host-old.cpp", OLD_SPELLING)
	ok, detail = toolchain.compile_source(old, flags, tempdir, None)
	report(ok, "QThread::create() still compiles on a Qt that has the feature", detail)


def compile_affected(root, by_source, control, shadow, flags, tempdir, moc, report):
	for relative in AFFECTED:
		entry = by_source.get(os.path.normpath(os.path.join(root, relative)))
		if entry is None:
			report(False, relative + " compiles on a Qt without cxx11_future",
				"not in compile_commands.json (was it renamed?)")
			continue
		affected = Toolchain(entry)
		if affected.args[0] != control.args[0]:
			print("SKIP %s: its recorded compiler differs from the control's" % relative)
			continue
		ok, detail = affected.compile_source(source_of(entry), [shadow] + flags, tempdir, moc)
		report(ok, relative + " compiles on a Qt without cxx11_future", detail)


def run_probe(root, build_dir, moc, report):
	entries = load_build(build_dir)
	if isinstance(entries, str):
		report(False, "the build tree has a compilation database", entries)
		return
	by_source = {source_of(entry): entry for entry in entries}
	control_entry = by_source.get(os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(build_dir)),
		"tests/src/core/WarpMarkersTest.cpp")))
	if control_entry is None:
		report(False, "tests/src/core/WarpMarkersTest.cpp is in compile_commands.json",
			"the control translation unit is missing (was it renamed?)")
		return
	flags = qt5_flags()
	if flags is None:
		print("probe: skipped (no Qt 5 development tree found)")
		return
	config = qt5_config(flags)
	if config is None:
		print("probe: skipped (no Qt 5 qtcore-config.h found)")
		return
	control = Toolchain(control_entry)
	with tempfile.TemporaryDirectory(prefix="zene-qt5-worker-") as tempdir:
		shadow = os.path.join(tempdir, "shadow")
		write_shadow(config, shadow)
		moc_tool = qt5_moc(moc)
		shadow_cases(control, shadow, flags, tempdir, report)
		host_control(control, flags, tempdir, report)
		compile_affected(root, by_source, control, shadow, flags, tempdir, moc_tool, report)


def main(argv):
	if "--self-test" in argv[1:]:
		failures = self_test()
		for failure in failures:
			print("FAIL", failure)
		if failures:
			return 1
		print("PASS: the source scan flags the factory spelling and not the portable one")
		return 0

	parser = argparse.ArgumentParser()
	parser.add_argument("--root", default=os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
	parser.add_argument("--build-dir", required=True)
	parser.add_argument("--moc", default="")
	options = parser.parse_args(argv[1:])
	report = Report()
	scan(os.path.abspath(options.root), report)
	run_probe(os.path.abspath(options.root), options.build_dir, options.moc, report)
	if report.failures:
		print("FAIL: %d worker-thread portability case(s) failed: %s"
			% (len(report.failures), "; ".join(report.failures)))
		return 1
	print("PASS: first-party sources build on a Qt without QThread::create()")
	return 0


if __name__ == "__main__":
	sys.exit(main(sys.argv))
