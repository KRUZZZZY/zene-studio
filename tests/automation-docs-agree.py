#!/usr/bin/env python3
"""automation-docs-agree.py - the automation docs must say which clips are sample-accurate.

WHY (Q050-362). `docs/KNOWN-LIMITATIONS.md` kept the blanket line "Automation is not
sample-accurate" as a current limitation long after the ramp shipped. The engine does
reach the audio path per sample for a clip carrying a curve (`AutomationClip::sampleAccurate()`
resolves the default by progression type), a Discrete clip keeps its per-tick step, a
`CubicHermite` curve is approximated as one straight segment per tick, each per-block ramp
holds 32 knots (`AutomationRamp::MaxKnots`) with the surplus refused and counted, and a clip
in a pattern is refused (`Refused`) by `automation.ramp_set`. The engine test
`SampleAccurateAutomationTest` covers that behaviour; this gate is the DOCS' own regression
check, because a render test cannot see a document revert.

WHAT IT CHECKS (file reads only; no build, no instance, no network):
  1. `docs/KNOWN-LIMITATIONS.md`'s overview bullet points at the detailed feature-row-9
     section and states the default, the Discrete exception, the 32-knot bound and the
     pattern-clip refusal.
  2. `docs/SAMPLE-ACCURATE-AUTOMATION.md` still carries the capacity and the typed pattern
     refusal, and the exactness split between Linear/Discrete and CubicHermite.
  3. `docs/AUTOMATION-MODES.md` is marked superseded with a date rather than reading as the
     current account; `docs/STATUS.md` carries the same dated correction.
  4. `docs/FEATURE-LIST-0.3.0.md` row 9 says **in the tree**, not **to build**, and
     `data/manual/index.html` describes the behaviour.
  5. The old blanket claims do not appear as current statements in the checked pages. An
     occurrence is allowed only where the page marks it historical - a dated `Corrected` /
     `Superseded` note on the same line or within the following few lines, this repo's
     correction convention.

Dated measurements and reports (`docs/COVERAGE-*`, `docs/KNOWN-LIMITATIONS-v*`,
`docs/RELEASE-NOTES-v0.2*`, `docs/RELEASE-PREP-*`, `docs/AUDIT-*`,
`docs/INDEPENDENT-NOTES-READ.md`, `docs/reports/`, `docs/specs/`, the run logs and the
`docs/050-features/` records) are deliberately outside the sweep: they are records of the
trees they measured, and several necessarily QUOTE the sentence they replaced.

`--self-test` is the gate's own red/green control: it copies every checked page into a
throwaway root and proves the gate fails when the old blanket line returns, when the
32-knot or pattern-refusal sentence is dropped from KNOWN-LIMITATIONS.md, when the
FEATURE-LIST row slips back to **to build**, and when the manual paragraph goes.

Usage:  python3 tests/automation-docs-agree.py [--root DIR] [--self-test]
Exit:   0 every check holds; 1 a mismatch or a stale claim; 2 usage/setup error.
"""
import argparse
import contextlib
import io
import os
import re
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)

KL = "docs/KNOWN-LIMITATIONS.md"
SAA = "docs/SAMPLE-ACCURATE-AUTOMATION.md"
MODES = "docs/AUTOMATION-MODES.md"
STATUS = "docs/STATUS.md"
FEATURE_LIST = "docs/FEATURE-LIST-0.3.0.md"
MANUAL = "data/manual/index.html"
CAPABILITIES = "docs/CAPABILITIES-0.3.0.md"
NOTES = "docs/RELEASE-NOTES-v0.3.0-alpha.md"

REQUIRED = {
    KL: (
        "Automation clips carrying a curve are sample-accurate by default",
        "a Discrete clip keeps its step",
        "Each\n  ramp holds **32 knots**",
        "`automation.ramp_set` refuses it (`Refused`)",
        "Sample-accurate automation (`automation.ramp_set`, `automation.ramp_get`) — feature row 9",
        "`docs/SAMPLE-ACCURATE-AUTOMATION.md` is the design record",
    ),
    SAA: (
        "`MaxKnots` (32)",
        "`automation.ramp_set` **refuses** it typed (`Refused`)",
        "`Linear` and `Discrete` are exact",
        "`CubicHermite` (tangent-edited) curves are approximated",
    ),
    MODES: (
        "Superseded 2026-10-30 (Q050-362)",
        "Discrete clips remain stepped",
        "Each per-block ramp holds 32 knots",
        "`automation.ramp_set` refuses a clip in a pattern",
    ),
    STATUS: (
        "corrected 2026-10-30 (Q050-362)",
        "a Linear or\nCubicHermite clip is sample-accurate by default",
    ),
    FEATURE_LIST: (
        "| 9 | Sample-accurate automation | `automation.ramp_set`, `automation.ramp_get` | **in the tree**",
        "**32 knots** (`MaxKnots`)",
        "a clip in a **pattern** is refused (`Refused`)",
    ),
    MANUAL: (
        "render at sample precision by default",
        "Each audio block's ramp holds 32 knots",
        "a clip in a pattern is refused by\n<code>automation.ramp_set</code>",
    ),
}

# Pages a reader takes as the current account. The dated records listed in the module
# docstring are intentionally absent.
SWEPT = (KL, SAA, MODES, STATUS, FEATURE_LIST, MANUAL, CAPABILITIES, NOTES)

