#!/usr/bin/env python3
"""Analyse an lcov tracefile for the Zene Studio coverage run (2026-09-12).

Reads one or more lcov tracefiles plus the two scope manifests and the recorded
baseline, and prints the numbers the coverage report is built from:

  * line-weighted totals (the headline definition; see tests/coverage-gate.sh)
  * every file at 0.00% with a non-zero instrumented-line count
  * every file with ZERO instrumented lines (unmeasurable, never 100%)
  * every scope-manifest entry that produced NO tracefile record at all
    (the instrumentation never saw it - a gap in the ratchet, not a pass)
  * per-file deltas against tests/coverage-baseline.tsv

Usage:
  python3 analyze-tracefile.py <repo-root> [tracefile ...]
"""
import os
import sys


def parse_lcov(path):
    """Return {abs_path: (lf, lh)} for every SF record in the tracefile."""
    records = {}
    current = None
    with open(path, encoding="utf-8", errors="replace") as fh:
        for line in fh:
            line = line.rstrip("\n")
            if line.startswith("SF:"):
                current = line[3:]
                records.setdefault(current, [0, 0])
            elif line.startswith("LF:") and current:
                records[current][0] = int(line[3:])
            elif line.startswith("LH:") and current:
                records[current][1] = int(line[3:])
            elif line == "end_of_record":
                current = None
    return records


def read_manifest(path):
    out = []
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            out.append(line)
    return out


def read_baseline(path):
    base = {}
    if not os.path.exists(path):
        return base
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            line = line.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            p, _, pct = line.partition("\t")
            pct = pct.strip()
            base[p] = None if pct in ("n/a", "N/A", "") else float(pct)
    return base


def main():
    root = os.path.abspath(sys.argv[1])
    tracefiles = sys.argv[2:] or [os.path.join(root, "build-coverage/coverage/coverage-fork.info")]

    merged = {}
    for tf in tracefiles:
        for p, (lf, lh) in parse_lcov(tf).items():
            key = p[len(root) + 1:] if p.startswith(root + os.sep) else p
            old = merged.get(key, (0, 0))
            # union the records: same file can appear once per tracefile
            merged[key] = (max(old[0], lf), max(old[1], lh))

    fork = read_manifest(os.path.join(root, "tests/fork-sources.txt"))
    alls = read_manifest(os.path.join(root, "tests/all-sources.txt"))
    base = read_baseline(os.path.join(root, "tests/coverage-baseline.tsv"))

    total_lines = sum(lf for lf, _ in merged.values())
    total_hit = sum(lh for _, lh in merged.values())
    pct = 100.0 * total_hit / total_lines if total_lines else 0.0

    print("=== tracefiles ===")
    for tf in tracefiles:
        print(f"  {tf}")
    print(f"=== headline (line-weighted, sum hit / sum instrumented) ===")
    print(f"  {pct:.2f}%  ({total_hit}/{total_lines} lines over {len(merged)} files with a record)")

    zero = sorted(p for p, (lf, lh) in merged.items() if lf > 0 and lh == 0)
    unmeas = sorted(p for p, (lf, _) in merged.items() if lf == 0)
    print(f"\n=== files at 0.00% with instrumented lines: {len(zero)} ===")
    zl = 0
    for p in zero:
        lf, lh = merged[p]
        zl += lf
        print(f"  0.00%  {lf:6d} lines  {p}")
    print(f"  total uninstrumented-hit lines in these files: {zl}")

    print(f"\n=== files with ZERO instrumented lines (unmeasurable, never 100%): {len(unmeas)} ===")
    for p in unmeas:
        print(f"  unmeasurable  {p}")

    # scope coverage: which manifest entries produced no record at all
    unseen_fork = [p for p in fork if p not in merged]
    print(f"\n=== fork-sources.txt entries with NO tracefile record: {len(unseen_fork)} of {len(fork)} ===")
    for p in unseen_fork:
        print(f"  unseen  {p}")

    # whole-tree manifest is checked against every tracefile given (pass coverage.info too)
    unseen_all = [p for p in alls if p not in merged]
    print(f"\n=== all-sources.txt entries with NO record in the given tracefile(s): "
          f"{len(unseen_all)} of {len(alls)} ===")
    for p in unseen_all:
        print(f"  unseen  {p}")

    print(f"\n=== per-file delta vs tests/coverage-baseline.tsv ({len(base)} entries) ===")
    dropped = []
    for p, (lf, lh) in sorted(merged.items()):
        if p not in base:
            continue
        old = base[p]
        new = None if lf == 0 else round(100.0 * lh / lf, 2)
        if old is None:
            print(f"  n/a->{new}")
            continue
        delta = (new if new is not None else 0.0) - old
        if delta < -0.05:
            dropped.append((p, old, new, lf, lh))
    for p, old, new, lf, lh in sorted(dropped):
        print(f"  DROP  {p}: {old:.2f}% -> {new}%  ({lh}/{lf} now)")
    print(f"  files that dropped: {len(dropped)}")

    base_absent = sorted(set(base) - set(merged))
    print(f"\n  baseline entries with no record in this run: {len(base_absent)}")
    for p in base_absent:
        print(f"    absent  {p}  (baseline {base[p]})")

    print(f"\n=== in-scope but not in baseline (would be 'new' to the gate): "
          f"{len([p for p in merged if p in fork and p not in base])} ===")


if __name__ == "__main__":
    main()
