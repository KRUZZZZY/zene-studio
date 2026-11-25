#!/usr/bin/env python3
"""Mutation runner for cluster new-p9-276 (bounded WinMM MIDI output).

Each patch in this folder is an API-preserving edit of include/MidiWinMMOutput.h that
re-introduces one way the fix can be undone. For every mutant the runner

  1. applies the patch to include/MidiWinMMOutput.h (the original is first copied to a scratch
     file in --scratch and is always restored, also on error),
  2. builds MidiWinMMShortMessageTest with CCACHE_DISABLE=1 and asserts the BUILD SUCCEEDED,
     so a "failure" can never be a compile error,
  3. runs the listed test slots and asserts that exactly the predicted slots fail, and that each
     failure is a QCOMPARE / QVERIFY failure ("Compared values are not the same" or
     "returned FALSE"), not a crash, a hang (timeout) or any other kind of FAIL!.

Slots that would hang under a mutant (for example a test that waits for a worker that the
mutant removed) are not run for that mutant; they are listed in SKIPPED below with the reason.
A final baseline run on the restored header shows every slot passing.

Usage: run-mutations.py --build-dir BUILD --scratch DIR [--jobs 4] [--only NAME ...]
Exit status 0 only if every assertion above held.
"""

import argparse
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
HEADER = os.path.join(ROOT, "include", "MidiWinMMOutput.h")
TARGET = "MidiWinMMShortMessageTest"

PACKING = [
    "centredBendIsCentre",
    "bendLowAndHighBytesForKnownValues",
    "everyBendRoundTripsThroughTheInputContract",
    "otherChannelMessagesKeepTheirDataBytes",
    "bendKeepsStatusAndChannel",
]
ROUTING = "publishedRoutingDrivesTheQueueToTheSubscribedDevices"
SUBSCRIBE = "subscriptionChangesTakeEffectImmediately"
FULL = "queueFullDropsNewestAndCounts"
ALLOC = "submissionIsAllocationFree"
OFFTHREAD = "workerSendsOffTheSubmittingThread"
DISCARD = "clearDevicesDiscardsQueuedCommandsAndStopsSends"
INFLIGHT = "clearDevicesWaitsForInFlightSink"
ALL = PACKING + [ROUTING, SUBSCRIBE, FULL, ALLOC, OFFTHREAD, DISCARD, INFLIGHT]

# name -> (slots to run, slots that must FAIL, why the rest are skipped)
MUTANTS = {
    "inline-send": (
        [s for s in ALL if s != INFLIGHT],
        {ROUTING, SUBSCRIBE, FULL, ALLOC, OFFTHREAD, DISCARD},
        {INFLIGHT: "the inline send blocks inside the gated driver call on the submitting thread"},
    ),
    "allocating-search": (ALL, {ALLOC}, {}),
    "publish-noop": (
        [s for s in ALL if s not in (OFFTHREAD, INFLIGHT)],
        {ROUTING, SUBSCRIBE, FULL, ALLOC, DISCARD},
        {OFFTHREAD: "nothing is routed, so the sink never signals and the test would wait forever",
         INFLIGHT: "nothing is routed, so the worker never enters the gated sink"},
    ),
    "no-generation-bump": (ALL, {DISCARD}, {}),
    "no-inflight-wait": (ALL, {INFLIGHT}, {}),
    "overflow-uncounted": (ALL, {FULL}, {}),
    "overflow-drains-inline": (ALL, {FULL}, {}),
}

SLOT_LINE = re.compile(r"^(PASS|FAIL!|SKIP|XFAIL|XPASS)\s*:\s*\w+::(\w+)\(")
FAIL_REASON = re.compile(r"Compared values are not the same|returned FALSE")


def run(cmd, log, env=None, timeout=None, cwd=None):
    with open(log, "w") as out:
        try:
            return subprocess.run(cmd, stdout=out, stderr=subprocess.STDOUT, env=env,
                                  timeout=timeout, cwd=cwd).returncode
        except subprocess.TimeoutExpired:
            out.write("\nRUNNER: timeout expired\n")
            return 124


