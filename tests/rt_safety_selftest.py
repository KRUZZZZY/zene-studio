#!/usr/bin/env python3
"""rt_safety_selftest.py - the rt-safety sweep's own controls.

A sweep that cannot fail proves nothing (row 52 of docs/FEATURE-LIST-0.3.0.md:
"It must FAIL on a deliberately introduced allocation (the positive control) or
it proves nothing"). This file is that control, and it is the ctest
RtSafetySelfTest.

Everything here is SYNTHESISED in a temp directory - fixture C++ sources, a
fixture scope, a fixture allowlist - so the test needs no build, no engine, no
socket and no compiler, and its inputs are exact. It drives the same decision core
the real sweep uses (tests/rt_safety_lib.py, and the real CLI where the exit code
IS the claim).

THE CONTROLS
------------
Positive (each MUST fail, and must name the offending path:symbol:rule):
  * a deliberate ALLOCATION on a declared audio-thread path   (new float[64])
  * a deliberate LOCK on a declared audio-thread path         (QMutexLocker)
  * a deliberate GROWTH on a declared audio-thread path       (push_back)
  * the same growth growing PAST an allowlist line's count
  * the CLI's own exit code, which is the number a gate records
Negative (each MUST pass):
  * a declared path with no forbidden construct
  * the deliberate allocation WITH an allowlist line and a reason
  * a member function defined in-class (declared Class::method)
Bound (the sweep MUST stay silent, because this is what it says it does not do):
  * the same allocation in a function the scope does NOT declare
  * `new` inside a comment line and inside a string literal
Refused (the ledgers, where a 2 means no verdict was reached):
  * an empty scope (0 entries is an ERROR, never a pass)
  * an allowlist line with a blank reason, or an unknown rule id
  * a scope entry whose symbol resolves to nothing (exit 1: the ledger is stale)
  * a scope entry whose file is not in the tree (exit 2: the tree is wrong)
  * an allowlist line that no longer covers anything (exit 1)
  * an allowlist line for a symbol the scope does not declare (exit 1)

Usage: python3 tests/rt_safety_selftest.py     (exit 0 only if every control held)
"""

import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from rt_safety_lib import RULES, run_sweep  # noqa: E402  (path set above)

SWEEP = os.path.join(HERE, "rt-safety-sweep.py")

# --- fixture sources -------------------------------------------------------
# Each is a small, plausible audio-thread path. The CLEAN one is the shape the
# tree's own fork paths are written in; the others carry one deliberate hit.

CLEAN = """\
// A declared audio-thread path with nothing forbidden in it.
#include <cstdint>

namespace fixture
{

void renderStageMix(float* out, std::uint32_t frames) noexcept
{
    for (std::uint32_t i = 0; i < frames; ++i)
    {
        out[i] = out[i] * 0.5f;
    }
}

} // namespace fixture
"""

WITH_ALLOCATION = """\
void renderStageMix(float* out, std::uint32_t frames) noexcept
{
    float* scratch = new float[64];  // the positive control
    for (std::uint32_t i = 0; i < frames; ++i)
    {
        out[i] = scratch[i % 64];
    }
    delete[] scratch;
}
"""

WITH_LOCK = """\
void renderStageMix(float* out, std::uint32_t frames) noexcept
{
    QMutexLocker locker(&m_changeMutex);  // the locking control
    for (std::uint32_t i = 0; i < frames; ++i)
    {
        out[i] = out[i] * 0.5f;
    }
}
"""

WITH_GROWTH = """\
void renderStageMix(float* out, std::uint32_t frames) noexcept
{
    TrackList trackList;  // the growth control: a local container, pushed per period
    trackList.push_back(findTrack(0));
    for (std::uint32_t i = 0; i < frames; ++i)
    {
        out[i] = out[i] * 0.5f;
    }
}
"""

WITH_GROWTH_TWICE = WITH_GROWTH.replace("trackList.push_back(findTrack(0));",
                                        "trackList.push_back(findTrack(0));\n"
                                        "    trackList.push_back(findTrack(1));")

#: A clean declared path beside a function nobody declared: the second one is
#: deliberately dirty, and the sweep must stay silent about it - the scope is
#: DECLARED, not discovered, and that is this programme's stated bound.
CLEAN_PLUS_UNDECLARED_DIRT = CLEAN.replace(
    "} // namespace fixture",
    """void controlThreadHelper()
{
    float* scratch = new float[64];  // NOT on an audio-thread path: not declared
    (void)scratch;
}

} // namespace fixture""")

