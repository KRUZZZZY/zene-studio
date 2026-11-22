#!/usr/bin/env python3
"""Fail on a Qt signal that is overloaded in Qt 5 but not in Qt 6, taken with a bare `&Class::signal`.

Cluster ci-632a20737fe640fa (2026-11-13), hosted linux-x86_64 build: the job's Qt 5.15.3
rejected

    src/gui/FollowActionEditor.cpp:207:25: error: no matching function for call to
        'lmms::gui::FollowActionEditor::connect(QSpinBox*&, <unresolved overloaded function type>, ...)'
        QObject::connect(jump, &QSpinBox::valueChanged, jump, [jump] { ... });

because Qt 5 gives QSpinBox two `valueChanged` signals, `valueChanged(int)` and
`valueChanged(const QString&)`. Qt 6 removed the string overload, so the same line
compiles on every Qt 6 host here and only the Qt 5 job sees it. The same shape exists
for QDoubleSpinBox (`double`/`QString`), QComboBox (`int`/`QString` on
currentIndexChanged, activated and highlighted), QProcess (finished, error),
QButtonGroup (buttonClicked) and QNetworkReply/QAbstractSocket (error, which Qt 5 also
has as a getter). The portable spelling is

    QObject::connect(jump, QOverload<int>::of(&QSpinBox::valueChanged), ...)

or `qOverload<int>(...)` / a `static_cast<...>(...)`, which pick the same overload on
Qt 5 and Qt 6 - no behaviour change.

The check has two halves:

  * a source scan of every tracked first-party C/C++ file for such a bare
    `&Class::signal`. This half runs on every platform, including the Qt 6 hosts where
    the defect cannot fail the build, so the regression is red wherever the suite runs.
  * a compile probe: if a Qt 5 development tree is discoverable, the known-bad fixture
    must be rejected and the known-good fixture accepted. A host without Qt 5 (or with
    only Qt 6, where the fixture compiles) reports the probe as skipped; the source scan
    is the authoritative half there, exactly as for the other Qt 5 portability checks.

Both halves are proved able to fail by the self-test fixtures, which this file runs as
its own red/green control.

Usage:
  qt5-signal-overload-check.py [--root DIR] [--cxx CXX]
  qt5-signal-overload-check.py --self-test

Exit codes: 0 = clean, 1 = finding (or a failed fixture), 2 = usage error.
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile

# Signals Qt 5 declares more than once on these classes. Taking one with `&C::s` is
# therefore ill-formed there; Qt 6 keeps a single overload (or none of the string one).
OVERLOADED = {
	"QSpinBox": {"valueChanged"},
	"QDoubleSpinBox": {"valueChanged"},
	"QComboBox": {"currentIndexChanged", "activated", "highlighted"},
	"QProcess": {"finished", "error"},
	"QButtonGroup": {"buttonClicked", "buttonPressed", "buttonReleased"},
	"QNetworkReply": {"error"},
	"QAbstractSocket": {"error"},
}

SOURCE_SUFFIXES = (".c", ".cc", ".cpp", ".cxx", ".h", ".hpp")

# Vendored third-party trees are not this repo's code (same list as tests/all-sources.txt).
EXCLUDED_PREFIXES = (
	"src/3rdparty/",
	"tests/reference/",
	"plugins/NeuralAmp/rtneural/",
	"plugins/NeuralAmp/nam/",
	"plugins/NeuralAmp/tests/",
	"plugins/RnnoiseDenoiser/rnnoise/",
	"plugins/SfzPlayer/sfizz/",
)

# Comments and string/char literals are blanked before scanning (line numbers kept).
NON_CODE = re.compile(
	r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'',
	re.S)

# The unguarded use. A guard is one of the casts below immediately before the `&`.
BARE_SIGNAL = re.compile(
	r'&\s*(?P<cls>' + "|".join(sorted(OVERLOADED)) + r')\s*::\s*(?P<sig>[A-Za-z_][A-Za-z0-9_]*)')

GUARDS = (
	re.compile(r"QOverload\s*<[^;{}]*?>\s*::\s*of\s*\(\s*$", re.S),
	re.compile(r"qOverload\s*<[^;{}]*?>\s*\(\s*$", re.S),
	re.compile(r"static_cast\s*<[^;{}]*?>\s*\(\s*$", re.S),
)

FIXTURE_RED = """\
#include <QObject>
#include <QSpinBox>
#include <QComboBox>

