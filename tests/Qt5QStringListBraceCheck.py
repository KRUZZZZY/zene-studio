#!/usr/bin/env python3
"""Fail on a brace-list assignment to a `QStringList` - ambiguous on Qt 5.

Cluster ci-b5ac4280f3e4b317 (2026-11-14), hosted `build / macos-arm64` job:
`tests/src/core/ImportTrackIoTest.cpp:344` wrote

    plan.documents = {UnparsableTrack, trackDocument(Lead)};

and the build stopped with

    error: use of overloaded operator '=' is ambiguous (with operand types
        'QStringList' and 'void')

Qt 5.15's `QStringList` declares `operator=(const QList<QString> &)` and
`operator=(QList<QString> &&)` besides its own copy/move assignment
(`/usr/include/qt/QtCore/qstringlist.h:111-127`; the macOS job's Qt 5 header is
the same file). A brace-enclosed list can initialise either `QStringList` or
its `QList<QString>` base, so every candidate is viable and overload resolution
is ambiguous. Qt 6 makes `QStringList` an alias of `QList<QString>`
(`qcontainerfwd.h:41`) with no extra assignment overloads, so a Qt 6 host
compiles the same line and only the Qt 5 jobs fail.

The fix at the call site is to name the type the list initialises:
`plan.documents = QStringList{...};`. These forms are NOT ambiguous and are not
reported:

  * a declaration/initialisation - `const QStringList names = {...};`,
    `QStringList names = {...};`, a parameter default `QStringList ids = {}`;
  * `names += {...}`, `QList<QString> list; list = {...};` (no extra overloads);
  * an explicitly typed assignment - `names = QStringList{...};`.

The check resolves `obj.member`, `obj->member`, `(*ptr).member`, `*ptr` and
plain identifiers to their declared type: locals and parameters declared in the
file, and class members declared in any first-party header (`TrackPlan`'s
`QStringList documents` lives in `include/ControlProjectTransplant.h`). Vendored
third-party trees are excluded, exactly as `tests/all-sources.txt` excludes
them. The source scan is authoritative and needs no Qt, so it is red on this
Qt 6 host where the defect compiles.

When a Qt 5 development tree is installed the check also compiles a fixture in
the failing shape: that Qt 5 must reject the brace assignment and accept the
explicitly typed form. Without Qt 5 the probe reports itself skipped (never
passed).

Before scanning, the file runs its own red/green fixtures, so a scanner that
cannot fail fails.

Usage:
  Qt5QStringListBraceCheck.py [--root DIR] [--cxx CXX]
  Qt5QStringListBraceCheck.py --self-test

Exit codes: 0 = clean, 1 = finding (or a failed fixture), 2 = usage error.
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile

SOURCE_SUFFIXES = (".c", ".cc", ".cpp", ".cxx", ".h", ".hpp", ".hxx")

# Vendored third-party trees are not this repo's code (tests/all-sources.txt).
EXCLUDED_PREFIXES = (
	"src/3rdparty/",
	"tests/reference/",
	"plugins/NeuralAmp/rtneural/",
	"plugins/NeuralAmp/nam/",
	"plugins/NeuralAmp/tests/",
	"plugins/RnnoiseDenoiser/rnnoise/",
	"plugins/SfzPlayer/sfizz/",
	# A git submodule (git ls-files reports the gitlink, not its files), so the
	# repo's own manifests never measure it: Carla's vendored Qt settings helper.
	"plugins/CarlaBase/carla/",
)

# Comments, string and char literals are blanked before scanning: a mention in
# prose or in an error string is not an assignment. Newlines are kept, so the
# reported line numbers are the source's own.
NON_CODE = re.compile(
	r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'', re.S)

# Only `QStringList` is ambiguous: a `QList<QString>` has no extra assignment
# overloads in Qt 5, so `QList<QString> list; list = {...};` is portable.
QSL = r'QStringList'
# A declarator of that type, capturing the declared name: `QStringList documents;`,
# `const QStringList &ids = {}`, `QStringList* documents = nullptr`.
QSL_DECLARATOR = re.compile(r'\bQStringList\b\s*(?:const\s*)?[&*]?\s*([A-Za-z_]\w*)')
# The whole text between the declaration's start and its name: `const QStringList &`.
QSL_PREFIX = re.compile(
	r'^(?:(?:const|volatile|static|constexpr|mutable|typename)\s+)*'
	r'(?:[A-Za-z_]\w*\s*::\s*)*QStringList\s*(?:const\s*)?[&*]*$')

# The type a declarator names: `control::transplant::TrackPlan plan`.
DECLARATOR = re.compile(
	r'(?<![\w.>])([A-Za-z_]\w*(?:::[A-Za-z_]\w*)*)\s*(?:const\s*)?[&*]?\s*([A-Za-z_]\w*)\s*(?=[;=({,)\[])')

CLASS_HEAD = re.compile(r'\b(?:class|struct|union)\s+([A-Za-z_]\w*)[^;{}()]*\{')

# `plan.documents = {`, `(*plan).documents = {`, `p->documents = {`, `names = {`.
ASSIGNMENT = re.compile(
	r'((?:\(\s*\*\s*[A-Za-z_]\w*\s*\)|\*?\s*[A-Za-z_]\w*)(?:\s*(?:\.|->)\s*[A-Za-z_]\w*)*)\s*=\s*\{')

# Words that can end the text before a name without being its type.
NOT_A_TYPE = {
	"return", "else", "case", "throw", "new", "delete", "using", "typedef", "namespace",
	"template", "class", "struct", "union", "if", "while", "for", "switch", "do", "goto",
	"sizeof", "static_assert", "decltype", "operator", "public", "private", "protected",
	"virtual", "explicit", "friend", "inline", "constexpr", "consteval", "constinit",
	"co_return", "co_await", "co_yield", "emit", "default", "const", "static", "mutable",
	"typename", "volatile", "auto", "void", "bool", "int", "unsigned", "signed", "long",
	"short", "char", "float", "double", "and", "or", "not", "this",
}

# Fixtures. The red ones are the ambiguous shapes (assignment to an existing
# QStringList, local, member through `.`, `->` and a dereferenced pointer);
# the green ones are every portable spelling.
FIXTURE_RED = """\
struct TrackPlan { QStringList documents; };