#: Both a comment and a string literal mention `new` and `push_back`; a sweep
#: that is fooled by either reports a hit that does not exist.
DECOYS = """\
void renderStageMix(float* out, std::uint32_t frames) noexcept
{
    // this path must not call new or push_back the way the old one did
    const char* note = "never new, never push_back, in the message a user reads";
    (void)note;
    for (std::uint32_t i = 0; i < frames; ++i)
    {
        out[i] = out[i] * 0.5f;
    }
}
"""

IN_CLASS = """\
class AudioEngine
{
public:
    void renderNextPeriod() noexcept
    {
        const auto lock = std::lock_guard{m_changeMutex};  // in-class definition
        (void)lock;
    }
};
"""

SCOPE_MIX = "src/core/Fixture.cpp\trenderStageMix\tfixture: the render stage, on the audio thread\n"
SCOPE_IN_CLASS = ("src/core/Fixture.cpp\tAudioEngine::renderNextPeriod\t"
                  "fixture: the render callback, defined in-class\n")


class Fixture:
    """A throwaway tree with one source file, a scope and an allowlist."""

    def __init__(self, root, source, scope_lines, allow_lines=()):
        os.makedirs(os.path.join(root, "src/core"), exist_ok=True)
        with open(os.path.join(root, "src/core/Fixture.cpp"), "w", encoding="utf-8") as handle:
            handle.write(source)
        self.root = root
        self.scope_lines = list(scope_lines)
        self.allow_lines = list(allow_lines)

    def scope_text(self):
        return "".join(self.scope_lines)

    def allow_text(self):
        return "".join(self.allow_lines)

    def verdict(self):
        return run_sweep(self.root, self.scope_text(), self.allow_text())

    def cli_exit_code(self):
        """The exit code the CLI really returns - the number a gate records."""
        scope = os.path.join(self.root, "scope.txt")
        allow = os.path.join(self.root, "allow.txt")
        for path, text in ((scope, self.scope_text()), (allow, self.allow_text())):
            with open(path, "w", encoding="utf-8") as handle:
                handle.write(text)
        argv = [sys.executable, SWEEP, "--tree", self.root, "--scope", scope,
                "--allowlist", allow, "--check"]
        process = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        return process.returncode, process.stdout.decode("utf-8", "replace")


def positive_controls(tmp, check):
    """The sweep MUST fail on each deliberate construct, and name it."""
    check_new_control(tmp, check)
    check_lock_control(tmp, check)
    check_growth_control(tmp, check)


def _first_named(verdict, fragment):
    """The single problem that names `fragment`, or '' when there is not one."""
    named = [problem for problem in verdict.problems if fragment in problem]
    return named[0] if len(named) == 1 else ""


def check_new_control(tmp, check):
    """The allocation positive control: `new` on a declared path."""
    verdict = Fixture(os.path.join(tmp, "alloc"), WITH_ALLOCATION, [SCOPE_MIX]).verdict()
    named = _first_named(verdict, "src/core/Fixture.cpp:renderStageMix:alloc-new")
    check("POSITIVE CONTROL: a deliberate `new` on an audio-thread path FAILS",
          verdict.exit_code == 1 and bool(named),
          "exit %d, %d problem(s): %s" % (verdict.exit_code, len(verdict.problems),
                                          (named or "-")[:96]))


def check_lock_control(tmp, check):
    """The locking positive control: a QMutexLocker on a declared path."""
    verdict = Fixture(os.path.join(tmp, "lock"), WITH_LOCK, [SCOPE_MIX]).verdict()
    named = _first_named(verdict, ":lock-guard")
    check("POSITIVE CONTROL: a deliberate QMutexLocker on an audio-thread path FAILS",
          verdict.exit_code == 1 and bool(named), "exit %d, %s" % (verdict.exit_code,
                                                                   (named or "-")[:96]))


def check_growth_control(tmp, check):
    """The growth positive control: container growth on a declared path."""
    verdict = Fixture(os.path.join(tmp, "growth"), WITH_GROWTH, [SCOPE_MIX]).verdict()
    named = _first_named(verdict, ":grow-container")
    check("POSITIVE CONTROL: a deliberate container growth on an audio-thread path FAILS",
          verdict.exit_code == 1 and bool(named), "exit %d, %s" % (verdict.exit_code,
                                                                   (named or "-")[:96]))