def build(args, tag):
    env = dict(os.environ, CCACHE_DISABLE="1")
    log = os.path.join(args.scratch, "build-%s.log" % tag)
    rc = run(["cmake", "--build", args.build_dir, "--target", TARGET, "-j%d" % args.jobs], log, env=env)
    return rc, log


def run_slots(args, tag, slots):
    log = os.path.join(args.scratch, "test-%s.log" % tag)
    binary = os.path.join(args.build_dir, "tests", TARGET)
    rc = run([binary] + slots, log, timeout=args.timeout, cwd=os.path.join(args.build_dir, "tests"))
    results, reasons = {}, {}
    last_fail = None
    with open(log, errors="replace") as f:
        for line in f:
            m = SLOT_LINE.match(line)
            if m:
                results[m.group(2)] = m.group(1)
                last_fail = m.group(2) if m.group(1) == "FAIL!" else None
                if last_fail:
                    reasons[last_fail] = bool(FAIL_REASON.search(line))
            elif last_fail and line.startswith("   "):
                continue
            elif last_fail and FAIL_REASON.search(line):
                reasons[last_fail] = True
    return rc, results, reasons, log


def check(name, slots, expected_fail, rc, results, reasons):
    problems = []
    if rc == 124:
        problems.append("the test run timed out (a hang is not a predicted failure)")
    for slot in slots:
        got = results.get(slot)
        want = "FAIL!" if slot in expected_fail else "PASS"
        if got != want:
            problems.append("%s: expected %s, saw %s" % (slot, want, got))
        if got == "FAIL!" and not reasons.get(slot):
            problems.append("%s: failed, but not through QCOMPARE/QVERIFY" % slot)
    return problems


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build-dir", required=True)
    ap.add_argument("--scratch", required=True, help="folder for the header backup and the logs")
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--timeout", type=int, default=300)
    ap.add_argument("--only", nargs="*", default=None)
    args = ap.parse_args()
    os.makedirs(args.scratch, exist_ok=True)

    backup = os.path.join(args.scratch, "MidiWinMMOutput.h.orig")
    shutil.copyfile(HEADER, backup)
    failures = []

    def step(label, slots, expected_fail, tag):
        rc, blog = build(args, tag)
        if rc != 0:
            failures.append("%s: BUILD FAILED (exit %d, %s)" % (label, rc, blog))
            print("FAIL  %-24s build failed, exit %d (%s)" % (label, rc, blog))
            return
        rc, results, reasons, tlog = run_slots(args, tag, slots)
        problems = check(label, slots, expected_fail, rc, results, reasons)
        failed = sorted(s for s, r in results.items() if r == "FAIL!")
        if problems:
            failures.extend("%s: %s" % (label, p) for p in problems)
            print("FAIL  %-24s %s (%s)" % (label, "; ".join(problems), tlog))
        else:
            print("ok    %-24s build ok; failing slots: %s (%s)" % (label, ", ".join(failed) or "none", tlog))

    try:
        step("baseline", ALL, set(), "baseline")
        for name, (slots, expected_fail, skipped) in MUTANTS.items():
            if args.only and name not in args.only:
                continue
            patch = os.path.join(HERE, name + ".patch")
            shutil.copyfile(backup, HEADER)
            rc = subprocess.run(["git", "apply", "--check", patch], cwd=ROOT).returncode
            if rc == 0:
                rc = subprocess.run(["git", "apply", patch], cwd=ROOT).returncode
            if rc != 0:
                failures.append("%s: the patch does not apply" % name)
                print("FAIL  %-24s patch does not apply" % name)
                continue
            for slot, why in skipped.items():
                print("      %-24s skips %s: %s" % (name, slot, why))
            step(name, slots, expected_fail, name)
    finally:
        shutil.copyfile(backup, HEADER)

    # the restored header must build and pass everything again
    step("restored-baseline", ALL, set(), "restored")
    if subprocess.run(["cmp", "-s", backup, HEADER]).returncode != 0:
        failures.append("the header was not restored")

    print()
    if failures:
        print("RESULT: FAIL")
        for f in failures:
            print("  -", f)
        return 1
    print("RESULT: PASS - every mutant built and failed exactly the predicted slots")
    return 0


if __name__ == "__main__":
    sys.exit(main())
