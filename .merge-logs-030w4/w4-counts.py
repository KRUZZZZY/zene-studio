#!/usr/bin/env python3
"""Per-file union counts for a merge of append-only registries.

usage: w4-counts.py <paths-file> <rev1> [<rev2> ...]

For every path in <paths-file> that exists in a rev, print:
    lines  entries  sha256[:12]  rev  path
"entries" = non-blank, non-comment lines (the entry SET of an append-only
registry).  Run once per side and once on the merged tree: the column triple is
the union proof recorded in the merge commit message.
"""
import hashlib
import subprocess
import sys


def blob(rev, path):
    out = subprocess.run(["git", "show", "%s:%s" % (rev, path)],
                         capture_output=True)
    return None if out.returncode != 0 else out.stdout


def main():
    paths_file, revs = sys.argv[1], sys.argv[2:]
    paths = [l.strip() for l in open(paths_file) if l.strip()]
    for rev in revs:
        print("== %s" % rev)
        for p in paths:
            b = blob(rev, p)
            if b is None:
                continue
            txt = b.decode("utf-8", "replace")
            lines = txt.split("\n")
            ent = [l for l in lines if l.strip() and not l.lstrip().startswith("#")]
            print("  %6d  %6d  %s  %s"
                  % (len(lines), len(ent), hashlib.sha256(b).hexdigest()[:12], p))
    return 0


if __name__ == "__main__":
    sys.exit(main())
