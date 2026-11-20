#!/usr/bin/env python3
"""Source gate for Q050-224.1: every in-scope engine worker registers the crash
reporter's alternate signal stack inside its THREAD-LAUNCH LAMBDA, before its
entry function can run.

Why a source gate.  The reporter installs its handlers with SA_ONSTACK, but
sigaltstack() is a per-thread attribute, so each thread must register its own
stack.  Three of the four in-scope launch sites have no callback a test can
inject into without shipping a test hook in the product:

    src/core/lv2/Lv2Worker.cpp        workerFunc()   (covered at runtime too:
                                                     tests/src/core/WorkerAltStackTest.cpp)
    src/core/RubberBandStretch.cpp    run()          (voice reset on the recycler)
    src/core/StemJobManager.cpp       workerLoop()   (native inference backend)
    src/wasm/WasmWorkerPool.cpp       laneMain()     (guest DSP lanes)

So this gate is their coverage proof: it reads each real launch lambda and
requires

    crashreporter::installAlternateStackForCurrentThread();

to appear INSIDE the lambda, BEFORE the call to the entry function.  Two
negative probes keep the predicate honest - a missing registration and a
registration placed after the entry call must both be rejected - plus a third
for the same mistake one level out (registering in the enclosing function
instead of on the new thread) and a positive fixture that must be accepted.
The gate fails if any probe's verdict is wrong, so it cannot pass vacuously on
a file it failed to parse.

The check is deliberately structural rather than a textual diff: it does not
care about formatting, and it names the entry call it expected to see, so a
launch site that quietly stops using `std::thread` (or starts a second one) is
reported instead of silently skipped.

Usage: worker-altstack-registration-check.py [--root <source tree>]
Exit 0 only when all four launch sites are correct and every probe agrees.
"""

import argparse
import pathlib
import re
import sys

REGISTRATION = "crashreporter::installAlternateStackForCurrentThread();"

#: (file, entry function the launch lambda must call after registering)
SITES = (
    ("src/core/lv2/Lv2Worker.cpp", "workerFunc"),
    ("src/core/RubberBandStretch.cpp", "run"),
    ("src/core/StemJobManager.cpp", "workerLoop"),
    ("src/wasm/WasmWorkerPool.cpp", "laneMain"),
)


class GateError(Exception):
    """A source shape this gate cannot prove - always a failure, never a skip."""


def _skip_literal(text, index):
    """Index just past a string/char literal starting at *index*."""
    quote = text[index]
    i = index + 1
    while i < len(text):
        if text[i] == "\\":
            i += 2
            continue
        if text[i] == quote:
            return i + 1
        i += 1
    return len(text)


def call_arguments(text, open_index):
    """The text between the parentheses of the call opened at *open_index*."""
    depth = 0
    i = open_index
    while i < len(text):
        ch = text[i]
        if ch in "\"'":
            i = _skip_literal(text, i)
            continue
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
            if depth == 0:
                return text[open_index + 1:i]
        i += 1
    raise GateError("unbalanced parentheses in a std::thread launch")


def first_brace_block(text):
    """The body of the first {...} block in *text*, or None when there is none."""
    depth = 0
    start = -1
    i = 0
    while i < len(text):
        ch = text[i]
        if ch in "\"'":
            i = _skip_literal(text, i)
            continue
        if ch == "{":
            if depth == 0:
                start = i
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0 and start >= 0:
                return text[start + 1:i]
        i += 1
    return None


def strip_comments(text):
    """*text* without C/C++ comments (string literals left intact).

    The check must read CODE: a comment that mentions the entry call before the
    registration does not move the registration, and a commented-out call must
    not count as one.
    """
    out = []
    i = 0
    while i < len(text):
        ch = text[i]
        if ch in "\"'":
            end = _skip_literal(text, i)
            out.append(text[i:end])
            i = end
            continue
        if text.startswith("//", i):
            end = text.find("\n", i)
            i = len(text) if end < 0 else end
            continue
        if text.startswith("/*", i):
            end = text.find("*/", i + 2)
            i = len(text) if end < 0 else end + 2
            continue
        out.append(ch)
        i += 1
    return "".join(out)


