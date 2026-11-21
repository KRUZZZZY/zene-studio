#!/usr/bin/env python3
"""Regression for cluster ci-1e541b5d1279ed2d: the crash reporter's ARM/DISARM
proof must compile against Apple's <signal.h>.

The macos-arm64 build stopped at

    tests/src/core/CrashReporterArmTest.cpp:81:16: error: exception specification
    in declaration does not match previous declaration      (the test's sigaction interposer)
    tests/src/core/CrashReporterArmTest.cpp:198:13: error: expected unqualified-id
                                                            (::sigemptyset, a macro on Apple)

and the sibling tests/src/core/CrashReporterTest.cpp:548 carried the same
`::sigemptyset` spelling and had not been compiled yet when the job stopped.

Why a probe.  This box is not Darwin, so the check compiles the two REAL
translation units with the project's own recorded flags from
build/compile_commands.json, under a model of the two Apple properties the test
depends on:

  1. sigaction() is declared WITHOUT an exception specification. glibc declares
     it `noexcept (true)` through __THROW; the probe prepends a shadow
     <sys/cdefs.h> that empties __THROW, which is exactly Apple's state (its
     libc has no __THROW).  The shadow is only added when the host's own
     declaration is noexcept, and a model assertion proves the change took.
  2. sigemptyset() is a function-like MACRO. Apple's is `(*(set) = 0, 0)`; the
     probe uses `(*(set) = {}, 0)` so the same macro also fits hosts whose
     sigset_t is a struct. `::sigemptyset(...)` therefore cannot parse.

Two fixtures keep the model honest and independent of the test sources: the
pre-fix shape must FAIL under it, the fixed shape must PASS. A Clang-family
compiler is required because that is what the hosted macOS job runs and it is
the compiler that enforces the exception-specification match; GCC accepts the
same redeclaration, so it cannot measure that half.

Usage: crash-reporter-apple-signal-check.py --build-dir <build>
       [--root <source>] [--cxx <clang>]

Exit 0 only when the model is proven and both real translation units compile.
"""

import argparse
import json
import pathlib
import shlex
import subprocess
import sys
import tempfile

TARGETS = (
    "tests/src/core/CrashReporterArmTest.cpp",
    "tests/src/core/CrashReporterTest.cpp",
)

# Flags the probe needs to reproduce the recorded compilation. Warning,
# optimisation and dependency flags are deliberately dropped: this is a
# syntax/portability probe, and the real build keeps every flag.
JOINED_PREFIXES = ("-I", "-D", "-U", "-F")
SPLIT_FLAGS = ("-isystem", "-iquote", "-isysroot", "-include", "-iframework", "-arch", "-target")
JOINED_FLAGS = ("-std=", "--target=", "-mmacosx-version-min=", "-isysroot")
STANDALONE_FLAGS = ("-fPIC", "-fPIE", "-pthread")

APPLE_SIGNAL_H = """\
#ifndef ZENE_TEST_SIMULATED_APPLE_SIGNAL_H
#define ZENE_TEST_SIMULATED_APPLE_SIGNAL_H
/* Model Apple's <signal.h> where the ARM/DISARM proof depends on it. */
#include <signal.h>
#include <csignal>
#undef sigemptyset
#define sigemptyset(set) (*(set) = {}, 0)
#endif
"""

CDEFS_SHADOW = """\
#ifndef ZENE_TEST_SIMULATED_APPLE_CDEFS_H
#define ZENE_TEST_SIMULATED_APPLE_CDEFS_H
/* Apple's libc has no __THROW; glibc's declares sigaction() noexcept through it. */
#include_next <sys/cdefs.h>
#undef __THROW
#undef __THROWNL
#define __THROW
#define __THROWNL
#endif
"""

MODEL_ASSERTION = """\
#include <signal.h>
#ifndef sigemptyset
#error "the model must define sigemptyset as a macro"
#endif
static_assert(!noexcept(::sigaction(0, nullptr, nullptr)),
	"the model must remove sigaction's exception specification");
int main() { return 0; }
"""

OLD_SHAPE = """\
#include <signal.h>
extern "C" int sigaction(int signum, const struct sigaction* action,
	struct sigaction* oldAction) noexcept
{
	(void)signum; (void)action; (void)oldAction; return 0;
}
int main()
{
	struct sigaction ignored{};
	::sigemptyset(&ignored.sa_mask);
	return 0;
}
"""

NEW_SHAPE = """\
#include <signal.h>
extern "C" int sigaction(int signum, const struct sigaction* action,
	struct sigaction* oldAction)
	noexcept(noexcept(::sigaction(0, nullptr, nullptr)))
{
	(void)signum; (void)action; (void)oldAction; return 0;
}
int main()
{
	struct sigaction ignored{};
	sigemptyset(&ignored.sa_mask);
	return 0;
}
"""


def run(command, cwd):
    return subprocess.run(command, cwd=cwd, text=True, capture_output=True, check=False)


def fail(message, result=None):
    print("FAIL " + message)
    if result is not None and (result.stdout or result.stderr):
        print((result.stdout + result.stderr).strip()[:2000])
    return False


def is_clang(cxx):
    result = run([cxx, "--version"], None)
    return result.returncode == 0 and "clang" in result.stdout.lower()


