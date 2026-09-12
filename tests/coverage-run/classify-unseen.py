#!/usr/bin/env python3
"""Classify every fork-sources.txt entry the instrumentation did not see.

For each manifest entry with no record in the fork tracefile, decide WHY:

  not-compiled   the build produced no .gcno for it at all, so no binary ever
                 contained it -> a feature-gated or dependency-absent file.
                 A gate that watchers fork-sources.txt cannot see it: a gap.
  compiled-norun it has a .gcno but no .gcda -> it was compiled into a binary
                 that no ctest test executed (e.g. linked only into the app).
                 Also a gap: zero evidence was produced this run.
  header-no-code a header with no executable lines of its own; gcov emits no
                 record for it and none is expected.

Usage: python3 classify-unseen.py <repo-root> <build-dir> <tracefile>
"""
import os
import subprocess
import sys


def read_manifest(path):
    return [l.strip() for l in open(path, encoding="utf-8") if l.strip() and not l.startswith("#")]


def tracefile_records(path):
    return {l[3:].rstrip("\n") for l in open(path, encoding="utf-8", errors="replace")
            if l.startswith("SF:")}


def main():
    root, build, trace = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2]), sys.argv[3]
    seen = tracefile_records(trace)
    seen_rel = {p[len(root) + 1:] if p.startswith(root + os.sep) else p for p in seen}
    fork = read_manifest(os.path.join(root, "tests/fork-sources.txt"))

    gcno = set()
    for dirpath, _dirs, files in os.walk(build):
        for f in files:
            if f.endswith(".gcno"):
                gcno.add(os.path.join(dirpath, f))
    # map basename -> gcno paths, since the object path mirrors the source tree
    gcno_basenames = {os.path.basename(p) for p in gcno}
    gcda_basenames = set()
    for dirpath, _dirs, files in os.walk(build):
        for f in files:
            if f.endswith(".gcda"):
                gcda_basenames.add(os.path.basename(f))

    buckets = {"not-compiled": [], "compiled-norun": [], "header-no-code": []}
    for rel in fork:
        if rel in seen_rel:
            continue
        base = os.path.basename(rel)
        gcno_name = base + ".gcno"
        gcda_name = base + ".gcda"
        ext = os.path.splitext(rel)[1]
        if ext not in (".c", ".cc", ".cpp", ".cxx"):
            # a header, or a non-C++ scope entry (.sh/.py)
            if ext not in (".h", ".hpp"):
                buckets.setdefault("non-source-entry", []).append(rel)
                continue
            if gcno_name in gcno_basenames:
                buckets["compiled-norun"].append(rel)
            else:
                buckets["header-no-code"].append(rel)
            continue
        if gcno_name not in gcno_basenames:
            buckets["not-compiled"].append(rel)
        elif gcda_name not in gcda_basenames:
            buckets["compiled-norun"].append(rel)
        else:
            # a .gcda exists but lcov emitted no record: count it separately
            buckets.setdefault("gcda-but-no-record", []).append(rel)

    for name in ("not-compiled", "compiled-norun", "gcda-but-no-record", "header-no-code",
                 "non-source-entry"):
        items = buckets.get(name, [])
        print(f"\n=== {name}: {len(items)} ===")
        for p in sorted(items):
            print(f"  {p}")
    print(f"\n=== totals: {len(fork)} in scope, {len(fork) - sum(len(v) for v in buckets.values())} "
          f"seen by the instrumentation, {sum(len(v) for v in buckets.values())} unseen ===")


if __name__ == "__main__":
    main()