def check_launch_body(body, entry):
    """Errors for one launch lambda body; empty means the site is correct."""
    code = strip_comments(body)
    registered_at = code.find(REGISTRATION)
    if registered_at < 0:
        return ["the launch lambda never calls " + REGISTRATION
                + " on the new thread"]
    call = re.search(r"\b" + re.escape(entry) + r"\s*\(", code)
    if call is None:
        return ["the launch lambda does not call " + entry + "()"]
    if registered_at > call.start():
        return ["the registration runs after " + entry + "() starts - a stack "
                "overflow inside the work is not covered"]
    return []


def launch_bodies(source):
    """Every {...} lambda body passed to a std::thread(...) in *source*."""
    bodies = []
    for match in re.finditer(r"std::thread\s*\(", source):
        arguments = call_arguments(source, match.end() - 1)
        body = first_brace_block(arguments)
        if body is not None:
            bodies.append(body)
    return bodies


def check_source(relative, entry, source):
    """Errors for one real file; empty means the site is correct."""
    bodies = launch_bodies(source)
    if not bodies:
        return ["no std::thread launch with a lambda body found"]
    launches = [body for body in bodies
                if re.search(r"\b" + re.escape(entry) + r"\s*\(", body)]
    if not launches:
        return ["no std::thread launch lambda calls " + entry + "()"]
    errors = []
    for body in launches:
        errors.extend(check_launch_body(body, entry))
    return errors


#: (name, fixture source, entry, must_pass)
PROBES = (
    ("missing registration",
     "m_thread = std::thread([this] { run(); });", "run", False),
    ("registration after the work",
     "m_thread = std::thread([this] { run(); "
     + REGISTRATION + " });", "run", False),
    ("registration outside the lambda (on the caller's thread)",
     "void start() { " + REGISTRATION + " "
     "m_thread = std::thread([this] { run(); }); }", "run", False),
    ("registration before the work (positive fixture)",
     "m_thread = std::thread([this] { " + REGISTRATION + " run(); });",
     "run", True),
)


def run(argv=None):
    parser = argparse.ArgumentParser(
        description="Check the alternate-stack registration in every engine "
                    "worker's thread-launch lambda.")
    parser.add_argument("--root", type=pathlib.Path,
                        default=pathlib.Path(__file__).resolve().parent.parent,
                        help="source tree (default: the tree this script is in)")
    args = parser.parse_args(argv)
    root = args.root.resolve()

    failures = 0
    for relative, entry in SITES:
        path = root / relative
        if not path.is_file():
            print("FAIL %s: missing file" % relative, file=sys.stderr)
            failures += 1
            continue
        try:
            errors = check_source(relative, entry, path.read_text(encoding="utf-8"))
        except GateError as error:
            print("FAIL %s: %s" % (relative, error), file=sys.stderr)
            failures += 1
            continue
        if errors:
            for error in errors:
                print("FAIL %s: %s" % (relative, error), file=sys.stderr)
            failures += 1
        else:
            print("PASS %s: launch lambda registers before %s()" % (relative, entry))

    for name, fixture, entry, must_pass in PROBES:
        errors = check_source("probe", entry, fixture)
        passed = not errors
        if passed != must_pass:
            verdict = "accepted" if passed else "rejected"
            print("FAIL probe '%s': the predicate %s it" % (name, verdict),
                  file=sys.stderr)
            failures += 1
        else:
            print("PASS probe '%s': %s"
                  % (name, "accepted" if passed else "rejected as it must be"))

    if failures:
        print("%d check(s) failed" % failures, file=sys.stderr)
        return 1
    print("all four launch sites register before their work; every probe agrees")
    return 0


if __name__ == "__main__":
    sys.exit(run())