def growth_and_cli_controls(tmp, check):
    """The ratchet bites, and the number a gate records is the CLI's own."""
    grown = Fixture(os.path.join(tmp, "grown"), WITH_GROWTH_TWICE, [SCOPE_MIX],
                    ["src/core/Fixture.cpp\trenderStageMix\tgrow-container\t1\t"
                     "accepted once, before the second push_back\n"])
    verdict = grown.verdict()
    check("POSITIVE CONTROL: a hit that grows past its allowlist count FAILS",
          verdict.exit_code == 1 and any("GREW" in p for p in verdict.problems),
          "exit %d, %s" % (verdict.exit_code,
                           next((p[:96] for p in verdict.problems if "GREW" in p), "-")))

    # The CLI's own exit code, not only the library's verdict: a gate records the
    # number the process returned.
    alloc = Fixture(os.path.join(tmp, "cli-alloc"), WITH_ALLOCATION, [SCOPE_MIX])
    code, output = alloc.cli_exit_code()
    check("... and the CLI's OWN exit code is 1 (the number a gate records)",
          code == 1 and "NEW rt-safety hit" in output, "exit %d" % code)


def negative_controls(tmp, check):
    """The sweep MUST pass on a clean path, and on a reasoned exception."""
    clean = Fixture(os.path.join(tmp, "clean"), CLEAN, [SCOPE_MIX])
    verdict = clean.verdict()
    check("NEGATIVE CONTROL: a declared path with no forbidden construct PASSES",
          verdict.exit_code == 0 and not verdict.problems,
          "exit %d, 0 problem(s), %d hit(s)" % (verdict.exit_code, len(verdict.hits)))

    accepted = Fixture(os.path.join(tmp, "accepted"), WITH_ALLOCATION, [SCOPE_MIX],
                       ["src/core/Fixture.cpp\trenderStageMix\talloc-new\t1\t"
                        "fixture: accepted with a reason\n"])
    verdict = accepted.verdict()
    check("NEGATIVE CONTROL: the same allocation WITH an allowlist line PASSES",
          verdict.exit_code == 0, "exit %d, allowed %d, fresh %d"
          % (verdict.exit_code, verdict.stats["allowlisted"], verdict.stats["fresh"]))

    in_class = Fixture(os.path.join(tmp, "inclass"), IN_CLASS, [SCOPE_IN_CLASS])
    verdict = in_class.verdict()
    check("the in-class definition form resolves (declared Class::method, written unqualified)",
          verdict.exit_code == 1 and verdict.stats["resolutions"]["in-class"] == 1,
          "exit %d, %d in-class resolution(s)"
          % (verdict.exit_code, verdict.stats["resolutions"]["in-class"]))


def bound_controls(tmp, check):
    """What the sweep says it does NOT do: it must stay silent, provably."""
    elsewhere = Fixture(os.path.join(tmp, "elsewhere"), CLEAN_PLUS_UNDECLARED_DIRT,
                        [SCOPE_MIX])
    verdict = elsewhere.verdict()
    check("BOUND: the same allocation OUTSIDE the declared scope is NOT seen",
          verdict.exit_code == 0 and len(verdict.hits) == 0,
          "exit %d, %d hit(s) - the scope is declared, not discovered"
          % (verdict.exit_code, len(verdict.hits)))

    decoys = Fixture(os.path.join(tmp, "decoys"), DECOYS, [SCOPE_MIX])
    verdict = decoys.verdict()
    check("BOUND: `new` in a comment and in a string literal is not a hit",
          verdict.exit_code == 0 and len(verdict.hits) == 0,
          "exit %d, %d hit(s)" % (verdict.exit_code, len(verdict.hits)))


