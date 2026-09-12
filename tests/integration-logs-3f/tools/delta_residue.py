#!/usr/bin/env python3
"""Train 3E: does HEAD already carry the unit's OWN change?

`residue.py` compares the unit's whole file against HEAD's, which is noisy for a
file the branch had not evolved (the branch's extra lines are then older upstream
text).  This one is the sharp instrument: take the unit's own delta
(`git diff <merge-base> <unit> -- path`), i.e. exactly what its commit does to
that file, and ask of every added line whether HEAD's copy carries it - verbatim,
or after the fork's lmms->zene rename.  A path with zero residue added no line to
HEAD that HEAD does not already have.

usage: delta_residue.py <unit-sha>
"""
import subprocess
import sys

W = "/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-pa-foreign"
RENAME = [("lmmsrc", "zenec"), ("lmms.exe", "zene.exe"), ("lmms-", "zene-"),
          ("lmms.rc", "zene.rc"), ("lmms.1", "zene.1"), ("lmms", "zene"),
          ("LMMS", "ZENE"), ("Lmms", "Zene")]


def sh(c):
    return subprocess.run(c, shell=True, cwd=W, capture_output=True, text=True).stdout


def norm(s):
    for a, b in RENAME:
        s = s.replace(a, b)
    return s


sha = sys.argv[1]
# The reference tree defaults to HEAD; pass a rev to ask the question against an earlier
# tip (e.g. the entry tip, now that the train's own merges make the unit an ancestor).
REF = sys.argv[3] if len(sys.argv) > 3 and sys.argv[2] == "--against" else "HEAD"
mb = sh(f"git merge-base {sha} {REF}").strip()
paths = [l.split("\t")[1] for l in sh(f"git diff --name-status {mb} {sha}").splitlines() if l.strip()]
print(f"unit {sha[:10]}  merge-base {mb[:10]}  against {REF}  paths={len(paths)}")
total = 0
for p in paths:
    d = sh(f"git diff {mb} {sha} -- {p}")
    plus = [l[1:] for l in d.splitlines() if l.startswith("+") and not l.startswith("+++")]
    head = sh(f"git cat-file -p {REF}:{p}")
    if not head:
        print(f"  {p:45s} added={len(plus):4d}  HEAD-ABSENT (a genuine addition)")
        total += len(plus)
        continue
    hset = set(head.splitlines())
    hnorm = {norm(x) for x in hset}
    res = [l for l in plus
           if l not in hset and l not in hnorm and norm(l) not in hset and norm(l) not in hnorm]
    soft = [l for l in res if l.strip() and any(l.strip() in h for h in hset | hnorm)]
    hard = [l for l in res if l not in soft]
    print(f"  {p:45s} delta-added={len(plus):4d} in-HEAD={len(plus)-len(res):4d} "
          f"substring={len(soft):3d} RESIDUE={len(hard)}")
    for l in hard:
        print(f"      RESIDUE| {l}")
    total += len(hard)
print(f"TOTAL hard residue for the unit's own delta: {total}")