QStringList localList()
{
	QStringList names;
	names = {QStringLiteral("a"), QStringLiteral("b")};
	names = {};
	TrackPlan plan;
	plan.documents = {QStringLiteral("one")};
	plan.documents = {UnparsableTrack, trackDocument(Lead)};
	TrackPlan* planPtr = &plan;
	planPtr->documents = {QStringLiteral("two")};
	(*planPtr).documents = {QStringLiteral("three")};
	return names;
}
"""

FIXTURE_GREEN = """\
struct TrackPlan { QStringList documents; };

void take(const QStringList& ids = {}, QStringList more = {})
{
	QStringList names = {QStringLiteral("a"), QStringLiteral("b")};
	const QStringList declared = {QStringLiteral("c")};
	names += QStringLiteral("d");
	QList<QString> plain;
	plain = {QStringLiteral("e")};
	TrackPlan plan;
	// plan.documents = {QStringLiteral("commented")};
	plan.documents = QStringList{QStringLiteral("named")};
	plan.documents.append(QStringLiteral("f"));
	// The cluster's target shape: clear, then two appends (ImportTrackIoTest.cpp:415-417).
	plan.documents.clear();
	plan.documents.append(UnparsableTrack);
	plan.documents.append(trackDocument(Lead));
	const char* text = "names = {QStringLiteral(\\"string\\")}";
	(void)text; (void)declared; (void)ids; (void)more;
}
"""

FIXTURE_RED_LINES = [6, 7, 9, 10, 12, 13]

# The target's fixed shape, pinned so the green fixture cannot silently drop it.
FIXTURE_GREEN_CLEAR_APPEND = (
	"plan.documents.clear();",
	"plan.documents.append(UnparsableTrack);",
	"plan.documents.append(trackDocument(Lead));",
)

PROBE_RED = """\
#include <QStringList>
struct Plan { QStringList documents; };
int main()
{
	Plan plan;
	plan.documents = {QStringLiteral("a"), QStringLiteral("b")};
	return plan.documents.size();
}
"""

PROBE_GREEN = """\
#include <QStringList>
struct Plan { QStringList documents; };
int main()
{
	Plan plan;
	plan.documents = QStringList{QStringLiteral("a"), QStringLiteral("b")};
	QStringList names = {QStringLiteral("a")};
	return plan.documents.size() + names.size();
}
"""


def blank_non_code(text):
	"""Comments and literals become spaces; every newline and offset is kept."""
	return NON_CODE.sub(lambda match: re.sub(r"[^\n]", " ", match.group(0)), text)


def blank_directives(text):
	"""Preprocessor lines become spaces (line numbers kept): a `#define` arg is no assignment."""
	return re.sub(r"^[ \t]*#[^\n]*", lambda match: " " * len(match.group(0)), text, flags=re.M)


def code_of(text):
	"""The scan form of a source: no comments, literals or preprocessor lines."""
	return blank_directives(blank_non_code(text))


def brace_end(text, start):
	"""Index of the '}' closing the '{' at @a start, or -1."""
	depth = 0
	for index in range(start, len(text)):
		if text[index] == "{":
			depth += 1
		elif text[index] == "}":
			depth -= 1
			if depth == 0:
				return index
	return -1


def class_index(codes):
	"""{class name: {member names declared with a QStringList type}} across the tree."""
	index = {}
	for text in codes.values():
		for match in CLASS_HEAD.finditer(text):
			end = brace_end(text, match.end() - 1)
			if end < 0:
				continue
			members = {decl.group(1) for decl in QSL_DECLARATOR.finditer(text[match.end():end])}
			if members:
				index.setdefault(match.group(1), set()).update(members)
	return index


def declared_type(text, name, limit):
	"""The type written before the last declaration of @a name before @a limit, or None."""
	found = None
	for match in DECLARATOR.finditer(text, 0, limit):
		if match.group(2) == name and match.group(1) not in NOT_A_TYPE:
			found = match.group(1)
	return found


def simple_type(type_name):
	"""`control::transplant::TrackPlan` -> `TrackPlan`."""
	if type_name is None:
		return None
	parts = [part for part in type_name.split("::") if part]
	return parts[-1] if parts else None


def line_of(text, offset):
	return text.count("\n", 0, offset) + 1


def is_initializer(text, offset):
	"""Whether `name = {` at @a offset is a declaration or a parameter default.

	`QStringList names = {`, `const QStringList& names = {` and a parameter's
	`QStringList ids = {}` all have the declaration's type and no statement before
	the name; an assignment (`QStringList names; names = {}`) does not.
	"""
	start = max(max(text.rfind(mark, 0, offset) for mark in ";{}("), text.rfind(",", 0, offset))
	return QSL_PREFIX.match(text[start + 1:offset].strip()) is not None


def assignment_targets(code):
	"""(offset of the target, target expression) for every `target = {` in @a code."""
	targets = []
	for match in ASSIGNMENT.finditer(code):
		raw = match.group(1)
		targets.append((match.start(1) + len(raw) - len(raw.lstrip()), raw.strip()))
	return targets


def member_of(target):
	"""(object expression, member name) for `obj.member` / `obj->member`, else None."""
	match = re.match(r"^(.*?)\s*(?:\.|->)\s*([A-Za-z_]\w*)$", target)
	return (match.group(1).strip(), match.group(2)) if match else None


def dereferenced(name):
	"""`*p` and `(*p)` name p; None for anything else."""
	match = re.match(r"^\(\s*\*\s*([A-Za-z_]\w*)\s*\)$", name) or re.match(r"^\*\s*([A-Za-z_]\w*)$", name)
	return match.group(1) if match else None


def enclosing_class(text, offset, classes):
	"""The members of the innermost class body holding @a offset, or None."""
	best = None
	for match in CLASS_HEAD.finditer(text):
		end = brace_end(text, match.end() - 1)
		if end < 0 or not match.end() <= offset < end:
			continue
		if best is None or match.start() > best[1]:
			best = (match.group(1), match.start())
	return classes.get(best[0]) if best else None


def qstringlist_names(code):
	"""Names this translation unit declares with a QStringList type (locals and parameters)."""
	return {decl.group(1) for decl in QSL_DECLARATOR.finditer(code)}


def member_assignment_is_ambiguous(text, offset, target, classes):
	"""Whether `obj.member = {` / `(*p).member = {` / `this->member = {` names a QStringList member."""
	member = member_of(target)
	if member is None:
		return False
	owner, member_name = member
	if owner == "this":
		members = enclosing_class(text, offset, classes)
		return members is not None and member_name in members
	base = dereferenced(owner) or owner
	if not re.match(r"^[A-Za-z_]\w*$", base):
		return False
	type_name = simple_type(declared_type(text, base, offset))
	return type_name in classes and member_name in classes[type_name]


def plain_assignment_is_ambiguous(text, offset, target, qsl_names):
	"""Whether `names = {` / `*names = {` assigns to a QStringList declared in this file."""
	base = dereferenced(target) or target
	if not re.match(r"^[A-Za-z_]\w*$", base) or is_initializer(text, offset):
		return False
	# Resolve the name's own declaration, so a same-named variable of another type
	# in the same file (a QStringList and a QByteArray `defaultValue`) is not confused.
	declared = simple_type(declared_type(text, base, offset))
	return declared == "QStringList" or (declared is None and base in qsl_names)


def scan_file(name, text, classes, qsl_names):
	"""(path, line, target) for every ambiguous QStringList brace assignment in @a text."""
	findings = []
	for offset, target in assignment_targets(text):
		if member_of(target) is not None:
			ambiguous = member_assignment_is_ambiguous(text, offset, target, classes)
		else:
			ambiguous = plain_assignment_is_ambiguous(text, offset, target, qsl_names)
		if ambiguous:
			findings.append((name, line_of(text, offset), target))
	return findings


def scan_text(name, text, classes=None):
	"""(path, line, target) for one source; @a classes carries the tree's class members."""
	code = code_of(text)
	index = classes if classes is not None else class_index({name: code})
	return scan_file(name, code, index, qstringlist_names(code))


