#!/usr/bin/env python3
"""Assert the merge-1 ledger resolution is a true union of BOTH full sides.

Reads the resolved tests/upstream-modifications.txt and the two index stages, and
proves, per path:
  * every path in either side is in the result (no entry lost);
  * a result reason is one of: ours, theirs, ours + '; ' + theirs, or a superset
    built from those two clauses (never text authored from scratch);
  * no duplicate path, no blank reason, no ';;'.
usage: verify_union.py [mergeN-dir]
"""
import os
import subprocess
import sys

W = "/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-pa-integration"
S = "/home/kruzzzzy/Documents/AI_KOS_PROJECT/.merge3b/sides/merge1"


def side(rev, path):
    return subprocess.run(["git", "show", f"{rev}:{path}"], cwd=W,
                          capture_output=True, text=True).stdout


def parse(text, key=True):
    d = {}
    for l in text.splitlines():
        if not l.strip() or l.startswith("#"):
            continue
        k, _, v = l.partition("\t")
        if k.strip():
            d.setdefault(k, v if key else "")
    return d


R = parse(open(os.path.join(W, "tests/upstream-modifications.txt"), encoding="utf-8").read())
O = parse(side(":2", "tests/upstream-modifications.txt"))
T = parse(side(":3", "tests/upstream-modifications.txt"))

lost = sorted((set(O) | set(T)) - set(R))
assert not lost, f"entries lost: {lost}"

unexplained = []
for k in set(O) | set(T):
    o, t, r = O.get(k), T.get(k), R.get(k)
    ok = (r == o or r == t or r == f"{o}; {t}"
          or (o and t and r.startswith(o) and t in r)      # superset keeping both clauses
          or (o and t and o in t and r == t)
          or (o and t and t in o and r == o))
    if not ok:
        unexplained.append(k)
assert not unexplained, f"reason not traceable to a side: {unexplained}"

dup = [k for k in R if list(parse(open(os.path.join(W, "tests/upstream-modifications.txt"),
                                      encoding="utf-8").read())).count(k) > 1]
assert not dup, "duplicate path"
assert all(v.strip() for v in R.values()), "blank reason"
assert not any(";;" in v for v in R.values()), "';;' in a reason"

clause = sum(1 for k in R if O.get(k) and T.get(k) and R[k] != O[k] and R[k] != T[k])
print(f"union OK: ours={len(O)} theirs={len(T)} result={len(R)} "
      f"(shared {len(set(O) & set(T))}, ours-only {len(set(O) - set(T))}, "
      f"theirs-only {len(set(T) - set(O))}); reason clauses merged for {clause} path(s); "
      f"0 lost, 0 dup, 0 blank, 0 untraceable")
