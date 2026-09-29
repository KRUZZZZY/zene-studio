#!/usr/bin/env python3
# checked-coverage.py - R6.3's ratchet: every command id the suite holds to its resultSchema (gate 1)
#
# Copyright (c) 2026 Zene Studio contributors
#
# This file is part of LMMS - https://lmms.io
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public
# License as published by the Free Software Foundation; either
# version 2 of the License, or (at your option) any later version.
"""Which commands has the test suite seen SUCCEED under result checks?

`ZENE_CONTROL_CHECKED_LOG=<file>` (src/core/ControlSchema.cpp, noteChecked) makes every
process append each command id whose successful reply passed its own resultSchema. A whole
ctest run with it set is therefore the set of commands whose reply contract the suite holds.
This script compares that set with the release surface - the committed MCP snapshot, which
ControlCommandsSnapshot keeps equal to the binary - and ratchets the difference:

  * an id that is NOT reached and NOT in the ratchet file is a regression (a command lost its
    only checked success, or a new command arrived without one) -> exit 1;
  * an id that IS reached but still listed is a stale line (the ratchet only ever shrinks, and a
    fixed entry must leave the file in the same change) -> exit 1;
  * `--write` rewrites the ratchet from this measurement, keeping existing reasons (a recorded
    act, like the other ratchets' re-anchors: say why in the commit).

Usage: checked-coverage.py --log FILE [--snapshot FILE] [--ratchet FILE] [--write]
"""

import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_SNAPSHOT = os.path.join(HERE, "..", "tools", "mcp-zene-control", "zene_control",
                                "commands_snapshot.json")
DEFAULT_RATCHET = os.path.join(HERE, "checked-coverage-unreached.txt")


def snapshot_ids(path):
    with open(path, encoding="utf-8") as handle:
        return {entry["id"] for entry in json.load(handle)["commands"]}


def logged_ids(path):
    with open(path, encoding="utf-8") as handle:
        return {line.strip() for line in handle if line.strip()}


def ratchet_entries(path):
    entries = {}
    if not os.path.exists(path):
        return entries
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            if not line.strip() or line.startswith("#"):
                continue
            command, _, reason = line.rstrip("\n").partition("\t")
            entries[command.strip()] = reason.strip()
    return entries


def write_ratchet(path, unreached, reasons):
    header = ("# Commands the test suite has never seen SUCCEED under result checks (R6.3).\n"
              "# Measured by tests/checked-coverage.py over a full ctest run (gate 1). This\n"
              "# list only shrinks: reach a command's success path in a test, then delete its\n"
              "# line in the same change. <command.id><TAB><why it is not reached yet>\n")
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(header)
        for command in sorted(unreached):
            handle.write("%s\t%s\n" % (command, reasons.get(command) or "not yet reached"))


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--log", required=True)
    parser.add_argument("--snapshot", default=DEFAULT_SNAPSHOT)
    parser.add_argument("--ratchet", default=DEFAULT_RATCHET)
    parser.add_argument("--write", action="store_true")
    args = parser.parse_args()

    surface = snapshot_ids(args.snapshot)
    reached = logged_ids(args.log) & surface
    unreached = surface - reached
    listed = ratchet_entries(args.ratchet)
    print("checked-coverage: %d of %d commands reached their success path under result checks"
          % (len(reached), len(surface)))
    if args.write:
        write_ratchet(args.ratchet, unreached, listed)
        print("wrote %s (%d unreached)" % (args.ratchet, len(unreached)))
        return 0

    regressions = sorted(unreached - set(listed))
    stale = sorted(set(listed) & reached)
    gone = sorted(set(listed) - surface)
    for command in regressions:
        print("REGRESSION: %s has no checked success in this run and is not in %s"
              % (command, os.path.basename(args.ratchet)))
    for command in stale:
        print("STALE: %s is reached now - delete its line from %s" % (command, os.path.basename(args.ratchet)))
    for command in gone:
        print("STALE: %s is not a command any more - delete its line" % command)
    if regressions or stale or gone:
        print("FAIL: the checked-coverage ratchet moved (%d regressions, %d stale lines)"
              % (len(regressions), len(stale) + len(gone)))
        return 1
    print("PASS: %d unreached, every one of them listed" % len(unreached))
    return 0


if __name__ == "__main__":
    sys.exit(main())
