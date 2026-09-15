#!/usr/bin/env python3
"""DOC-5 citation sweep.

Measures, for every *.md name that the product tree cites, whether the name
resolves to a tracked file in the product repo, in the program workspace, or
nowhere.  Read-only.  Emits a TSV on stdout.
"""
import os
import re
import subprocess
import sys
from collections import defaultdict

PRODUCT = "/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-030/wdoc5"
WORKSPACE = "/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research"

# token: a *.md basename, optionally preceded by a path
TOKEN = re.compile(r"(?<![\w/.-])((?:[A-Za-z0-9_.+-]+/)*[A-Za-z0-9_][A-Za-z0-9_.+-]*\.md)\b")


def tracked(repo):
    out = subprocess.run(["git", "ls-files"], cwd=repo, capture_output=True, text=True)
    return out.stdout.split("\n")[:-1]


def main():
    prod_files = tracked(PRODUCT)
    ws_files = tracked(WORKSPACE)

    prod_by_base = defaultdict(list)
    for f in prod_files:
        prod_by_base[os.path.basename(f)].append(f)
    ws_by_base = defaultdict(list)
    for f in ws_files:
        ws_by_base[os.path.basename(f)].append(f)

    # Search product tree; skip the vendored 3rdparty READMEs and the
    # docs/specs snapshots themselves (they cite their own source, which we
    # handle separately).
    roots = ["src", "include", "plugins", "modules", "tests", "recording",
             "tools", "docs", "CMakeLists.txt", "cmake", "data", ".github"]
    cmd = ["git", "grep", "-I", "-n", "-E", r"[A-Za-z0-9_][A-Za-z0-9_./+-]*\.md", "--"] + roots
    out = subprocess.run(cmd, cwd=PRODUCT, capture_output=True, text=True)
    if out.returncode not in (0, 1):
        sys.exit("git grep failed: " + out.stderr)

    # path:lineno:content
    counts = defaultdict(lambda: defaultdict(int))   # token -> file -> n
    for line in out.stdout.split("\n"):
        if not line:
            continue
        parts = line.split(":", 2)
        if len(parts) != 3:
            continue
        path, _, content = parts
        for m in TOKEN.finditer(content):
            counts[m.group(1)][path] += 1

    print("TOKEN\tN_CITING_FILES\tN_TOTAL\tPRODUCT\tWORKSPACE\tLOCATION\tCITING_FILES")
    rows = []
    for tok, files in counts.items():
        base = os.path.basename(tok)
        p = prod_by_base.get(base, [])
        w = ws_by_base.get(base, [])
        if p:
            loc = "product:" + p[0]
        elif w:
            loc = "workspace:" + w[0]
        else:
            loc = "NEITHER"
        rows.append((tok, len(files), sum(files.values()), bool(p), bool(w), loc,
                     ",".join(sorted(files))))
    rows.sort(key=lambda r: (r[5] != "NEITHER", r[5].startswith("product"), -r[1]))
    for tok, nf, nt, p, w, loc, fl in rows:
        print(f"{tok}\t{nf}\t{nt}\t{int(p)}\t{int(w)}\t{loc}\t{fl}")


if __name__ == "__main__":
    main()
