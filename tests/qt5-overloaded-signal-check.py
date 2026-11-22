#!/usr/bin/env python3
"""Fail on a pointer-to-member `connect` of a signal Qt 5 declares more than once.

Cluster ci-8fceff87a3151aad (2026-11-13). The hosted windows-arm64 job builds against
Qt 5.15 (msys2 clangarm64, `win32-clang-g++`), and its build stopped at

    src/gui/FollowActionEditor.cpp:207:11: error: no matching member function for call to 'connect'
        QObject::connect(jump, &QSpinBox::valueChanged, jump, [...]);

`QSpinBox::valueChanged` has two overloads in Qt 5.15 - `valueChanged(int)` and the
deprecated `valueChanged(const QString &)` - so `&QSpinBox::valueChanged` names an
overloaded function, not a member pointer, and template argument deduction for the PMF
`connect` fails. Qt 6 removed the QString overload, which is why the Qt 6 build on this
box (and the Qt 6 jobs) never noticed. The same trap exists for every Qt 5 signal that
kept a deprecated QString overload: QDoubleSpinBox, QComboBox (three of them),
QButtonGroup, QProcess, QCompleter, QSignalMapper, QSqlDriver and QTextBrowser.

The fix at the call site is the resolution the rest of the tree already uses:
`QOverload<int>::of(&QSpinBox::valueChanged)`, `qOverload<int>(...)`, or an explicit
`static_cast<...>(...)`. This check finds any connect argument that names one of those
signals without such a resolution, so a new call site cannot reintroduce the CI failure
on a Qt 6-only development host.

It reads sources only (no compiler, no Qt headers, no build), so it is deterministic and
runs wherever a python3 is. Before scanning the tree it runs its own red/green fixtures
(the red ones must be flagged at their real lines, the green ones - explicit resolutions,
an unambiguous same-named signal from another class, and a string-based connect - must
not), so a scanner that cannot fail fails.

Exit 0 = clean, 1 = findings (or a failed fixture).
"""

import os
import re
import sys

SCAN_DIRS = ("src", "include", "plugins", "tests")
SOURCE_SUFFIXES = (".h", ".hpp", ".hxx", ".cpp", ".cxx", ".cc")
EXCLUDED_DIRS = ("src/3rdparty", "tests/reference", "plugins/NeuralAmp/rtneural",
	"plugins/NeuralAmp/nam", "plugins/NeuralAmp/tests",
	"plugins/RnnoiseDenoiser/rnnoise", "plugins/SfzPlayer/sfizz")

#! Signals Qt 5.15 declares with a second, deprecated QString (or pointer/int) overload.
#! Built from /usr/include/qt/Qt*/q*.h of the Qt 5.15.19 on this box; QSocketNotifier's
#! second overload is private (Q_QDOC/BUILDING_QSOCKETNOTIFIER), so that one is NOT here:
#! a `&QSocketNotifier::activated` compile on Qt 5.15 exits 0.
QT5_OVERLOADED = {
	"QButtonGroup": {"buttonClicked", "buttonPressed", "buttonReleased", "buttonToggled"},
	"QComboBox": {"activated", "currentIndexChanged", "highlighted"},
	"QCompleter": {"activated", "highlighted"},
	"QDoubleSpinBox": {"valueChanged"},
	"QProcess": {"finished"},
	"QSignalMapper": {"mapped"},
	"QSpinBox": {"valueChanged"},
	"QSqlDriver": {"notification"},
	"QTextBrowser": {"highlighted"},
}

CONNECT = re.compile(r"\bconnect\s*\(")
MEMBER_POINTER = re.compile(r"&\s*(\w+)\s*::\s*(\w+)")
RESOLUTION = re.compile(
	r"(?:QOverload\s*<[^;{}]*?>\s*::\s*of|qOverload\s*<[^;{}]*?>|static_cast\s*<[^;{}]*?>)\s*\(\s*$")


def skip_string(text, i):
	"""Skip the string or character literal starting at i; return the index just past it.

	String bodies are blanked (newlines kept) so a `//` inside a literal is not a comment."""
	quote = text[i]
	out, i = [quote], i + 1
	while i < len(text) and text[i] != quote:
		if text[i] == "\\" and i + 1 < len(text):
			out.append("\n" if text[i + 1] == "\n" else "  ")
			i += 2
			continue
		out.append("\n" if text[i] == "\n" else " ")
		i += 1
	if i < len(text):
		out.append(quote)
		i += 1
	return "".join(out), i


def skip_line_comment(text, i):
	start = i
	while i < len(text) and text[i] != "\n":
		i += 1
	return " " * (i - start), i


def skip_block_comment(text, i):
	start = i + 2
	i = start
	while i < len(text) and not (text[i] == "*" and i + 1 < len(text) and text[i + 1] == "/"):
		i += 1
	end = min(i + 2, len(text))
	return "".join("\n" if text[j] == "\n" else " " for j in range(start, end)), end


