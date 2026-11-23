#!/usr/bin/env python3
"""Regression for cluster ci-64efb7585b33898b: two Qt5/Clang portability defects in C++ test sources.

Cluster ci-64efb7585b33898b (2026-11), hosted windows-arm64 build (clang, Qt 5.15),
two test translation units failed to compile although the local Qt6/GCC build is green.

(a) `QVERIFY(peer.bind(QHostAddress::LocalHost, 0));`
    (tests/src/core/LinkAudioStreamTestSupport.h, ControlOscServerTest.cpp)

        error: call to member function 'bind' is ambiguous

    Before Qt 6.2 QAbstractSocket has only bind(const QHostAddress&, quint16 = 0,
    BindMode = Default) and bind(quint16 = 0, BindMode = Default).  The unscoped
    enum QHostAddress::SpecialAddress converts to quint16 (standard conversion)
    or to QHostAddress (user-defined) and the literal 0 converts to BindMode only
    through a user-defined conversion, so neither overload wins.  Qt 6.2 added a
    bind(QHostAddress::SpecialAddress, ...) overload, which hides the defect on
    Qt6.  Fixed form: bind(QHostAddress(QHostAddress::LocalHost), 0).

(b) `QVERIFY_EXCEPTION_THROWN(AudioRing(std::numeric_limits<std::size_t>::max()),
                             std::length_error);` (tests/src/core/LinkAudioCodecTest.cpp)

        error: definition or redeclaration of 'max' not allowed inside a function

    Qt pastes the expression as a statement (`expression;`), so `Type(X());` or
    `Type(name);` is the most vexing parse: a declaration, not a construction.
    Fixed form: static_cast<void>(AudioRing(...)).  Constructions such as
    RmsHelper(0), TimeSig(4, 0) or AudioRing(std::size_t{1} << n) are not
    declarations and are not flagged.

The check has three parts:

  * a source scan of the tracked C++ files under tests/, src/, plugins/ and
    include/ for both patterns.  It runs on every host (GCC and Qt6 accept both
    defects), so it is the authoritative half.  Only an exception macro whose
    expression sits on one line is examined; multi-line expressions are ignored.
  * a compile probe: with a clang compiler and Qt5 headers, two known-bad
    fixtures must fail to compile and their fixed forms must compile.  Otherwise
    the probe is reported as skipped.
  * a self-test (`--self-test`, also run first on every invocation) proving the
    scanner flags the bad lines and leaves the good sibling lines alone.

Usage:
  qt5-clang-test-portability-check.py [--root DIR] [--compiler CXX] [--qt5-include DIR]
  qt5-clang-test-portability-check.py --self-test

Exit codes: 0 = clean, 1 = finding (or a failed self-test/probe), 2 = usage error.
"""

import argparse
import os
import re
import subprocess
import sys
import tempfile

SOURCE_SUFFIXES = (".cpp", ".h", ".hpp")
SOURCE_DIRS = ("tests/", "src/", "plugins/", "include/")
SKIP_PARTS = ("src/3rdparty/", "/3rdparty/", "/external/", "/rtneural/")

BIND_SPECIAL = re.compile(r"\bbind\s*\(\s*QHostAddress::")
EXC_MACRO = re.compile(r"\b(QVERIFY_EXCEPTION_THROWN|QVERIFY_THROWS_EXCEPTION)\s*\(")

NAME = r"[A-Za-z_]\w*(?:<[^()]*>)?"
QUALIFIED = rf"(?:::\s*)?(?:{NAME}\s*::\s*)*{NAME}"
# `Type(D)` where D could be a declarator: a (qualified) name, optional `()`.
DECLARATION_SHAPE = re.compile(rf"^\s*{QUALIFIED}\s*\(\s*(?:{QUALIFIED}\s*(?:\(\s*\))?)\s*\)\s*$")

BIND_BAD = "QVERIFY(peer.bind(QHostAddress::LocalHost, 0));"
BIND_GOOD = "QVERIFY(peer.bind(QHostAddress(QHostAddress::LocalHost), 0));"
EXC_BAD = "QVERIFY_EXCEPTION_THROWN(AudioRing(std::numeric_limits<std::size_t>::max()), std::length_error);"
EXC_BAD2 = "QVERIFY_THROWS_EXCEPTION(std::length_error, Widget(size));"
EXC_GOOD = [
	"QVERIFY_EXCEPTION_THROWN(static_cast<void>(AudioRing(std::numeric_limits<std::size_t>::max())), std::length_error);",
	"QVERIFY_EXCEPTION_THROWN(RmsHelper(0), std::invalid_argument);",
	"QVERIFY_EXCEPTION_THROWN(ValueBuffer(-1), std::invalid_argument);",
	"QVERIFY_EXCEPTION_THROWN(TimeSig(4, 0), std::invalid_argument);",
	"QVERIFY_EXCEPTION_THROWN(AudioRing(std::size_t{1} << 63), std::length_error);",
]