# The pre-ramp claims. Each is a failure unless the page marks it historical nearby.
FORBIDDEN = (
    "Automation is not sample-accurate",
    "sample-accurate playback is not",
    "sample-accurate automation is NOT in",
    "| 9 | Sample-accurate automation | none yet | **to build**",
    "Sample-accurate automation | NOT-STARTED | TO BUILD",
    "automation not sample-accurate",
    "Sample-accurate playback of volume/pan — not implemented",
    "it resolves once per tick",
)
ALLOWANCE_LINES = 8
CORRECTION = re.compile(
    r"Corrected 2026-10-30|Superseded 2026-10-30|corrected 2026-10-30|Q050-362"
    r"|pre-ramp record|dated record|the record of",
    re.I,
)


def read(root, relpath):
    with open(os.path.join(root, relpath), encoding="utf-8") as fh:
        return fh.read()


def check_required(root, failures):
    for relpath, phrases in sorted(REQUIRED.items()):
        try:
            text = read(root, relpath)
        except OSError as exc:
            print("FAIL %s: unreadable (%s)" % (relpath, exc))
            failures += 1
            continue
        for phrase in phrases:
            if phrase not in text:
                print("FAIL %s: the corrected statement is gone: %r" % (relpath, phrase))
                failures += 1
    return failures


def check_forbidden(root, failures):
    for relpath in SWEPT:
        try:
            lines = read(root, relpath).splitlines()
        except OSError as exc:
            print("FAIL %s: unreadable (%s)" % (relpath, exc))
            failures += 1
            continue
        for i, line in enumerate(lines):
            for phrase in FORBIDDEN:
                if phrase.lower() not in line.lower():
                    continue
                window = "\n".join(lines[i:i + ALLOWANCE_LINES + 1])
                if CORRECTION.search(window):
                    print("ok   %s:%d: historical claim, marked corrected in place: %r"
                          % (relpath, i + 1, phrase))
                else:
                    print("FAIL %s:%d: the old blanket claim is back as a current statement: %r"
                          % (relpath, i + 1, phrase))
                    failures += 1
    return failures


def run(root):
    failures = 0
    failures = check_required(root, failures)
    failures = check_forbidden(root, failures)
    return failures


def copy_pages(dst):
    for relpath in SWEPT:
        target = os.path.join(dst, relpath)
        os.makedirs(os.path.dirname(target), exist_ok=True)
        shutil.copyfile(os.path.join(REPO, relpath), target)


def rewrite(root, relpath, old, new):
    path = os.path.join(root, relpath)
    with open(path, encoding="utf-8") as fh:
        text = fh.read()
    if old not in text:
        raise RuntimeError("self-test setup: %r not found in %s" % (old, relpath))
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(text.replace(old, new, 1))


def quiet_check(root):
    with contextlib.redirect_stdout(io.StringIO()):
        return run(root)


def self_test():
    baseline = tempfile.mkdtemp(prefix="automation-docs-agree-")
    try:
        copy_pages(baseline)
        if quiet_check(baseline) != 0:
            print("SELF-TEST FAIL: the current pages do not pass the gate")
            return 1

        controls = (
            ("the old blanket line returns",
             KL,
             "Automation clips carrying a curve are sample-accurate by default; a Discrete clip keeps its step.",
             "Automation is not sample-accurate."),
            ("the 32-knot capacity is dropped",
             KL,
             "Each\n  ramp holds **32 knots**;",
             "Each\n  ramp holds a fixed capacity;"),
            ("the pattern refusal is dropped",
             KL,
             "`automation.ramp_set` refuses it (`Refused`).",
             "`automation.ramp_set` accepts it."),
            ("feature row 9 slips back to to-build",
             FEATURE_LIST,
             "| 9 | Sample-accurate automation | `automation.ramp_set`, `automation.ramp_get` | **in the tree**",
             "| 9 | Sample-accurate automation | none yet | **to build**"),
            ("the manual paragraph goes",
             MANUAL,
             "Automation clips that carry a curve (Linear or CubicHermite) render at sample precision by default",
             "Automation clips render as they always have"),
            ("the superseded marker goes",
             MODES,
             "**Superseded 2026-10-30 (Q050-362):**",
             "**Note:**"),
        )
        failed = 0
        for label, relpath, old, new in controls:
            case = tempfile.mkdtemp(prefix="automation-docs-agree-case-")
            try:
                copy_pages(case)
                rewrite(case, relpath, old, new)
                if quiet_check(case) == 0:
                    print("SELF-TEST FAIL: %s was NOT refused (%s)" % (label, relpath))
                    failed += 1
                else:
                    print("ok   self-test: %s is refused" % label)
            finally:
                shutil.rmtree(case, ignore_errors=True)
        if failed:
            print("SELF-TEST FAIL: %d control(s) were not refused" % failed)
            return 1
        print("SELF-TEST PASS: the current pages pass; every stale/removed claim is refused.")
        return 0
    finally:
        shutil.rmtree(baseline, ignore_errors=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", default=REPO, help="checkout root (default: this repo)")
    parser.add_argument("--self-test", action="store_true", help="prove the gate can fail")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    root = os.path.abspath(args.root)
    if not os.path.isdir(root):
        print("automation-docs-agree: no such root: %s" % root)
        return 2
    failures = run(root)
    checked = sum(len(v) for v in REQUIRED.values())
    if failures:
        print("automation-docs-agree: %d check(s) failed over %d docs / %d required strings"
              % (failures, len(REQUIRED), checked))
        return 1
    print("automation-docs-agree: %d required statements and %d blanket-claim sweeps held over %d docs"
          % (checked, len(FORBIDDEN), len(REQUIRED)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
