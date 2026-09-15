#!/usr/bin/env python3
"""Locate a path across every ref in the product repo: does it exist in ANY
commit, and if so, is it on release/0.3.0?"""
import subprocess
import sys
from collections import defaultdict

PRODUCT = "/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-030/wdoc5"
MAIN = "/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/lmms"

PATHS = sys.argv[1:]


def sh(args, cwd=MAIN):
    return subprocess.run(args, cwd=cwd, capture_output=True, text=True).stdout


for p in PATHS:
    print("=" * 90)
    print("PATH:", p)
    log = sh(["git", "log", "--all", "--oneline", "--diff-filter=AD", "--", p]).strip()
    if not log:
        print("  never added or deleted in any commit")
    else:
        print("  history (A/D only):")
        for l in log.split("\n")[:6]:
            print("    " + l)
    refs = sh(["git", "for-each-ref", "--format=%(refname:short)", "refs/heads", "refs/remotes"]).split("\n")
    have = []
    for r in refs:
        if not r:
            continue
        out = sh(["git", "ls-tree", "-r", "--name-only", r, "--", p]).strip()
        if out == p:
            have.append(r)
    print("  present in tree of refs:", ", ".join(have[:14]) or "(none)")
    if len(have) > 14:
        print(f"    ... +{len(have)-14} more")
