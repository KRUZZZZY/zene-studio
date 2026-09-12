#!/usr/bin/env python3
"""Train 3E: classify every line a pinned unit has that HEAD does not.

For each changed path, take the unit-only lines (`+` in `git diff HEAD <unit>`), and
ask whether HEAD's copy of the file already carries that line, either verbatim or
after the fork's own renames (lmms->zene, LMMS->ZENE, lmmsrc->zenec, .mmpz names are
not touched).  Lines with no HEAD counterpart are 'RESIDUE' and are printed in full;
they are the only thing a merge could actually bring.

usage: residue.py <unit-sha> [--all]
"""
import os
import subprocess
import sys

W = "/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-pa-foreign"

RENAME = [("lmmsrc", "zenec"), ("lmms.exe", "zene.exe"), ("lmms-", "zene-"),
          ("lmms.rc", "zene.rc"), ("lmms.1", "zene.1"), ("doc/lmms", "doc/zene"),
          ("lmmsio", "zeneio"), ("lmms", "zene"), ("LMMS", "ZENE"),
          ("Lmms", "Zene")]


def sh(c):
    return subprocess.run(c, shell=True, cwd=W, capture_output=True, text=True).stdout


def norm(s):
    out = s
    for a, b in RENAME:
        out = out.replace(a, b)
    return out


def main():
    sha = sys.argv[1]
    mb = sh(f"git merge-base {sha} HEAD").strip()
    paths = [l.split("\t")[1] for l in sh(f"git diff --name-status {mb} {sha}").splitlines() if l.strip()]
    total_res = 0
    for p in paths:
        head = sh(f"git cat-file -p HEAD:{p}")
        if not head:
            print(f"\n{p}: ABSENT at HEAD (a genuine addition)")
            d = sh(f"git diff {mb} {sha} -- {p}")
            print("   lines:", len([l for l in d.splitlines() if l.startswith("+") and not l.startswith("+++")]))
            continue
        hset = set(head.splitlines())
        hnorm = set(norm(x) for x in head.splitlines())
        plus = [l[1:] for l in sh(f"git diff HEAD {sha} -- {p}").splitlines()
                if l.startswith("+") and not l.startswith("+++")]
        residue = []
        for l in plus:
            if l in hset or l in hnorm or norm(l) in hset or norm(l) in hnorm:
                continue
            residue.append(l)
        # also: does any HEAD line *contain* the residue line's meaning (substring)?
        soft = [l for l in residue if any(l.strip() and l.strip() in h for h in hset | hnorm)]
        hard = [l for l in residue if l not in soft]
        print(f"\n{p}: unit-only lines={len(plus)} present-at-HEAD(verbatim/renamed)={len(plus)-len(residue)} "
              f"substring-of-a-HEAD-line={len(soft)} RESIDUE={len(hard)}")
        for l in hard:
            print("   RESIDUE|", l)
        total_res += len(hard)
    print(f"\nTOTAL HARD RESIDUE LINES for {sha[:10]}: {total_res}")


if __name__ == "__main__":
    main()
