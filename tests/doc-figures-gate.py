#!/usr/bin/env python3
"""doc-figures-gate.py - a document's CURRENT command-surface figure must be the measured one.

WHY. The command count is quoted in many places, and they drift: on 2026-09-28 the docs gave
87, 334, 340, 350 and 351 for the same surface, and README.md still said 332. Historical records
(release notes, dated audits) are allowed to keep the figure that was true when they were
written, so the gate does not grep for numbers. Instead a document marks the figure it claims is
CURRENT:

    <!-- canary -->353 ids / 57 groups<!-- /canary -->

(the comment markers render invisibly), and this gate checks every marked figure against the
committed snapshot, tools/mcp-zene-control/zene_control/commands_snapshot.json - the file the
ControlCommandsSnapshot ctest already holds equal to the live binary. So one number is measured
once, and every current claim is read against it.

The documents that MUST carry a marker are listed in tests/doc-figures.txt (one path per line,
relative to --root), so a doc cannot pass by dropping its marker.

Usage:
    python3 tests/doc-figures-gate.py                        # this repo's own docs
    python3 tests/doc-figures-gate.py --root <dir> <doc>...  # e.g. the workspace's AGENTS.md
Exit: 0 every marked figure matches; 1 a mismatch or a required doc without a marker; 2 usage.
"""
import argparse
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
SNAPSHOT = os.path.join(REPO, "tools", "mcp-zene-control", "zene_control", "commands_snapshot.json")
MARK = re.compile(r"<!--\s*canary\s*-->(.*?)<!--\s*/canary\s*-->", re.S)
FIGURE = re.compile(r"(\d+)\s*(?:ids|commands|command ids)\D{0,20}?(\d+)\s*groups")


def measured():
    with open(SNAPSHOT) as fh:
        commands = json.load(fh)["commands"]
    return len(commands), len({c["group"] for c in commands})


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=REPO)
    parser.add_argument("docs", nargs="*")
    args = parser.parse_args()
    docs = args.docs
    if not docs:
        with open(os.path.join(HERE, "doc-figures.txt")) as fh:
            docs = [l.strip() for l in fh if l.strip() and not l.startswith("#")]
    ids, groups = measured()
    print("doc-figures-gate: snapshot measures %d ids / %d groups" % (ids, groups))
    failures = 0
    for doc in docs:
        path = os.path.join(args.root, doc)
        if not os.path.isfile(path):
            print("FAIL %s: no such file" % doc); failures += 1; continue
        with open(path, encoding="utf-8") as fh:
            text = fh.read()
        marks = MARK.findall(text)
        if not marks:
            print("FAIL %s: carries no <!-- canary --> figure" % doc); failures += 1; continue
        for mark in marks:
            m = FIGURE.search(mark)
            if not m:
                print("FAIL %s: marked text %r holds no 'N ids / M groups' figure" % (doc, mark)); failures += 1
            elif (int(m.group(1)), int(m.group(2))) != (ids, groups):
                print("FAIL %s: claims %s ids / %s groups, the snapshot measures %d / %d"
                      % (doc, m.group(1), m.group(2), ids, groups)); failures += 1
            else:
                print("ok   %s: %s" % (doc, mark.strip()))
    print("RESULT: %s" % ("PASS" if not failures else "FAIL (%d)" % failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
