#!/usr/bin/env python3
"""Resolve the add/add CODE conflicts of lane 030/import-detection by UNION.

include/Song.h and src/core/Song.cpp each carry three regions where BOTH sides
added a block at the same place, about two independent project-state fields
(the chord track, ours; the detected key, theirs).  The union keeps both blocks.

Two regions end with lines the two sides SHARE (a closing brace, a
`return true;` pair), because the merge only conflicted the block ABOVE the
shared tail.  Union there means the shared tail is DUPLICATED: once for ours'
block and once for theirs' - byte-identical copies, so the file's own
indentation is what is used.

usage: resolve-code-import-detection.py [--dry]
"""
import subprocess
import sys

# how many shared-tail lines belong to EACH region, in file order
TAIL_LINES = [0, 1, 2]        # Song.cpp: clear / save / load
TAIL_H = [0, 0, 0]            # Song.h: include / accessors / member


def split_regions(lines):
    regions, i = [], 0
    while i < len(lines):
        if lines[i].startswith("<<<<<<<"):
            j = next(n for n in range(i + 1, len(lines)) if lines[n] == "=======")
            k = next(n for n in range(j + 1, len(lines)) if lines[n].startswith(">>>>>>>"))
            regions.append((i, j, k, lines[i + 1:j], lines[j + 1:k]))
            i = k + 1
        else:
            i += 1
    return regions


def resolve(path, tails, dry):
    lines = open(path).read().split("\n")
    regions = split_regions(lines)
    print("== %s: %d conflict region(s)" % (path, len(regions)))
    if len(regions) != len(tails):
        sys.exit("FATAL %s: %d regions, expected %d" % (path, len(regions), len(tails)))
    out, prev = [], 0
    for idx, (i, j, k, ours, theirs) in enumerate(regions):
        out += lines[prev:i]
        tail = lines[k + 1:k + 1 + tails[idx]]
        if len(tail) != tails[idx]:
            sys.exit("FATAL %s: region %d has no shared tail" % (path, idx))
        # our block + its copy of the shared tail, blank, their block (the
        # ORIGINAL shared tail then serves THEIR block)
        out += ours + tail + [""] + theirs
        print("   region %d: ours %d lines + theirs %d lines + %d shared tail line(s) duplicated"
              % (idx, len(ours), len(theirs), len(tail)))
        prev = k + 1
    out += lines[prev:]
    text = "\n".join(out)
    bad = [n for n, l in enumerate(text.split("\n"), 1) if l.startswith(("<<<<<<<", ">>>>>>>", "======="))]
    if bad:
        sys.exit("FATAL %s: markers remain at %s" % (path, bad[:4]))
    # proof: every line either side ADDED (vs the merge base) is present
    mb = subprocess.run(["git", "merge-base", "HEAD", "030/import-detection"],
                        capture_output=True, text=True).stdout.strip()
    base = subprocess.run(["git", "show", "%s:%s" % (mb, path)],
                          capture_output=True, text=True).stdout.split("\n")
    for who, side in (("ours", lines), ("theirs", lines)):
        pass
    ours_side = subprocess.run(["git", "show", ":2:%s" % path],
                               capture_output=True, text=True).stdout.split("\n")
    theirs_side = subprocess.run(["git", "show", ":3:%s" % path],
                                 capture_output=True, text=True).stdout.split("\n")
    import difflib
    mset = set(l.rstrip() for l in out)
    for who, side in (("ours", ours_side), ("theirs", theirs_side)):
        sm = difflib.SequenceMatcher(None, base, side, autojunk=False)
        added = []
        for tag, i1, i2, j1, j2 in sm.get_opcodes():
            if tag in ("insert", "replace"):
                added += [l for l in side[j1:j2] if l.strip()]
        miss = [l for l in added if l.rstrip() not in mset]
        print("   %s added lines missing: %d%s" % (who, len(miss), "  !! " + repr(miss[:2]) if miss else ""))
        if miss:
            sys.exit("FATAL %s: %s-added line lost" % (path, who))
    print("   PROOF both sides' added lines present; 0 markers; %d lines total"
          % len(out))
    if dry:
        print("   (dry run)")
        return
    open(path, "w").write(text)
    print("   WROTE %s" % path)


def main():
    dry = "--dry" in sys.argv
    resolve("include/Song.h", TAIL_H, dry)
    resolve("src/core/Song.cpp", TAIL_LINES, dry)
    return 0


if __name__ == "__main__":
    sys.exit(main())
