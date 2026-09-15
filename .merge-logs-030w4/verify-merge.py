#!/usr/bin/env python3
"""Prove NO SILENT LOSS in a merge: every line either side ADDED relative to the
merge base must still be present in the merged tree, unless the line is a member
of a file the train records as re-measured/re-emitted at the tip.

A clean auto-merge is not a union - this is the check for that.

usage: verify-merge.py <label> <base> <ours> <theirs> <merged>
"""
import difflib
import subprocess
import sys

# files whose content is MEASURED or REGENERATED at the tip, or whose rows are
# resolved by a recorded union rule (their lines legitimately change text):
SKIP = {
    "tests/src/core/ReversibilityContractTest.cpp",
    "tests/upstream-modifications.txt",
    "tests/fork-sources.txt",
    "tests/all-sources.txt",
    "tests/tools-sources.txt",
    # the A16 JOIN list: its id set is unioned into ONE statement, so the merged
    # line is not byte-equal to either side's wrap of it (union proof: the
    # resolver prints "join union: ours N ids + theirs M -> K", and the id-set
    # union is what the file means).
    "src/core/ControlReversibilityTable.cpp",
    # SECTION 5a: this train's own fix gated the ramp publish on
    # `automationMode() != AutomationMode::Off`, so theirs' added line
    # `if( model != nullptr )` is deliberately REPLACED by the gated condition.
    # That is the one file where a line a lane added is not expected verbatim;
    # tests/src/core/SampleAccurateAutomationTest.cpp (the test that fails if
    # the ramp still applies in Off) is the same commit's proof.
    "src/core/Song.cpp",
    # DOCS DECLARED AT THE MERGE POINT (merge 8, 030/audit):
    #  * LANE-STATE.md is a LANE-OWNED scratch record, not a shared registry -
    #    each lane overwrites it with its own state, so "union" has no meaning
    #    and the incoming lane's copy is the file (ours stays in git history).
    #  * docs/COVERAGE-MATRIX-2026-09-13.md was rewritten on both sides: ours is
    #    the NEWER measurement (0ee78abed), theirs the audit's tip-pinned
    #    snapshot plus its SUPERSEDED banner and its new section. The merge kept
    #    ours' body and carried their banner + their extra section.
    "LANE-STATE.md",
    "docs/COVERAGE-MATRIX-2026-09-13.md",
    "tools/mcp-zene-control/zene_control/commands_snapshot.json",
    "docs/RELEASE-NOTES-v0.3.0-alpha.md",
}
MAXSHOW = 6


def ls_tree(rev):
    out = subprocess.run(["git", "ls-tree", "-r", "--name-only", rev],
                         capture_output=True, text=True, check=True).stdout
    return set(out.split("\n"))


def blob(rev, path):
    out = subprocess.run(["git", "show", "%s:%s" % (rev, path)],
                         capture_output=True, text=True)
    return None if out.returncode != 0 else out.stdout


def added(base, side):
    sm = difflib.SequenceMatcher(None, base, side, autojunk=False)
    out = []
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag in ("insert", "replace"):
            out.extend(side[j1:j2])
    return [l for l in out if l.strip()]


def main():
    label, base, ours, theirs, merged = sys.argv[1:6]
    print("== no-silent-loss check: %s" % label)
    print("   base=%s ours=%s theirs=%s merged=%s" % (base, ours, theirs, merged))
    changed = subprocess.run(["git", "diff", "--name-only", base, theirs],
                             capture_output=True, text=True, check=True).stdout.split("\n")
    changed_o = subprocess.run(["git", "diff", "--name-only", base, ours],
                               capture_output=True, text=True, check=True).stdout.split("\n")
    # only the files BOTH sides touched are merge risk: a file only theirs
    # changed is taken wholesale by the merge machinery
    both = sorted(set(f for f in changed if f) & set(f for f in changed_o if f))
    only_theirs = sorted(set(f for f in changed if f) - set(both))
    changed = both
    print("   files theirs changed = %d; both sides = %d; theirs-only = %d"
          % (len(set(f for f in changed_o) | set(f for f in changed if f)), len(both), len(only_theirs)))
    problems, checked, skipped = [], 0, []
    for f in changed:
        if f in SKIP:
            skipped.append(f)
            continue
        bt, ot, tt, mt = blob(base, f), blob(ours, f), blob(theirs, f), blob(merged, f)
        if tt is None or mt is None or ot is None or bt is None:
            if tt is None and mt is not None:
                problems.append("%s: theirs DELETED it, merged tree still has it" % f)
            elif tt is not None and mt is None:
                problems.append("%s: theirs has it, merged tree does NOT" % f)
            continue
        if "\0" in mt:
            continue                      # binary: line diff meaningless
        b = bt.split("\n")
        o = ot.split("\n")
        t = tt.split("\n")
        m = mt.split("\n")
        merged_lines = set(l.rstrip() for l in m)
        checked += 1
        for who, side in (("theirs", t), ("ours", o)):
            miss = [l for l in added(b, side) if l.rstrip() not in merged_lines]
            if miss:
                problems.append("%s: %d %s-added line(s) missing, e.g. %r"
                                % (f, len(miss), who, miss[:MAXSHOW]))
    print("   files changed by theirs = %d   line-checked = %d   skipped (re-measured/union-resolved) = %d"
          % (len(changed), checked, len(skipped)))
    for f in skipped:
        print("     skip: %s" % f)
    if problems:
        print("!! SILENT LOSS FOUND (%d)" % len(problems))
        for p in problems:
            print("   - %s" % p)
        return 1
    print("   OK: no added line from either side is missing from the merged tree")
    return 0


if __name__ == "__main__":
    sys.exit(main())