def strip_comments(text):
	"""Blank out comments and string bodies, keeping newlines so line numbers hold."""
	out, i = [], 0
	while i < len(text):
		c = text[i]
		if c in "\"'":
			piece, i = skip_string(text, i)
		elif c == "/" and text[i + 1:i + 2] == "/":
			piece, i = skip_line_comment(text, i)
		elif c == "/" and text[i + 1:i + 2] == "*":
			piece, i = skip_block_comment(text, i)
		else:
			piece, i = c, i + 1
		out.append(piece)
	return "".join(out)



def statement_at(text, open_paren):
	"""The text of one call, from `(` to its balancing `)`, and the line it starts on."""
	depth = 0
	for i in range(open_paren, len(text)):
		if text[i] == "(":
			depth += 1
		elif text[i] == ")":
			depth -= 1
			if depth == 0:
				return text[open_paren:i + 1]
	return text[open_paren:]


def resolved(prefix):
	return bool(RESOLUTION.search(prefix[-300:]))


def scan_text(path, text):
	clean = strip_comments(text)
	findings = []
	for call in CONNECT.finditer(clean):
		# Skip the definition of a helper called connect (none in this tree); only calls matter.
		statement = statement_at(clean, call.end() - 1)
		for candidate in MEMBER_POINTER.finditer(statement):
			owner, signal = candidate.group(1), candidate.group(2)
			if signal not in QT5_OVERLOADED.get(owner, ()):
				continue
			if resolved(statement[:candidate.start()]):
				continue
			line = clean.count("\n", 0, call.start()) + 1
			findings.append((path, line, f"&{owner}::{signal}"))
	return findings


def iter_sources(root):
	for top in SCAN_DIRS:
		base = os.path.join(root, top)
		for directory, subdirs, files in os.walk(base):
			relative = os.path.relpath(directory, root)
			subdirs[:] = sorted(d for d in subdirs if not d.startswith(".") and
				not any(relative == os.path.normpath(e) or relative.startswith(os.path.normpath(e) + os.sep)
					for e in EXCLUDED_DIRS))
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
void f(QSpinBox* spin, QDoubleSpinBox* chance, QComboBox* box, QProcess* process)
{
\tQObject::connect(spin, &QSpinBox::valueChanged, spin, [spin] { spin->setProperty("edited", true); });
\tconnect(chance, &QDoubleSpinBox::valueChanged, this, [this](double) {});
\tconnect(box, &QComboBox::currentIndexChanged, this, [this](int) {});
\tconnect(box, &QComboBox::activated, this, [this] {});
\tconnect(process, &QProcess::finished, this, [this](int, QProcess::ExitStatus) {});
}
"""

FIXTURE_GREEN = """
void f(QSpinBox* spin, QDoubleSpinBox* chance, QComboBox* box, QSlider* slider, QLineEdit* edit)
{
\t// A comment naming &QSpinBox::valueChanged must not be a finding.
\tQObject::connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), spin, [spin] {});
\tconnect(chance, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) {});
\tconnect(box, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, [this](int) {});
\tconnect(slider, &QSlider::valueChanged, this, [this](int) {});
\tconnect(edit, &QLineEdit::textEdited, this, [this](const QString&) {});
\tconnect(box, SIGNAL(currentIndexChanged(int)), this, SLOT(update()));
}
"""

FIXTURE_EXPECTED = ["&QSpinBox::valueChanged", "&QDoubleSpinBox::valueChanged",
	"&QComboBox::currentIndexChanged", "&QComboBox::activated", "&QProcess::finished"]


def self_test():
	failures = []
	red = scan_text("fixture-red.cpp", FIXTURE_RED)
	got = [declaration for _, _, declaration in red]
	if got != FIXTURE_EXPECTED:
		failures.append(f"red fixture flagged {got}, expected {FIXTURE_EXPECTED}")
	lines = [line for _, line, _ in red]
	if lines != [4, 5, 6, 7, 8]:
		failures.append(f"red fixture lines {lines}, expected [4, 5, 6, 7, 8]")
	green = scan_text("fixture-green.cpp", FIXTURE_GREEN)
	if green:
		failures.append(f"green fixture flagged {green}")
	return failures


def main(argv):
	root = argv[1] if len(argv) > 1 else os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
	failures = self_test()
	for failure in failures:
		print(f"SELF-TEST FAIL: {failure}")
	if failures:
		return 1
	print("self-test: fixtures flagged and passed as expected")
	findings = scan_tree(root)
	for path, line, declaration in findings:
		print(f"{path}:{line}: pointer to the Qt 5 overloaded signal {declaration} without "
			f"QOverload/qOverload/static_cast; Qt 5.15 connect() cannot deduce it")
	if findings:
		print(f"FAIL: {len(findings)} unresolved Qt 5 overloaded signal(s); "
			f"name the overload, e.g. QOverload<int>::of(&QSpinBox::valueChanged)")
		return 1
	print("PASS: no connect names a Qt 5 overloaded signal without an explicit overload")
	return 0


if __name__ == "__main__":
	sys.exit(main(sys.argv))
