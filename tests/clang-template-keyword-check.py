#!/usr/bin/env python3
"""Regression for cluster ci-165e1914b308f189: no `template` keyword without a template argument list.

Cluster ci-165e1914b308f189 (2026-11-11), hosted windows-arm64 build: the
vendored atomic_queue header failed to compile with

    atomic_queue.h:394:31: error: a template argument list is expected after a
        name prefixed by the template keyword
        [-Wmissing-template-arg-list-after-template-kw]
    return Base::template do_pop_any(states_[index], elements_[index]);

`template` is only needed before a member template name that is followed by an
explicit template argument list (`x.template f<T>(...)`); when the arguments are
deduced, the keyword makes the program ill-formed in current Clang (the
diagnostic is on by default, and the CI compile line carries -Wno-error, so it
cannot be waived).  The four sites in
plugins/SfzPlayer/sfizz/external/atomic_queue/include/atomic_queue/atomic_queue.h
used it before a plain call, which is why every translation unit including
sfizz's FilePool.h (FilePool.cpp, Synth.cpp, ...) failed to build.

The check has two independent halves:

  * a source scan of every tracked C/C++ file for `::template name`,
    `.template name` and `->template name` where `name` is not followed by
    `<`.  This half runs on every platform, including GCC hosts that accept the
    construct, so the regression is red wherever the suite runs.
  * a compile probe: if the supplied compiler enforces the rule (it rejects a
    known-bad fixture), the real atomic_queue header is compiled through a
    translation unit that instantiates both affected queue templates.  A GCC
    host accepts the known-bad fixture, so the probe is reported as skipped
    there and the source scan is the authoritative half.

Both halves are proved able to fail by the self-test fixtures (`--self-test`),
which this file runs as its own red/green control.

Usage:
  clang-template-keyword-check.py [--root DIR] [--compiler CXX]
                                  [--atomic-queue-include DIR]
  clang-template-keyword-check.py --self-test

Exit codes: 0 = clean, 1 = finding (or a failed fixture), 2 = usage error.
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile

# Comments and string/char literals are blanked before scanning: a mention in
# prose or in an error string is not a use of the keyword.
NON_CODE = re.compile(
	r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'',
	re.S)

# `Base::template do_pop_any(`, `q.template f(`, `p->template f(` - the name
# after the keyword.  A following `<` is an explicit template argument list and
# is accepted; everything else is the defect.
KEYWORD_USE = re.compile(
	r'(?:(?:::|\.|->)\s*)template\s+([A-Za-z_][A-Za-z0-9_]*)\s*')

SOURCE_SUFFIXES = (".c", ".cc", ".cpp", ".cxx", ".h", ".hpp")

# A name that can be prefixed with `template` but carries no explicit argument
# list: the compiler rejects this and so must the scanner.
FIXTURE_RED = """\
struct Base { template<class T> static T f(T v) { return v; } };
struct Other { int member; };

int bad_call()
{
	return Base::template f(1);
}

template<class T>
struct Wrapper
{
	static int g(T v) { return Base::template f(v); }
};

int bad_member(Other& o)
{
	return o.template f(1) + (&o)->template f(2);
}

// A mention in a comment is not a use: Base::template f(1)
static const char* text = "Base::template f(1)";
"""

# The fixed form: no keyword before a deduced call, and the keyword with an
# explicit argument list where the standard requires it.
FIXTURE_GREEN = """\
struct Base { template<class T> static T f(T v) { return v; } };

int good_call()
{
	return Base::f(1);
}

template<class T>
struct Wrapper
{
	static T g(T v) { return Base::template f<T>(v); }
};

template<class A>
struct Traits
{
	typename std::allocator_traits<A>::template rebind_alloc<int> alloc;
};

// Base::template f(1) in a comment, and "Base::template f(1)" in a string.
"""

# Compiled with the host compiler to learn whether it enforces the rule and to
# compile the real header when it does.
COMPILER_RED_PROBE = """\
struct Base { template<class T> static T f(T v) { return v; } };
int g() { return Base::template f(1); }
"""

COMPILER_GREEN_PROBE = """\
#include "atomic_queue/atomic_queue.h"