def scan_tree(root):
	"""Every first-party source, scanned as one component so header classes resolve."""
	codes = {}
	for directory, dirnames, filenames in os.walk(root):
		dirnames[:] = [entry for entry in dirnames if entry not in (".git", "build", "node_modules")]
		for filename in filenames:
			if not filename.endswith(SOURCE_SUFFIXES):
				continue
			path = os.path.join(directory, filename)
			key = os.path.relpath(path, root).replace(os.sep, "/")
			if key.startswith(EXCLUDED_PREFIXES):
				continue
			try:
				with open(path, encoding="utf-8", errors="replace") as handle:
					codes[key] = code_of(handle.read())
			except OSError:
				continue
	classes = class_index(codes)
	findings = []
	for key, code in sorted(codes.items()):
		findings.extend(scan_file(key, code, classes, qstringlist_names(code)))
	return findings


def qt5_flags():
	"""Compile flags of a Qt 5 development tree, or None when pkg-config has none."""
	if shutil.which("pkg-config") is None:
		return None
	if subprocess.run(["pkg-config", "--exists", "Qt5Core"], capture_output=True).returncode != 0:
		return None
	result = subprocess.run(["pkg-config", "--cflags", "Qt5Core"], capture_output=True, text=True)
	return result.stdout.split() if result.returncode == 0 else None


