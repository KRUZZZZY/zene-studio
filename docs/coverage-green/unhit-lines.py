#!/usr/bin/env python3
"""Show which lines of a header a real tracefile leaves uncovered, with the source text.

Usage: python3 docs/coverage-green/unhit-lines.py <tracefile> <repo-root> <path> [path...]

For each named path (repo-relative), prints every instrumented line whose gcov
execution count is zero, with its source line, so a decision between "write a test"
and "this cannot run in this configuration" can be made from the code and not from
the percentage.
"""
import collections
import sys

tracefile, root = sys.argv[1], sys.argv[2].rstrip("/")
paths = sys.argv[3:]

per_file = collections.defaultdict(dict)
cur = None
with open(tracefile, encoding="utf-8") as fh:
    for line in fh:
        line = line.rstrip("\n")
        if line.startswith("SF:"):
            cur = line[3:]
            if cur.startswith(root + "/"):
                cur = cur[len(root) + 1:]
            continue
        if line == "end_of_record":
            cur = None
            continue
        if cur is None or not line.startswith("DA:"):
            continue
        body = line[3:]
        lineno, _, count = body.partition(",")
        count = count.split(",")[0]
        try:
            per_file[cur][int(lineno)] = int(count)
        except ValueError:
            pass

for p in paths:
    data = per_file.get(p)
    if data is None:
        print(f"=== {p}: NO TRACEFILE RECORD (not instrumented in this configuration)")
        continue
    hits = [n for n, c in data.items() if c > 0]
    miss = sorted(n for n, c in data.items() if c == 0)
    print(f"=== {p}: {len(data)} instrumented lines, {len(hits)} hit, {len(miss)} missed "
          f"({100.0 * len(hits) / len(data):.2f}%)")
    try:
        src = open(f"{root}/{p}", encoding="utf-8", errors="replace").read().splitlines()
    except OSError:
        src = []
    for n in miss:
        text = src[n - 1].strip() if 0 < n <= len(src) else "<no such line>"
        print(f"    {n:>5}: {text}")
    print()
