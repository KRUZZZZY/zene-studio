#!/usr/bin/env python3
"""Fail when an exported class declares a thread_local data member.

Cluster ci-512849fa221097db (2026-11-10). MSVC rejects a static thread_local
data member of a class that has a DLL interface:

    include/AudioEngine.h(891): error C2492: 'lmms::AudioEngine::s_changeInModelDepth':
    data with thread storage duration may not have dll interface

The class is `class LMMS_EXPORT AudioEngine`; on MSVC `LMMS_EXPORT` expands to
`__declspec(dllexport)`. GCC and Clang accept the same declaration, so the
hosted msvc-x64 build was the only job that saw it.

The check reads product headers only (include/ and src/, without the vendored
src/3rdparty tree). It tracks class bodies and reports every `thread_local`
declaration that ends a member statement inside a class carrying `LMMS_EXPORT`
or `__declspec(dllexport)`. A thread_local inside a member function body, a
thread_local member of a non-exported class, and text in comments or strings
are NOT flagged. Before scanning the tree it runs its own red/green fixtures,
so a scanner that cannot fail fails instead.

Exit 0 = clean, 1 = findings (or a failed fixture).
"""

import os
import re
import sys

SCAN_DIRS = ("include", "src")
SKIP_DIR_NAMES = {"3rdparty", "external"}
SOURCE_SUFFIXES = (".h", ".hpp", ".hxx")
CLASS_HEAD = re.compile(r"(?:^|[\s;{}])(class|struct|union)\s+(?!\w+\s*;)")
ACCESS_LABEL = re.compile(
	r"(?:^|\s)(?:(?:public|protected|private)\s+(?:slots|Q_SLOTS)|signals|Q_SIGNALS)\s*$"
	r"|(?:^|\s)(?:public|protected|private)\s*$")
EXPORT_MARKER = re.compile(
	r"(?<![A-Za-z0-9_])LMMS_EXPORT(?![A-Za-z0-9_])|__declspec\s*\(\s*dllexport\s*\)")
DECLSPEC = re.compile(r"__declspec\s*\([^)]*\)")
THREAD_LOCAL = re.compile(r"(?<![A-Za-z0-9_])thread_local(?![A-Za-z0-9_])")
TOKEN = re.compile(
	r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'|[{};:]|\n|[^{};:/"\'\n]+|/',
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
	"""One brace level: a class body (and whether it is exported) or anything else."""

	def __init__(self, is_class, is_exported):
		self.is_class = is_class
		self.is_exported = is_exported


class Scanner:
	"""Walks one file's tokens and records thread_local data members of exported classes."""

	def __init__(self, path):
		self.path = path
		self.scopes = [Scope(False, False)]
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
		elif token.startswith('"') or token.startswith("'"):
			self.append(" ")
		elif token == "{":
			self.open_brace()
		elif token == "}":
			self.close_brace()
		elif token == ";":
			self.end_statement()
		elif token == ":":
			self.colon()
		else:
			self.append(token)

	def append(self, token):
		if not self.statement.strip():
			self.statement_line = self.line
		self.statement += token

	def colon(self):
		if self.scopes[-1].is_class and ACCESS_LABEL.search(self.statement):
			self.statement = ""
		else:
			self.append(":")

	def open_brace(self):
		self.check_declaration()
		head = DECLSPEC.sub(" ", self.statement)
		is_class = bool(CLASS_HEAD.search(" " + head)) and "(" not in head
		is_exported = is_class and bool(EXPORT_MARKER.search(self.statement))
		self.scopes.append(Scope(is_class, is_exported))
		self.statement = ""

	def close_brace(self):
		if len(self.scopes) > 1:
			self.scopes.pop()
		self.statement = ""

	def end_statement(self):
		self.check_declaration()
		self.statement = ""

	def check_declaration(self):
		if not self.scopes[-1].is_exported:
			return
		if not THREAD_LOCAL.search(self.statement):
			return
		declaration = " ".join(self.statement.split())
		self.findings.append((self.path, self.statement_line, declaration))


def scan_text(path, text):
	if "LMMS_EXPORT" not in text and "__declspec" not in text:
		return []
	if "thread_local" not in text:
		return []
	return Scanner(path).scan(text)


def iter_sources(root):
	for top in SCAN_DIRS:
		for directory, subdirs, files in os.walk(os.path.join(root, top)):
			subdirs[:] = sorted(d for d in subdirs
				if not d.startswith(".") and d not in SKIP_DIR_NAMES)
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
class LMMS_EXPORT Exported
{
public:
	static thread_local int s_depth;
	//! A comment mentioning thread_local is not a declaration.
	struct Nested { static thread_local int s_nested; };
};

struct __declspec(dllexport) ExportedStruct
{
	static thread_local int s_depth;
};
"""

FIXTURE_GREEN = """
class LMMS_EXPORT Exported
{
public:
	static int s_depth;
	inline static int s_count = 0;
	bool held() const noexcept;
	bool body() const noexcept { thread_local int depth = 0; return depth > 0; }
	const char* text = "thread_local int s_text;";
};

class Plain
{
	static thread_local int s_depth;
};

class LMMS_EXPORT FunctionLocal
{
	bool held() const noexcept { thread_local int depth = 0; return depth > 0; }
};
"""


def self_test():
	red = scan_text("fixture-red.h", FIXTURE_RED)
	green = scan_text("fixture-green.h", FIXTURE_GREEN)
	failures = []
	if len(red) != 2:
		failures.append(f"red fixture flagged {len(red)} declaration(s), expected 2: {red}")
	if [line for _, line, _ in red] != [5, 12]:
		failures.append(f"red fixture line numbers {[line for _, line, _ in red]}, expected [5, 12]")
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
		print(f"{path}:{line}: exported class declares a thread_local data member "
			f"(MSVC C2492): {declaration}")
	if findings:
		print(f"FAIL: {len(findings)} thread_local data member(s) in an exported class; "
			f"move the variable out of the class or access it through an out-of-line function")
		return 1
	print("PASS: no thread_local data member in an exported class")
	return 0


if __name__ == "__main__":
	sys.exit(main(sys.argv))