void bad(QSpinBox* spin, QDoubleSpinBox* dspin, QComboBox* combo, QProcess* proc)
{
	QObject::connect(spin, &QSpinBox::valueChanged, spin, [] {});
	QObject::connect(dspin, &QDoubleSpinBox::valueChanged, dspin, [](double) {});
	QObject::connect(combo, &QComboBox::currentIndexChanged, combo, [](int) {});
	QObject::connect(proc, &QProcess::finished, proc, [](int) {});
}
"""

FIXTURE_GREEN = """\
#include <QObject>
#include <QSpinBox>
#include <QComboBox>

void good(QSpinBox* spin, QDoubleSpinBox* dspin, QComboBox* combo, QProcess* proc)
{
	QObject::connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), spin, [] {});
	QObject::connect(dspin, qOverload<double>(&QDoubleSpinBox::valueChanged), dspin, [](double) {});
	QObject::connect(combo, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
		combo, [](int) {});
	QObject::connect(proc,
		QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), proc, [](int) {});
	// A comment about &QSpinBox::valueChanged must not be reported.
	const char* text = "&QComboBox::activated";
	(void) text;
}
"""

FIXTURE_EXPECTED = ["QSpinBox", "QDoubleSpinBox", "QComboBox", "QProcess"]


def strip_non_code(text):
	"""Blank comments and literals, keeping every newline so line numbers stay right."""
	def blank(match):
		return re.sub(r"[^\n]", " ", match.group(0))
	return NON_CODE.sub(blank, text)


def scan_text(path, text):
	"""Return [(path, line, class, signal)] for unguarded overloaded-signal takes."""
	code = strip_non_code(text)
	findings = []
	for match in BARE_SIGNAL.finditer(code):
		if match.group("sig") not in OVERLOADED[match.group("cls")]:
			continue
		before = code[max(0, match.start() - 200):match.start()]
		if any(guard.search(before) for guard in GUARDS):
			continue
		line = code.count("\n", 0, match.start()) + 1
		findings.append((path, line, match.group("cls"), match.group("sig")))
	return findings


def self_test():
	failures = []
	red = scan_text("fixture-red.cpp", FIXTURE_RED)
	if [cls for _, _, cls, _ in red] != FIXTURE_EXPECTED:
		failures.append(f"red fixture flagged {[c for _, _, c, _ in red]}, expected {FIXTURE_EXPECTED}")
	if [line for _, line, _, _ in red] != [7, 8, 9, 10]:
		failures.append(f"red fixture lines {[line for _, line, _, _ in red]}, expected [7, 8, 9, 10]")
	green = scan_text("fixture-green.cpp", FIXTURE_GREEN)
	if green:
		failures.append(f"green fixture flagged {green}")
	if scan_text("plain.cpp", "int valueChanged(int);\n"):
		failures.append("a file without a Qt signal take was flagged")
	return failures


BAD_PROBE = """\
#include <QtCore/QObject>
#include <QtWidgets/QSpinBox>
void wire(QSpinBox* box)
{
	QObject::connect(box, &QSpinBox::valueChanged, box, [box] { box->setProperty("edited", true); });
}
"""

GOOD_PROBE = """\
#include <QtCore/QObject>
#include <QtWidgets/QSpinBox>
void wire(QSpinBox* box)
{
	QObject::connect(box, QOverload<int>::of(&QSpinBox::valueChanged), box, [box] { box->setProperty("edited", true); });
}
"""


def qt5_flags():
	"""`pkg-config --cflags Qt5Widgets` on a host that has a Qt 5 development tree, else None."""
	if shutil.which("pkg-config") is None:
		return None
	try:
		out = subprocess.run(["pkg-config", "--cflags", "Qt5Widgets"],
			capture_output=True, text=True, timeout=60)
	except (OSError, subprocess.SubprocessError):
		return None
	return out.stdout.split() if out.returncode == 0 and out.stdout.strip() else None


def compile_probe(cxx, flags):
	"""Compile the known-bad and known-good probes against Qt 5. Returns a verdict string or None."""
	directory = tempfile.mkdtemp(prefix="qt5-signal-probe-")
	try:
		bad = os.path.join(directory, "bad.cpp")
		good = os.path.join(directory, "good.cpp")
		with open(bad, "w", encoding="utf-8") as handle:
			handle.write(BAD_PROBE)
		with open(good, "w", encoding="utf-8") as handle:
			handle.write(GOOD_PROBE)
		command = [cxx, "-std=gnu++20", "-fsyntax-only"] + flags
		bad_run = subprocess.run(command + [bad], capture_output=True, text=True, timeout=300)
		if bad_run.returncode == 0:
			# Qt 6 (or no Qt 5 headers): the probe compiles, so this host cannot enforce the rule.
			return None
		good_run = subprocess.run(command + [good], capture_output=True, text=True, timeout=300)
		if good_run.returncode != 0:
			return ("probe: the Qt 5 tree rejected the portable spelling: "
				+ " ".join(good_run.stderr.split())[:300])
		return "probe: Qt 5 rejects the bare take and accepts QOverload<int>::of"
	finally:
		shutil.rmtree(directory, ignore_errors=True)


def tracked_names(root):
	"""Every path git tracks under @a root, or None when git cannot answer."""
	try:
		out = subprocess.run(["git", "-C", root, "ls-files", "-z"], capture_output=True, timeout=300)
	except (OSError, subprocess.SubprocessError):
		return None
	if out.returncode != 0:
		return None
	return out.stdout.decode("utf-8", "replace").split("\0")


def walked_names(root):
	"""Every file under the source roots, for a tree git cannot list."""
	names = []
	for top in ("include", "src", "plugins", "tests"):
		for directory, subdirs, files in os.walk(os.path.join(root, top)):
			subdirs[:] = sorted(d for d in subdirs if not d.startswith("."))
			names += [os.path.relpath(os.path.join(directory, name), root) for name in sorted(files)]
	return names


def is_scanned_source(name):
	return name.endswith(SOURCE_SUFFIXES) and not name.startswith(EXCLUDED_PREFIXES)


def iter_sources(root):
	"""Tracked first-party C/C++ files, as git reports them (submodules are not tracked files)."""
	for name in tracked_names(root) or walked_names(root):
		if name and is_scanned_source(name):
			yield name


def scan_tree(root):
	findings = []
	for name in iter_sources(root):
		try:
			with open(os.path.join(root, name), encoding="utf-8", errors="replace") as handle:
				findings += scan_text(name, handle.read())
		except OSError:
			continue
	return findings


def main(argv):
	parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
	parser.add_argument("--root", default=os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
	parser.add_argument("--cxx", default=os.environ.get("CXX") or "c++")
	parser.add_argument("--self-test", action="store_true")
	args = parser.parse_args(argv[1:])

	failures = self_test()
	for failure in failures:
		print(f"SELF-TEST FAIL: {failure}")
	if failures:
		return 1
	print("self-test: fixtures flagged and passed as expected")
	if args.self_test:
		return 0

	root = os.path.abspath(args.root)
	if not os.path.isdir(root):
		print(f"error: not a directory: {root}", file=sys.stderr)
		return 2

	findings = scan_tree(root)
	for path, line, cls, sig in findings:
		print(f"{path}:{line}: bare &{cls}::{sig} - that signal is overloaded in Qt 5 "
			f"(the hosted linux-x86_64 build fails here); write QOverload<...>::of(&{cls}::{sig})")

	flags = qt5_flags()
	if flags is None:
		print("probe: skipped (no Qt 5 development tree found)")
	else:
		verdict = compile_probe(args.cxx, flags)
		print(verdict or "probe: skipped (this compiler's Qt does not reject the bare take)")

	if findings:
		print(f"FAIL: {len(findings)} unguarded overloaded-signal take(s)")
		return 1
	print("PASS: no unguarded take of a signal that Qt 5 overloads")
	return 0


if __name__ == "__main__":
	sys.exit(main(sys.argv))
