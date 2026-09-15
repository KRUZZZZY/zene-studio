#!/usr/bin/env python3
"""For every cited *.md token that does not resolve in this tree, ask git where
it does exist: product repo (any branch), or the program workspace."""
import os
import re
import subprocess
import sys
from collections import defaultdict

PRODUCT = "/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-030/wdoc5"
WORKSPACE = "/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research"

TOKEN = re.compile(r"(?<![\w/.-])((?:[A-Za-z0-9_.+-]+/)*[A-Za-z0-9_][A-Za-z0-9_.+-]*\.md)\b")


def run(cmd, cwd):
    return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True).stdout


def main(tsv):
    prod_files = set(run(["git", "ls-files"], PRODUCT).split("\n"))
    prod_roots = set()
    for f in prod_files:
        prod_roots.add(os.path.basename(f))
    ws_files = set(run(["git", "ls-files"], WORKSPACE).split("\n"))
    ws_roots = {os.path.basename(f) for f in ws_files}

    rows = []
    for line in open(tsv).read().split("\n")[1:]:
        if not line.strip():
            continue
        parts = line.split("\t")
        tok, nf, nt, p, w, loc, citing = parts[0], parts[1], parts[2], parts[3], parts[4], parts[5], parts[6]
        if loc != "NEITHER":
            continue
        base = os.path.basename(tok)
        # where in the product repo, across ALL branches?
        branches = run(["git", "branch", "-a", "--contains", "HEAD"], PRODUCT)  # placeholder
        allb = run(["git", "rev-list", "--all", "--", "*" + base], PRODUCT).strip()
        in_branch = ""
        if allb:
            names = run(["git", "branch", "-a", "--contains", allb.split("\n")[0]], PRODUCT)
            in_branch = " ".join(l.strip() for l in names.split("\n") if l.strip())[:200]
        # workspace git history
        ws_hist = run(["git", "log", "--all", "--oneline", "-1", "--", "**/" + base], WORKSPACE).strip()
        rows.append((tok, nf, in_branch, ws_hist, citing))

    for tok, nf, in_branch, ws_hist, citing in sorted(rows, key=lambda r: -int(r[1])):
        print("=" * 100)
        print(f"TOKEN {tok}   citing_files={nf}")
        print(f"  product-any-branch: {in_branch or '(no branch contains it)'}")
        print(f"  workspace-history : {ws_hist or '(never in workspace history)'}")
        print(f"  cited by : {citing[:600]}")


if __name__ == "__main__":
    main(sys.argv[1])
