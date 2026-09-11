#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""demo_check.py -- assert musical facts about a project file.

Used by tools/mmpz-git/depth-demo.sh to turn "the merge looked right" into a
checked exit code.  Reads whatever container the file is in.

Usage:
  demo_check.py FILE [check ...]

Checks:
  --has-note KEY:POS     a note with this key at this pos exists
  --no-note KEY:POS      no such note exists
  --has-track NAME       a track with this name exists
  --no-track NAME        no track with this name exists
  --notes N              the file holds exactly N notes

Exit: 0 if every check passed, 1 otherwise (each result is printed).
"""
import sys

import mmpz_git as M


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    path = argv[0]
    doc = M.parse(M.load_any(path))
    root = doc.documentElement
    assert root is not None
    notes = [(n.getAttribute("key"), n.getAttribute("pos"))
             for n in root.getElementsByTagName("note")]
    tracks = [t.getAttribute("name")
              for t in root.getElementsByTagName("track")]

    failures = 0
    i = 1
    while i < len(argv):
        opt = argv[i]
        val = argv[i + 1] if i + 1 < len(argv) else ""
        i += 2
        if opt == "--has-note":
            key, _, pos = val.partition(":")
            ok = (key, pos) in notes
            why = "present" if ok else "MISSING"
        elif opt == "--no-note":
            key, _, pos = val.partition(":")
            ok = (key, pos) not in notes
            why = "absent" if ok else "PRESENT"
        elif opt == "--has-track":
            ok = val in tracks
            why = "present" if ok else "MISSING"
        elif opt == "--no-track":
            ok = val not in tracks
            why = "absent" if ok else "PRESENT"
        elif opt == "--notes":
            ok = len(notes) == int(val)
            why = "%d notes" % len(notes)
        else:
            print("demo_check: unknown check %r" % opt)
            return 2
        print("  %-4s %-14s %-16s %s" % ("OK" if ok else "FAIL", opt, val, why))
        failures += 0 if ok else 1
    print("  %s %s" % ("PASS" if not failures else "FAIL",
                       "%d check(s) failed" % failures if failures else ""))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
