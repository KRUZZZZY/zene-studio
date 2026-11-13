#!/usr/bin/env python3
"""Fail when a first-party source names a signal that Qt 5 overloads without disambiguating it.

Cluster ci-c22070dd8cbbc7c0 (2026-11-13). The hosted `build / linux-arm64` job
(`.github/workflows/build.yml:304`) builds against the distribution's Qt 5
(5.15), where a signal can have two overloads, and Qt 6 declares only one:

    /usr/include/aarch64-linux-gnu/qt5/QtWidgets/qspinbox.h:108  void valueChanged(int);
    /usr/include/aarch64-linux-gnu/qt5/QtWidgets/qspinbox.h:112  void valueChanged(const QString &);

`QObject::connect(jump, &QSpinBox::valueChanged, jump, lambda)` therefore forms an
"unresolved overloaded function type" and the connect template argument cannot be
deduced - the job stopped with

    src/gui/FollowActionEditor.cpp:207:25: error: no matching function for call to
    'lmms::gui::FollowActionEditor::connect(QSpinBox*&, <unresolved overloaded
    function type>, QSpinBox*&, ...)'

while this box's Qt 6 tree compiled the same line. The fix is the disambiguated
form the rest of the tree uses (`src/gui/UndoHistoryPanel.cpp:90`):

    QOverload<int>::of(&QSpinBox::valueChanged)

The check reads sources only (no build, no Qt, no moc), so it is deterministic and
runs on every platform, including the Qt 6 hosts where the defect compiles. It
scans every first-party source for `&Q<Class>::<signal>` where Qt 5 publishes two
overloads of that signal (the pinned contract below, with its header lines) and
requires the member pointer to be inside a disambiguating `QOverload<...>::of(...)`,
`qOverload<...>(...)` or `static_cast<...>(...)` call. Before scanning the tree it
runs its own red/green fixtures, so a scanner that cannot fail fails instead.

Kinds of reuse this covers: `&QDoubleSpinBox::valueChanged` (double and QString,
qspinbox.h:174,178), `QComboBox::currentIndexChanged` / `activated` / `highlighted`
(currentIndexChanged has int and QString, qcombobox.h:236,240; activated and
highlighted have int and QString, qcombobox.h:232,234,245,247), `QProcess::finished`
(one-argument and two-argument, qprocess.h:297,299) and the int overloads of
`QButtonGroup::buttonClicked/buttonPressed/buttonReleased/buttonToggled`
(qbuttongroup.h:80-83,90-96). `QSocketNotifier::activated` is NOT listed: its
one-argument Qt 5 overload is compiled only under `Q_MOC_RUN`/`BUILDING_QSOCKETNOTIFIER`
(qsocketnotifier.h:77-84), so a member pointer names a single signal.

Usage:
  qt5-signal-overload-check.py [--root DIR]
  qt5-signal-overload-check.py --self-test

Exit codes: 0 = clean, 1 = finding (or a failed fixture), 2 = usage error.
"""

import argparse
import os
import re
import sys

SCAN_DIRS = ("include", "src", "plugins", "tests")
SOURCE_SUFFIXES = (".h", ".hpp", ".hxx", ".cpp", ".cxx", ".cc")

#! Qt 5.15 signals with two overloads, and the header lines that declare them. A
#! member pointer to one of these is ambiguous on Qt 5 and unambiguous on Qt 6.
QT5_OVERLOADED = {
	"QSpinBox": {
		"valueChanged": "int and QString (qspinbox.h:108,112)",
	},
	"QDoubleSpinBox": {
		"valueChanged": "double and QString (qspinbox.h:174,178)",
	},
	"QComboBox": {
		"currentIndexChanged": "int and QString (qcombobox.h:236,240)",
		"activated": "int and QString (qcombobox.h:232,245)",
		"highlighted": "int and QString (qcombobox.h:234,247)",
	},
	"QProcess": {
		"finished": "one-argument and two-argument (qprocess.h:297,299)",
	},
	"QButtonGroup": {
		"buttonClicked": "QAbstractButton* and int (qbuttongroup.h:80,90)",
		"buttonPressed": "QAbstractButton* and int (qbuttongroup.h:81,92)",
		"buttonReleased": "QAbstractButton* and int (qbuttongroup.h:82,94)",
		"buttonToggled": "QAbstractButton*,bool and int,bool (qbuttongroup.h:83,96)",
	},
}

#! The call that immediately encloses the member pointer, if it disambiguates it.
DISAMBIGUATED = re.compile(
	r"(?:QOverload\s*<[^<>;{}]*>\s*::of"
	r"|qOverload\s*<[^<>;{}]*>\s*::of"
	r"|qOverload\s*<[^<>;{}]*>"
	r"|static_cast\s*<[^;{}]*>)\s*\(\s*$")