def compile_probe(cxx, flags, workdir):
	"""Only a Qt 5 tree rejects PROBE_RED and accepts PROBE_GREEN; None when it accepts both."""
	results = []
	for name, text in (("red.cpp", PROBE_RED), ("green.cpp", PROBE_GREEN)):
		path = os.path.join(workdir, name)
		with open(path, "w", encoding="utf-8") as handle:
			handle.write(text)
		try:
			results.append(subprocess.run([cxx, "-std=gnu++20", "-fsyntax-only", *flags, path],
				capture_output=True, timeout=120))
		except (OSError, subprocess.SubprocessError):
			return None
	if results[0].returncode == 0:
		return None
	if results[1].returncode != 0:
		return "probe: this Qt 5 tree rejected the explicitly typed QStringList initialiser"
	return "probe: Qt 5 rejects the brace assignment and accepts QStringList{...}"


def self_test():
	failures = []
	red = scan_text("fixture-red.cpp", FIXTURE_RED)
	lines = [line for _, line, _ in red]
	if lines != FIXTURE_RED_LINES:
		failures.append(f"red fixture flagged lines {lines}, expected {FIXTURE_RED_LINES}")
	green = scan_text("fixture-green.cpp", FIXTURE_GREEN)
	if green:
		failures.append(f"green fixture flagged {green}")
	missing = [form for form in FIXTURE_GREEN_CLEAR_APPEND if form not in FIXTURE_GREEN]
	if missing:
		failures.append(f"green fixture lacks the target's clear/append form: {missing}")
	return failures


def run_self_test():
	"""Print the fixture verdict; True when both fixtures behaved as required."""
	failures = self_test()
	for failure in failures:
		print(f"SELF-TEST FAIL: {failure}")
	if failures:
		return False
	print("self-test: the brace assignment is flagged and every portable form is not")
	return True


def run_probe(args):
	"""Print the Qt 5 compile probe's verdict; skipped (not passed) without Qt 5."""
	flags = qt5_flags()
	if flags is None:
		print("probe: skipped (no Qt 5 development tree found)")
		return
	workdir = tempfile.mkdtemp(prefix="qt5-qstringlist-brace-")
	try:
		print(compile_probe(args.cxx, flags, workdir) or "probe: skipped (this Qt accepts the brace assignment)")
	finally:
		shutil.rmtree(workdir, ignore_errors=True)


def report(findings):
	"""Print every finding and the verdict; the process exit code."""
	for path, line, target in findings:
		print(f"{path}:{line}: `{target} = {{...}}` is ambiguous on Qt 5; "
			f"name the type: `{target} = QStringList{{...}}`")
	if findings:
		print(f"FAIL: {len(findings)} ambiguous QStringList brace assignment(s)")
		return 1
	print("PASS: no ambiguous QStringList brace assignment in the first-party tree")
	return 0


def main(argv):
	parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
	parser.add_argument("--root", default=os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
	parser.add_argument("--cxx", default=os.environ.get("CXX") or shutil.which("c++") or "c++")
	parser.add_argument("--self-test", action="store_true")
	args = parser.parse_args(argv[1:])
	if not run_self_test():
		return 1
	if args.self_test:
		return 0
	root = os.path.abspath(args.root)
	if not os.path.isdir(root):
		print(f"error: not a directory: {root}", file=sys.stderr)
		return 2
	run_probe(args)
	return report(scan_tree(root))


if __name__ == "__main__":
	sys.exit(main(sys.argv))
