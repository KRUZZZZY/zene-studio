#!/usr/bin/env python3
"""Fail on a trailing return type in a declaration that Qt 5's moc parses.

Cluster ci-v041-1 (2026-10-27). moc parses every member declared in a
`signals:` / `slots:` section and every `Q_INVOKABLE` / `Q_SCRIPTABLE` / `Q_SIGNAL` /
`Q_SLOT` member as a meta-method. The Qt 5.15.3 moc that the hosted linux-x86_64
job installs (Ubuntu 22.04) cannot parse a trailing return type there:

    include/Song.h:706: Parse error at ";"

so AutoMoc fails and nothing builds. Qt 6's moc (and late 5.15 LTS builds) accept
it, which is why a Qt 6 build here never notices. A trailing return type on an
ordinary member (outside those sections) is fine on every moc and is NOT flagged;
upstream uses that form widely.

The check reads sources only (no moc, no build): it walks include/, src/,
plugins/ and tests/ for files that declare Q_OBJECT / Q_GADGET, tracks the access
section of every class body, and reports each meta-method declaration whose
parameter list is followed by `->`. Before scanning the tree it runs its own
fixtures (a flagged slot, signal and Q_INVOKABLE; an unflagged ordinary member,
inline body and helper class), so a scanner that cannot fail fails instead.

Exit 0 = clean, 1 = findings (or a failed fixture).
"""

import os
import re
import sys

SCAN_DIRS = ("include", "src", "plugins", "tests")
SOURCE_SUFFIXES = (".h", ".hpp", ".hxx", ".cpp", ".cxx", ".cc")
MOC_CLASS_MACRO = re.compile(r"\bQ_(OBJECT|GADGET)\b")
META_SECTION_LABEL = re.compile(
	r"(?:^|\s)(?:(?:public|protected|private)\s+(?:slots|Q_SLOTS)|signals|Q_SIGNALS)\s*$")
PLAIN_ACCESS_LABEL = re.compile(r"(?:^|\s)(?:public|protected|private)\s*$")
META_METHOD_MARKER = re.compile(r"\bQ_(INVOKABLE|SCRIPTABLE|SIGNAL|SLOT)\b")
CLASS_HEAD = re.compile(r"(?:^|[\s;{}])(class|struct|union)\s+(?!\w+\s*;)")
TRAILING_RETURN = re.compile(
	r"\)\s*(?:const\s*)?(?:volatile\s*)?(?:&&?\s*)?(?:noexcept\s*(?:\([^()]*\))?\s*)?->")
TOKEN = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'|::|[{};:]|\n|[^{};:/"\'\n]+|/',
	re.S)


def strip_preprocessor(text):
	"""Blank out preprocessor lines (with continuations), keeping line numbers."""
	out = []
	continuing = False
	for line in text.split("\n"):
		directive = continuing or line.lstrip().startswith("#")
		continuing = directive and line.rstrip().endswith("\\")
		out.append("" if directive else line)
	return "\n".join(out)


class Scope:
	"""One brace level: a class body (tracks its section) or anything else."""

	def __init__(self, is_class):
		self.is_class = is_class
		self.in_meta_section = False


class Scanner:
	"""Walks one file's tokens and records meta-method trailing-return declarations."""

	def __init__(self, path):
		self.path = path
		self.scopes = [Scope(False)]
		self.statement = ""
		self.statement_line = 1
		self.line = 1
		self.findings = []

	def scan(self, text):
		for match in TOKEN.finditer(strip_preprocessor(text)):
			self.feed(match.group(0))
		return self.findings

	def feed(self, token):
		if token == "\n" or token.startswith("//") or token.startswith("/*"):
			self.line += token.count("\n")
			self.append(" ")
			return
		handlers = {"{": self.open_brace, "}": self.close_brace, ";": self.end_statement,
			":": self.colon}
		handler = handlers.get(token)
		if handler is not None:
			handler()
		else:
			self.append(token)

	def append(self, token):
		if not self.statement.strip():
			self.statement_line = self.line
		self.statement += token

	def check_declaration(self):
		scope = self.scopes[-1]
		if not scope.is_class:
			return
		meta = scope.in_meta_section or META_METHOD_MARKER.search(self.statement)
		if meta and TRAILING_RETURN.search(self.statement):
			declaration = " ".join(self.statement.split())
			self.findings.append((self.path, self.statement_line, declaration))

	def open_brace(self):
		self.check_declaration()
		self.scopes.append(Scope(bool(CLASS_HEAD.search(" " + self.statement)) and "(" not in self.statement))
		self.statement = ""

	def close_brace(self):
		if len(self.scopes) > 1:
			self.scopes.pop()
		self.statement = ""

	def end_statement(self):
		self.check_declaration()
		self.statement = ""

	def colon(self):
		scope = self.scopes[-1]
		if scope.is_class and META_SECTION_LABEL.search(self.statement):
			scope.in_meta_section = True
			self.statement = ""
		elif scope.is_class and PLAIN_ACCESS_LABEL.search(self.statement):
			scope.in_meta_section = False
			self.statement = ""
		else:
			self.append(":")


def scan_text(path, text):
	if not MOC_CLASS_MACRO.search(text):
		return []
	return Scanner(path).scan(text)


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
class Song : public QObject
{
	Q_OBJECT
public:
	auto ordinary() const -> bool;
public slots:
	void stopExport();
	//! A comment -> with an arrow.
	auto prepare(int mode) noexcept -> bool;
signals:
	auto changed() -> void;
private:
	Q_INVOKABLE auto invokable(int x) const -> int;
};
class Test : public QObject
{
	Q_OBJECT
private slots:
	static auto renderPeriod(int frames)
		-> std::array<std::vector<float>, 2>
	{
		return {};
	}
};
"""

FIXTURE_GREEN = """
class Song : public QObject
{
	Q_OBJECT
public:
	auto ordinary() const -> bool;
	auto inlineBody() -> int { return m_p->value(); }
public slots:
	bool prepare(int mode) noexcept;
	void body() { m_x = m_p->value() ? 1 : 2; }
	static std::array<std::vector<float>, 2> renderPeriod(int frames)
	{
		auto f = [](int x) -> int { return x; };
		return {};
	}
private:
	struct Helper { auto get() -> int; };
	auto ordinaryAfterSlots() -> bool;
signals:
	void changed();
};
"""

FIXTURE_EXPECTED = ["prepare", "changed", "invokable", "renderPeriod"]


def self_test():
	red = scan_text("fixture-red.h", FIXTURE_RED)
	names = [re.search(r"auto (\w+)", declaration).group(1) for _, _, declaration in red]
	green = scan_text("fixture-green.h", FIXTURE_GREEN)
	failures = []
	if names != FIXTURE_EXPECTED:
		failures.append(f"red fixture flagged {names}, expected {FIXTURE_EXPECTED}")
	if [line for _, line, _ in red][:1] != [10]:
		failures.append(f"red fixture line numbers {[line for _, line, _ in red]}, first expected 10")
	if green:
		failures.append(f"green fixture flagged {green}")
	if scan_text("plain.h", "struct S { public slots: auto f() -> int; };"):
		failures.append("a file without Q_OBJECT/Q_GADGET was scanned")
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
		print(f"{path}:{line}: trailing return type in a moc-parsed declaration "
			f"(Qt 5 moc: Parse error): {declaration}")
	if findings:
		print(f"FAIL: {len(findings)} declaration(s); declare the return type before the name")
		return 1
	print("PASS: no trailing return type in a moc-parsed declaration")
	return 0


if __name__ == "__main__":
	sys.exit(main(sys.argv))