FIXTURE_BIND_BAD = "QUdpSocket s;\nvoid f() { s.bind(QHostAddress::LocalHost, 0); }\n"
FIXTURE_BIND_GOOD = "QUdpSocket s;\nvoid f() { s.bind(QHostAddress(QHostAddress::LocalHost), 0); }\n"
FIXTURE_EXC_BODY = "{ QVERIFY_EXCEPTION_THROWN(%s, std::length_error); }\n"
FIXTURE_EXC_BAD = FIXTURE_EXC_BODY % "Ring(std::numeric_limits<std::size_t>::max())"
FIXTURE_EXC_GOOD = FIXTURE_EXC_BODY % "static_cast<void>(Ring(std::numeric_limits<std::size_t>::max()))"
FIXTURE_PRELUDE = (
	"#include <QtNetwork/QUdpSocket>\n#include <QtTest/QtTest>\n#include <limits>\n"
	"#include <stdexcept>\nstruct Ring { explicit Ring(std::size_t n) { if (n) throw std::length_error(\"x\"); } };\n")


def split_top_level(text, first):
	"""Split `text` at the first (or last) comma outside brackets; None when there is none."""
	depth, cut = 0, None
	for index, char in enumerate(text):
		depth += char in "([{" 
		depth -= char in ")]}"
		if char == "," and depth == 0:
			cut = index
			if first:
				break
	return None if cut is None else (text[:cut], text[cut + 1:])


def macro_arguments(line, start):
	"""Text between the macro's parentheses when they close on this line, else None."""
	depth = 1
	for index in range(start, len(line)):
		depth += line[index] == "("
		depth -= line[index] == ")"
		if depth == 0:
			return line[start:index]
	return None


def exception_expression(macro, args):
	"""The expression argument: QVERIFY_THROWS_EXCEPTION(type, expr...) vs (expr, type)."""
	parts = split_top_level(args, first=macro == "QVERIFY_THROWS_EXCEPTION")
	return None if parts is None else parts[1 if macro == "QVERIFY_THROWS_EXCEPTION" else 0]


def scan_line(line):
	"""Return the reasons this line is a defect (empty for a clean line)."""
	if line.lstrip().startswith("//"):
		return []
	reasons = []
	if BIND_SPECIAL.search(line):
		reasons.append("bind(QHostAddress::<SpecialAddress>, ...) is ambiguous before Qt 6.2; "
			"write bind(QHostAddress(QHostAddress::X), ...)")
	for match in EXC_MACRO.finditer(line):
		args = macro_arguments(line, match.end())
		expression = exception_expression(match.group(1), args) if args else None
		if expression and DECLARATION_SHAPE.match(expression):
			reasons.append(f"{match.group(1)} expression `{expression.strip()}` parses as a "
				"declaration (most vexing parse); wrap it in static_cast<void>(...)")
	return reasons


def scan_text(path, text):
	findings = []
	for number, line in enumerate(text.splitlines(), 1):
		if "bind" in line or "QVERIFY_" in line:
			findings += [(path, number, reason) for reason in scan_line(line)]
	return findings


def tracked_sources(root):
	try:
		out = subprocess.run(["git", "-C", root, "ls-files", "-z", "--", *SOURCE_DIRS],
			capture_output=True, check=True).stdout
	except (OSError, subprocess.CalledProcessError):
		return []
	paths = [p.decode("utf-8", "replace") for p in out.split(b"\0") if p]
	return [p for p in paths if p.endswith(SOURCE_SUFFIXES) and not any(s in p for s in SKIP_PARTS)]


def scan_tree(root):
	findings = []
	for relative in tracked_sources(root):
		try:
			with open(os.path.join(root, relative), encoding="utf-8", errors="replace") as handle:
				findings += scan_text(relative, handle.read())
		except OSError:
			continue
	return findings