MEMBER_POINTER = re.compile(r"&(Q[A-Za-z0-9_]+)::([A-Za-z0-9_]+)")

#! Comments and literals are blanked before scanning: a mention in prose, or the
#! text of an error string, is not a use of the signal.
NON_CODE = re.compile(
	r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'', re.S)

WINDOW = 400  #! characters before the '&' that may hold the enclosing call


def strip_non_code(text):
	"""Blank comments and literals, keeping every offset and line break."""
	pieces, last = [], 0
	for match in NON_CODE.finditer(text):
		pieces.append(text[last:match.start()])
		pieces.append(re.sub(r"[^\n]", " ", match.group(0)))
		last = match.end()
	pieces.append(text[last:])
	return "".join(pieces)


def disambiguated(code, amp):
	"""Is the member pointer at @a amp inside a disambiguating call?"""
	window = code[max(0, amp - WINDOW):amp]
	return bool(DISAMBIGUATED.search(window))


def scan_text(path, text):
	"""Findings in one file: (path, line, class, signal, why)."""
	code = strip_non_code(text)
	findings = []
	for match in MEMBER_POINTER.finditer(code):
		klass, signal = match.group(1), match.group(2)
		why = QT5_OVERLOADED.get(klass, {}).get(signal)
		if why is None or disambiguated(code, match.start()):
			continue
		line = code.count("\n", 0, match.start()) + 1
		findings.append((path, line, klass, signal, why))
	return findings


def iter_sources(root):
	for top in SCAN_DIRS:
		for directory, subdirs, files in os.walk(os.path.join(root, top)):
			subdirs[:] = sorted(d for d in subdirs if not d.startswith("."))
			for name in sorted(files):
				if name.endswith(SOURCE_SUFFIXES):
					yield os.path.join(directory, name)


def scan_tree(root):
	findings = []
	for path in iter_sources(root):
		with open(path, encoding="utf-8", errors="replace") as handle:
			findings += scan_text(os.path.relpath(path, root), handle.read())
	return findings


FIXTURE_RED = """
#include <QComboBox>
#include <QObject>
#include <QSpinBox>
void wire(QSpinBox* box, QComboBox* combo)
{
	QObject::connect(box, &QSpinBox::valueChanged, box, [box] { box->setValue(1); });
	QObject::connect(combo, &QComboBox::currentIndexChanged, combo, [](int) {});
}
"""

FIXTURE_GREEN = """
#include <QComboBox>
#include <QObject>
#include <QSpinBox>
//! A comment naming &QSpinBox::valueChanged is not a use.
static const char* kMention = "&QSpinBox::valueChanged";
void wire(QSpinBox* box, QComboBox* combo)
{
	QObject::connect(box, QOverload<int>::of(&QSpinBox::valueChanged), box, [box] { box->setValue(1); });
	QObject::connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), combo, [](int) {});
	QObject::connect(combo, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), combo,
		[](int) {});
	QObject::connect(combo, &QComboBox::currentTextChanged, combo, [](const QString&) {});
}
"""

#! Red fixture findings, in file order: (line, class, signal).
FIXTURE_EXPECTED = [(7, "QSpinBox", "valueChanged"), (8, "QComboBox", "currentIndexChanged")]


def self_test():
	failures = []
	red = scan_text("fixture-red.cpp", FIXTURE_RED)
	got = [(line, klass, signal) for _, line, klass, signal, _ in red]
	if got != FIXTURE_EXPECTED:
		failures.append(f"red fixture found {got}, expected {FIXTURE_EXPECTED}")
	green = scan_text("fixture-green.cpp", FIXTURE_GREEN)
	if green:
		failures.append(f"green fixture flagged {green}")
	prose = scan_text("prose.cpp", "// &QSpinBox::valueChanged in prose\n")
	if prose:
		failures.append(f"prose mention flagged {prose}")
	return failures


def main():
	parser = argparse.ArgumentParser(description="Qt 5 signal-overload portability check")
	parser.add_argument("--root", default=os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
	parser.add_argument("--self-test", action="store_true",
		help="run only the red/green fixtures (proves the scan can fail)")
	args = parser.parse_args()

	failures = self_test()
	for failure in failures:
		print(f"SELF-TEST FAIL: {failure}")
	if failures:
		return 1
	print("self-test: fixtures flagged and passed as expected")
	if args.self_test:
		return 0

	findings = scan_tree(args.root)
	for path, line, klass, signal, why in findings:
		print(f"{path}:{line}: &{klass}::{signal} is overloaded in Qt 5 ({why}); "
			f"disambiguate it with QOverload<...>::of(...) or the Qt 5 build stops")
	if findings:
		print(f"FAIL: {len(findings)} ambiguous signal member pointer(s)")
		return 1
	print("PASS: every Qt 5 overloaded signal member pointer is disambiguated")
	return 0


if __name__ == "__main__":
	sys.exit(main())