def ledger_controls(tmp, check):
    """The ledgers parse-refuse what they cannot justify."""
    empty = Fixture(os.path.join(tmp, "empty"), CLEAN, [])
    verdict = empty.verdict()
    check("an EMPTY scope is exit 2 (0 entries is an error, never a pass)",
          verdict.exit_code == 2 and "ERROR" in " ".join(verdict.problems),
          "exit %d, %s" % (verdict.exit_code, verdict.problems[0][:80]))

    blank = Fixture(os.path.join(tmp, "blank"), WITH_ALLOCATION, [SCOPE_MIX],
                    ["src/core/Fixture.cpp\trenderStageMix\talloc-new\t1\t\n"])
    verdict = blank.verdict()
    check("an allowlist line with a BLANK reason is exit 2",
          verdict.exit_code == 2 and "no reason" in " ".join(verdict.problems),
          "exit %d, %s" % (verdict.exit_code, verdict.problems[0][:80]))

    unknown = Fixture(os.path.join(tmp, "unknown-rule"), WITH_ALLOCATION, [SCOPE_MIX],
                      ["src/core/Fixture.cpp\trenderStageMix\talloc-nope\t1\treason\n"])
    verdict = unknown.verdict()
    check("an allowlist line naming an unknown rule id is exit 2",
          verdict.exit_code == 2 and "not a rule id" in " ".join(verdict.problems),
          "exit %d, %s" % (verdict.exit_code, verdict.problems[0][:80]))


def staleness_controls(tmp, check):
    """A ledger line whose subject is gone is a failure, not a silent skip."""
    stale_symbol = Fixture(os.path.join(tmp, "stale-symbol"), CLEAN,
                           ["src/core/Fixture.cpp\trenderStageMix\tok\n",
                            "src/core/Fixture.cpp\tdeletedFunction\tthis function is gone\n"])
    verdict = stale_symbol.verdict()
    check("a scope entry whose symbol resolves to nothing is exit 1",
          verdict.exit_code == 1 and "resolves to no body" in " ".join(verdict.problems),
          "exit %d, %s" % (verdict.exit_code, verdict.problems[0][:80]))

    missing = Fixture(os.path.join(tmp, "missing-file"), CLEAN,
                      ["src/core/Gone.cpp\trenderStageMix\tnot in the tree\n"])
    verdict = missing.verdict()
    check("a scope entry whose FILE is not in the tree is exit 2",
          verdict.exit_code == 2 and "no such file" in " ".join(verdict.problems),
          "exit %d, %s" % (verdict.exit_code, verdict.problems[0][:80]))

    stale_allow = Fixture(os.path.join(tmp, "stale-allow"), CLEAN, [SCOPE_MIX],
                          ["src/core/Fixture.cpp\trenderStageMix\talloc-new\t1\t"
                           "the hit this accepted is gone\n"])
    verdict = stale_allow.verdict()
    check("an allowlist line that no longer covers anything is exit 1",
          verdict.exit_code == 1 and "STALE allowlist line" in " ".join(verdict.problems),
          "exit %d, %s" % (verdict.exit_code, verdict.problems[0][:80]))

    undeclared = Fixture(os.path.join(tmp, "undeclared"), CLEAN, [SCOPE_MIX],
                         ["src/core/Fixture.cpp\tsomeOtherSymbol\talloc-new\t1\t"
                          "the scope does not declare this symbol\n"])
    verdict = undeclared.verdict()
    check("an allowlist line for a symbol the scope does not declare is exit 1",
          verdict.exit_code == 1
          and any("scope file does not declare" in p for p in verdict.problems),
          "exit %d, %s" % (verdict.exit_code, next((p[:96] for p in verdict.problems
                                                   if "scope file does not declare" in p), "-")))


def rule_controls(check):
    """The rule set itself covers the three words of the AGENTS.md rule."""
    categories = sorted({rule.category for rule in RULES})
    check("the rule set covers the AGENTS.md rule's three words",
          categories == ["allocation", "growth", "locking"],
          "rules: %s" % ", ".join(rule.id for rule in RULES))


def main():
    tmp = tempfile.mkdtemp(prefix="rt-safety-selftest-")
    failures = []

    def check(name, ok, evidence):
        print("  %-4s %-68s %s" % ("PASS" if ok else "FAIL", name, evidence))
        if not ok:
            failures.append(name)

    try:
        positive_controls(tmp, check)
        growth_and_cli_controls(tmp, check)
        negative_controls(tmp, check)
        bound_controls(tmp, check)
        ledger_controls(tmp, check)
        staleness_controls(tmp, check)
        rule_controls(check)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    print("")
    if failures:
        print("RESULT: FAIL - %d control(s) did not hold: %s" % (len(failures), failures))
        return 1
    print("RESULT: PASS - the sweep fails on a deliberate allocation, lock and growth, "
          "names each one, passes a clean path, and stays silent exactly where its "
          "stated bound says it does not look.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
