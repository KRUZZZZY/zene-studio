#!/usr/bin/env python3
"""Classify each cited-document token by citing-file class (code vs docs) and
by where the document lives."""
import os
import re
import subprocess
from collections import defaultdict

PRODUCT = "/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-030/wdoc5"
WS = "/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research"

CODE_DIRS = ("src/", "include/", "plugins/", "modules/", "tests/", "recording/",
             "tools/", "cmake/", "CMakeLists.txt", "data/")
DOC_DIRS = ("docs/",)

TOKEN = re.compile(r"(?<![\w/.-])((?:[A-Za-z0-9_.+-]+/)*[A-Za-z0-9_][A-Za-z0-9_.+-]*\.md)\b")


def sh(a, cwd=PRODUCT):
    return subprocess.run(a, cwd=cwd, capture_output=True, text=True).stdout


def main():
    prod = set(sh(["git", "ls-files"]).split("\n"))
    ws = set(sh(["git", "ls-files"], WS).split("\n"))
    prod_base = {os.path.basename(f) for f in prod}
    ws_base = {os.path.basename(f) for f in ws}

    roots = ["src", "include", "plugins", "modules", "tests", "recording", "tools",
             "docs", "CMakeLists.txt", "cmake", "data"]
    out = sh(["git", "grep", "-I", "-n", "-E", r"[A-Za-z0-9_][A-Za-z0-9_./+-]*\.md", "--"] + roots)
    citers = defaultdict(set)
    for line in out.split("\n"):
        if not line:
            continue
        parts = line.split(":", 2)
        if len(parts) != 3:
            continue
        path, _, content = parts
        for m in TOKEN.finditer(content):
            citers[m.group(1)].add(path)

    # which documents are SPECS?  a doc is a spec-in-scope candidate if it is
    # cited from at least one code file, or is itself a SPEC-* file.
    candidates = []
    for tok, files in citers.items():
        base = os.path.basename(tok)
        if base in prod_base or base in ws_base:
            loc = "product" if base in prod_base else "workspace"
        else:
            loc = "NEITHER"
        code = sorted(f for f in files if f.startswith(CODE_DIRS))
        docs = sorted(f for f in files if not f.startswith(CODE_DIRS))
        if not code and not tok.upper().startswith("SPEC"):
            continue
        candidates.append((base, tok, loc, len(code), len(docs), code, docs))

    print(f"{'DOC':<44} {'LOC':<10} {'code':>4} {'docs':>4}   token")
    for base, tok, loc, nc, nd, code, docs in sorted(candidates, key=lambda r: (-r[3], r[0])):
        print(f"{base:<44} {loc:<10} {nc:>4} {nd:>4}   {tok}")
    print()
    print("### code-only detail")
    for base, tok, loc, nc, nd, code, docs in sorted(candidates, key=lambda r: (-r[3], r[0])):
        if not code:
            continue
        print(f"--- {base}  [{loc}]  spec-token={tok}")
        for c in code:
            print("    " + c)


if __name__ == "__main__":
    main()
