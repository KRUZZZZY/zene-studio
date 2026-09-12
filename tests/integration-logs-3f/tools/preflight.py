#!/usr/bin/env python3
"""Train 3E preflight: for each pinned unit, what would the merge actually bring?

For every path the unit changes (vs its own merge base with HEAD), report:
  * whether HEAD has the path, whether the two blobs are byte-identical;
  * how many lines the unit's side has that HEAD does not ('+' in git diff HEAD <unit>)
    and how many HEAD has that the unit lacks ('-');
  * for a path the unit only *adds*, whether a HEAD commit added a file of the
    same content (the already-present case).

Writes per-unit diffs to tests/integration-logs-3e/preflight/<unit>.diff.
"""
import os
import subprocess
import sys

W = "/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-pa-foreign"
OUT = os.path.join(W, "tests/integration-logs-3e/preflight")

UNITS = [
    ("1", "tools/local-ci", "3e1480477b04c7a49612f96ac99cc502b7b56d8c"),
    ("2", "test/real-client-e2e", "ae89fa89401e409ddf2aaa456b1cdf52c3c7e4c6"),
    ("3", "feat/stem-split", "bbf7a307cb17f10727bdb4fe68960528a71254ad"),
    ("4", "part-b-engine-integration", "dbcb8a2ba61cd4ee4a67a832cc0b582f187069cc"),
    ("5", "feat/session-view-model", "e317f1062dcca0f0411e293588c3cf731cc0b127"),
    ("6", "docs/fork-readme", "2e36486d5f6b58b136c3fc226dfab994ff56f776"),
]


def sh(c):
    r = subprocess.run(c, shell=True, cwd=W, capture_output=True, text=True)
    return r.returncode, r.stdout, r.stderr


def main():
    os.makedirs(OUT, exist_ok=True)
    for n, name, sha in UNITS:
        mb = sh(f"git merge-base {sha} HEAD")[1].strip()
        changed = sh(f"git diff --name-status {mb} {sha}")[1].splitlines()
        print(f"\n===== unit {n} {name} {sha[:10]}  (merge base {mb[:10]}) =====")
        print(f"  {'path':60s} {'status':10s} {'HEAD':6s} {'branch':6s} {'same?':6s} {'+':>6s} {'-':>6s}")
        for line in changed:
            st, path = line.split("\t")[:2]
            hp = sh(f"git rev-parse HEAD:{path}")[1].strip()
            bp = sh(f"git rev-parse {sha}:{path}")[1].strip()
            hstat = "yes" if hp and not hp.startswith("HEAD:") else "ABSENT"
            bstat = "yes" if bp and not bp.startswith(sha + ":") else "ABSENT"
            same = "IDENT" if (hp == bp and hp) else "-"
            plus = minus = 0
            if hp and bp:
                d = sh(f"git diff HEAD {sha} -- {path}")[1]
                plus = sum(1 for l in d.splitlines() if l.startswith("+") and not l.startswith("+++"))
                minus = sum(1 for l in d.splitlines() if l.startswith("-") and not l.startswith("---"))
            print(f"  {path:60s} {st:10s} {hstat:6s} {bstat:6s} {same:6s} {plus:6d} {minus:6d}")
        sh(f"git diff {mb} {sha} > {OUT}/{n}-{name.replace('/', '_')}.diff")


if __name__ == "__main__":
    main()