int main()
{
	atomic_queue::AtomicQueue2<int, 4> q2;
	int v = 0;
	if (!q2.try_push(1))
		return 1;
	if (!q2.try_pop(v) || v != 1)
		return 2;
	atomic_queue::AtomicQueueB2<int> qb2(4);
	if (!qb2.try_push(2))
		return 3;
	if (!qb2.try_pop(v) || v != 2)
		return 4;
	return 0;
}
"""


def strip_non_code(text):
	def blank(match):
		s = match.group(0)
		return "\n" * s.count("\n") if s.startswith(("/*", "//")) else " "
	return NON_CODE.sub(blank, text)


def scan_text(path, text):
	"""Return (path, line, snippet) for every keyword use without an argument list."""
	findings = []
	code = strip_non_code(text)
	lines = text.splitlines()
	for match in KEYWORD_USE.finditer(code):
		if code[match.end():].startswith("<"):
			continue
		line = code.count("\n", 0, match.start()) + 1
		snippet = lines[line - 1].strip() if line - 1 < len(lines) else ""
		findings.append((path, line, snippet[:160]))
	return findings


def git_tracked_sources(root):
	try:
		out = subprocess.run(["git", "-C", root, "ls-files", "-z"],
			capture_output=True, check=True).stdout
	except (OSError, subprocess.CalledProcessError):
		return []
	return [p.decode("utf-8", "replace") for p in out.split(b"\0")
		if p.decode("utf-8", "replace").endswith(SOURCE_SUFFIXES)]


def walked_sources(root):
	paths = []
	for directory, subdirs, files in os.walk(root):
		subdirs[:] = sorted(d for d in subdirs if d not in {".git", "build"})
		paths += [os.path.relpath(os.path.join(directory, name), root)
			for name in sorted(files) if name.endswith(SOURCE_SUFFIXES)]
	return paths


def tracked_sources(root):
	return git_tracked_sources(root) or walked_sources(root)


def scan_file(root, relative):
	path = os.path.join(root, relative)
	try:
		with open(path, encoding="utf-8", errors="replace") as handle:
			text = handle.read()
	except OSError:
		return []
	return scan_text(relative, text) if "template" in text else []


def scan_tree(root):
	findings = []
	for relative in tracked_sources(root):
		findings += scan_file(root, relative)
	return findings


def self_test():
	failures = []
	red = scan_text("fixture-red.h", FIXTURE_RED)
	green = scan_text("fixture-green.h", FIXTURE_GREEN)
	if len(red) != 4:
		failures.append(f"red fixture flagged {len(red)} use(s), expected 4: {red}")
	if [line for _, line, _ in red] != [6, 12, 17, 17]:
		failures.append(f"red fixture line numbers {[line for _, line, _ in red]}, expected [6, 12, 17, 17]")
	if green:
		failures.append(f"green fixture flagged: {green}")
	return failures


def run(command):
	return subprocess.run(command, capture_output=True, text=True)


def compiler_rejects_bad_syntax(compiler, workdir):
	"""True when the compiler enforces the rule; False when it accepts the fixture."""
	probe = os.path.join(workdir, "bad-keyword.cpp")
	with open(probe, "w", encoding="utf-8") as handle:
		handle.write(COMPILER_RED_PROBE)
	return run([compiler, "-std=gnu++17", "-fsyntax-only", probe]).returncode != 0


def compile_real_header(compiler, include_dir, workdir):
	probe = os.path.join(workdir, "atomic-queue-probe.cpp")
	with open(probe, "w", encoding="utf-8") as handle:
		handle.write(COMPILER_GREEN_PROBE)
	command = [compiler, "-std=gnu++17", "-fsyntax-only", "-I", include_dir, probe]
	return command, run(command)


def compile_probe(compiler, include_dir):
	workdir = tempfile.mkdtemp(prefix="zene-template-keyword-",
		dir=os.environ.get("TMPDIR") or None)
	try:
		if not compiler_rejects_bad_syntax(compiler, workdir):
			print(f"compile probe skipped: {compiler} accepts the bad fixture "
				f"(it does not enforce the rule); the source scan above is the check")
			return 0
		command, result = compile_real_header(compiler, include_dir, workdir)
		if result.returncode == 0:
			print(f"PASS: {compiler} compiles the real atomic_queue header "
				f"(AtomicQueue2 and AtomicQueueB2 instantiated)")
			return 0
		print(f"FAIL: {compiler} cannot compile the real atomic_queue header "
			f"(exit {result.returncode})")
		print("  command:", " ".join(command))
		print((result.stderr or result.stdout).strip()[:4000])
		return 1
	finally:
		shutil.rmtree(workdir, ignore_errors=True)


def main(argv):
	parser = argparse.ArgumentParser(add_help=True,
		description="Fail on a `template` keyword with no template argument list.")
	parser.add_argument("--root", default=os.getcwd())
	parser.add_argument("--compiler")
	parser.add_argument("--atomic-queue-include")
	parser.add_argument("--self-test", action="store_true")
	args = parser.parse_args(argv[1:])
	root = os.path.abspath(args.root)
	include_dir = args.atomic_queue_include or os.path.join(root,
		"plugins/SfzPlayer/sfizz/external/atomic_queue/include")

	failures = self_test()
	for failure in failures:
		print(f"SELF-TEST FAIL: {failure}")
	if args.self_test:
		if failures:
			return 1
		print("self-test: red fixture flagged at the expected lines, green fixture clean")
		return 0
	if failures:
		return 1

	findings = scan_tree(root)
	for path, line, snippet in findings:
		print(f"{path}:{line}: 'template' keyword without a template argument list: {snippet}")
	if findings:
		print(f"FAIL: {len(findings)} 'template' keyword use(s) before a name with no "
			f"template argument list; current Clang rejects these "
			f"(-Wmissing-template-arg-list-after-template-kw)")
		return 1
	print("PASS: no 'template' keyword without a template argument list in tracked C/C++ sources")
	return compile_probe(args.compiler, include_dir) if args.compiler else 0


if __name__ == "__main__":
	sys.exit(main(sys.argv))