def self_test():
	failures = []
	for label, line in [("bind", BIND_BAD), ("exception", EXC_BAD), ("throws", EXC_BAD2),
			("bind-var", "QVERIFY2(after.bind(QHostAddress::LocalHost, m_port), \"x\");")]:
		if len(scan_line(line)) != 1:
			failures.append(f"bad {label} line not flagged exactly once: {line}")
	for line in [BIND_GOOD, *EXC_GOOD, "// s.bind(QHostAddress::Any, 0);"]:
		if scan_line(line):
			failures.append(f"good line flagged: {line}")
	found = scan_text("fixture.cpp", "int x;\n" + BIND_BAD + "\n" + EXC_BAD + "\n" + EXC_GOOD[1] + "\n")
	if [line for _, line, _ in found] != [2, 3]:
		failures.append(f"fixture text flagged lines {[line for _, line, _ in found]}, expected [2, 3]")
	return failures


def qt5_include_dir(explicit):
	candidates = [explicit] if explicit else ["/usr/include/qt", "/usr/include/x86_64-linux-gnu/qt5"]
	try:
		query = subprocess.run(["pkg-config", "--variable=includedir", "Qt5Core"],
			capture_output=True, text=True)
		candidates += [query.stdout.strip()] if query.returncode == 0 and not explicit else []
	except OSError:
		pass
	return next((c for c in candidates if c and os.path.isfile(os.path.join(c, "QtCore", "qglobal.h"))), None)


def is_qt5(include_dir):
	try:
		with open(os.path.join(include_dir, "QtCore", "qconfig.h"), encoding="utf-8", errors="replace") as handle:
			return re.search(r'QT_VERSION_STR\s+"5\.', handle.read()) is not None
	except OSError:
		return False


def compiles(compiler, include_dir, workdir, name, source):
	path = os.path.join(workdir, name)
	with open(path, "w", encoding="utf-8") as handle:
		handle.write(FIXTURE_PRELUDE + source)
	command = [compiler, "-fsyntax-only", "-std=gnu++20", "-fPIC", "-I", include_dir]
	command += ["-I" + os.path.join(include_dir, m) for m in ("QtCore", "QtNetwork", "QtTest")]
	return subprocess.run(command + [path], capture_output=True, text=True).returncode == 0


def compile_probe(compiler, qt5_include):
	if not compiler:
		return skip("no --compiler given")
	try:
		version = subprocess.run([compiler, "--version"], capture_output=True, text=True).stdout
	except OSError as error:
		return skip(f"cannot run {compiler}: {error}")
	if "clang" not in version.lower():
		return skip(f"{compiler} is not clang")
	include_dir = qt5_include_dir(qt5_include)
	if not include_dir or not is_qt5(include_dir):
		return skip("Qt5 headers not found (use --qt5-include DIR)")
	expected = [("bind-bad.cpp", FIXTURE_BIND_BAD, False), ("bind-good.cpp", FIXTURE_BIND_GOOD, True),
		("exc-bad.cpp", "void g()" + FIXTURE_EXC_BAD, False), ("exc-good.cpp", "void g()" + FIXTURE_EXC_GOOD, True)]
	status = 0
	with tempfile.TemporaryDirectory(dir=os.environ.get("TMPDIR") or None) as workdir:
		for name, source, should_compile in expected:
			ok = compiles(compiler, include_dir, workdir, name, source) == should_compile
			print(f"{'PASS' if ok else 'FAIL'}: {name} {'compiles' if should_compile else 'is rejected'} with {compiler}")
			status |= 0 if ok else 1
	return status


def skip(reason):
	print(f"compile probe skipped: {reason}; the source scan above is the check")
	return 0


def main(argv):
	parser = argparse.ArgumentParser(description="Fail on Qt5/Clang portability defects in C++ test sources.")
	parser.add_argument("--root", default=os.getcwd())
	parser.add_argument("--compiler")
	parser.add_argument("--qt5-include")
	parser.add_argument("--self-test", action="store_true")
	args = parser.parse_args(argv[1:])
	failures = self_test()
	for failure in failures:
		print(f"SELF-TEST FAIL: {failure}")
	if failures or args.self_test:
		print("" if failures else "self-test: bad lines flagged, good sibling lines clean")
		return 1 if failures else 0
	findings = scan_tree(os.path.abspath(args.root))
	for path, line, reason in findings:
		print(f"{path}:{line}: {reason}")
	if findings:
		print(f"FAIL: {len(findings)} Qt5/Clang portability defect(s) in C++ test sources")
	else:
		print("PASS: no Qt5/Clang portability defects in tracked C++ sources")
	probe_status = compile_probe(args.compiler, args.qt5_include)
	return 1 if findings or probe_status else 0


if __name__ == "__main__":
	sys.exit(main(sys.argv))