def host_sigaction_is_noexcept(cxx, temp):
    source = temp / "host-noexcept.cpp"
    source.write_text(
        '#include <signal.h>\nstatic_assert(noexcept(::sigaction(0, nullptr, nullptr)), "noexcept");\n'
        "int main() { return 0; }\n", encoding="utf-8")
    result = run([cxx, "-std=c++20", "-fsyntax-only", str(source)], temp)
    return result.returncode == 0


def recorded_flags(entry):
    arguments = entry.get("arguments")
    if not arguments:
        arguments = shlex.split(entry["command"])
    flags, index = [], 1          # skip the compiler
    while index < len(arguments):
        argument = arguments[index]
        index += 1
        if argument in ("-c", "-o", "-MF", "-MT", "-MQ"):
            index += 1           # the value of the pair
            continue
        if argument in SPLIT_FLAGS:
            flags += [argument, arguments[index]]
            index += 1
            continue
        if argument.startswith(JOINED_FLAGS) or argument.startswith(JOINED_PREFIXES):
            flags.append(argument)
            continue
        if argument in STANDALONE_FLAGS:
            flags.append(argument)
    return flags


def compile_probe(cxx, flags, overlay, source, temp):
    command = [cxx, "-I", str(overlay), *flags,
               "-include", str(overlay / "apple_signal.h"), "-fsyntax-only", str(source)]
    return run(command, temp)


def load_entries(commands, root):
    """The recorded compile command of every target, keyed by repo-relative path."""
    entries = {}
    for entry in json.loads(commands.read_text(encoding="utf-8")):
        try:
            relative = pathlib.Path(entry["file"]).resolve().relative_to(root).as_posix()
        except ValueError:
            continue      # a generated or out-of-tree source is not one of the targets
        if relative in TARGETS:
            entries[relative] = entry
    return entries


def prepare_overlay(cxx, temp):
    """Write the Apple model; empty __THROW only where the host declares it noexcept."""
    overlay = temp / "apple"
    (overlay / "sys").mkdir(parents=True)
    (overlay / "apple_signal.h").write_text(APPLE_SIGNAL_H, encoding="utf-8")
    if host_sigaction_is_noexcept(cxx, temp):
        (overlay / "sys" / "cdefs.h").write_text(CDEFS_SHADOW, encoding="utf-8")
    return overlay


def preflight(args, root, build):
    """The recorded commands to compile, or None after saying why they are unusable."""
    commands = build / "compile_commands.json"
    if not commands.is_file():
        fail("no compile_commands.json at %s" % commands)
        return None
    if not is_clang(args.cxx):
        fail("a Clang-family compiler is required (hosted macOS runs AppleClang); got %s" % args.cxx)
        return None
    entries = load_entries(commands, root)
    missing = [target for target in TARGETS if target not in entries]
    if missing:
        fail("no recorded compile command for " + ", ".join(missing))
        return None
    return entries


def require(cxx, overlay, source, temp, label, compiles):
    """Compile one source under the model and require it to compile (or not)."""
    result = compile_probe(cxx, [], overlay, source, temp)
    if (result.returncode == 0) == compiles:
        print("PASS " + label)
        return True
    return fail(label + (" but it must be rejected" if compiles else " but it must compile"), result)


def model_checks(cxx, overlay, temp):
    """The model is in force, and it separates the pre-fix shape from the fixed one."""
    checks = []
    assertion = temp / "model-assertion.cpp"
    assertion.write_text(MODEL_ASSERTION, encoding="utf-8")
    checks.append(require(cxx, overlay, assertion, temp,
                          "the Apple signal model is in force", True))
    for name, text, compiles in (("old", OLD_SHAPE, False), ("new", NEW_SHAPE, True)):
        fixture = temp / ("fixture-%s.cpp" % name)
        fixture.write_text(text, encoding="utf-8")
        checks.append(require(cxx, overlay, fixture, temp,
                              "%s shape under the model" % name, compiles))
    return checks


def target_checks(cxx, overlay, root, entries, temp):
    """The two REAL translation units compile under Apple's <signal.h>."""
    checks = []
    for target in TARGETS:
        result = compile_probe(cxx, recorded_flags(entries[target]),
                               overlay, root / target, temp)
        if result.returncode == 0:
            print("PASS %s compiles under Apple's <signal.h>" % target)
            checks.append(True)
        else:
            checks.append(fail("%s does not compile under Apple's <signal.h>" % target, result))
    return checks


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=pathlib.Path, required=True)
    parser.add_argument("--root", type=pathlib.Path, default=pathlib.Path(__file__).resolve().parents[1])
    parser.add_argument("--cxx", default="clang++")
    args = parser.parse_args()
    root, build = args.root.resolve(), args.build_dir.resolve()
    entries = preflight(args, root, build)
    if entries is None:
        return 1

    with tempfile.TemporaryDirectory(prefix="zene-apple-signal-") as directory:
        temp = pathlib.Path(directory)
        overlay = prepare_overlay(args.cxx, temp)
        checks = model_checks(args.cxx, overlay, temp)
        checks += target_checks(args.cxx, overlay, root, entries, temp)

    print("RESULT %d/%d checks passed" % (sum(checks), len(checks)))
    return 0 if all(checks) else 1


if __name__ == "__main__":
    sys.exit(main())
