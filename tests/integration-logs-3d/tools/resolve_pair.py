#!/usr/bin/env python3
"""Merge-train 3B generic conflict resolver, working from the index stages.

usage: resolve_pair.py <mode> <path> [<path> ...]
  mode = ledger      append-only <path><TAB><reason> registry  (tests/upstream-modifications.txt)
  mode = manifest    plain entry list, LC_ALL=C sorted        (tests/fork-sources.txt etc.)

Reads :2: (ours/integration) and :3: (theirs/branch) wholesale from the index, so
multiple conflict hunks need no special handling, and resolves:

  ledger    result = ours verbatim, then per shared path:
              theirs' reason == the merge base's  -> keep ours (the branch never
                                                     touched this reason)
              one side's text contains the other's -> keep the superset
              otherwise                            -> both clauses, ours first
            then every theirs-only path is appended, kept under the banner section
            it sits in in theirs' own file so the grouping survives.
  manifest  result = the ENTRY SET union, sorted by bytes, under ours' header.

Asserts, always: no conflict marker; every entry from both sides present; no
duplicate path; no blank reason; no ';;'.
"""
import os
import subprocess
import sys

W = "/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-pa-integration"


def show(rev, path):
    r = subprocess.run(["git", "show", f"{rev}:{path}"], cwd=W, capture_output=True, text=True)
    if r.returncode != 0:
        return ""
    return r.stdout


def parse_key(text):
    d = {}
    for l in text.splitlines():
        if not l.strip() or l.startswith("#"):
            continue
        k, _, v = l.partition("\t")
        if k.strip():
            d.setdefault(k, v)
    return d


def entries(text):
    return [l for l in text.splitlines() if l.strip() and not l.lstrip().startswith("#")]


def resolve_ledger(path):
    p = os.path.join(W, path)
    ours_t, theirs_t, base_t = show(":2", path), show(":3", path), show(":1", path)
    O, T, B = parse_key(ours_t), parse_key(theirs_t), parse_key(base_t)
    out = ours_t.splitlines()
    n_super = n_base = n_union = 0
    for idx, l in enumerate(out):
        if l.startswith("#") or not l.strip():
            continue
        k, _, reason = l.partition("\t")
        if k not in T or T[k] == reason:
            continue
        tn = T[k]
        if tn == B.get(k):
            n_base += 1
        elif tn in reason:
            n_super += 1
        elif reason in tn:
            out[idx] = k + "\t" + tn
            n_super += 1
        else:
            out[idx] = k + "\t" + reason + "; " + tn
            n_union += 1
    new = [k for k in T if k not in O]
    appended = []
    if new:
        block, banner = [], []
        for l in theirs_t.splitlines():
            if not l.strip():
                continue
            if l.startswith("#"):
                if l.startswith("# ---"):
                    if any(not b.startswith("#") for b in block):
                        appended += [""] + banner + block
                        block = []
                    banner = [l]
                else:
                    banner.append(l)
                continue
            if l.split("\t")[0] in new:
                block.append(l)
        if any(not b.startswith("#") for b in block):
            appended += [""] + banner + block
    out += appended
    body = "\n".join(out) + "\n"
    open(p, "w", encoding="utf-8").write(body)
    R = parse_key(body)
    assert not any(x.startswith("<<<<<<<") or x.startswith(">>>>>>>") or x.rstrip() == "======="
                   for x in out), "conflict marker"
    missing = (set(O) | set(T)) - set(R)
    assert not missing, f"entries lost: {sorted(missing)}"
    seen = [l.split("\t")[0] for l in body.splitlines() if l.strip() and not l.startswith("#")]
    assert len(seen) == len(set(seen)), "duplicate path"
    assert all(v.strip() for v in R.values()), "blank reason"
    assert not any(";;" in v for v in R.values()), "';;' in a reason"
    for k in R:
        o, t, r = O.get(k), T.get(k), R[k]
        ok = (r == o or r == t or (o and t and (r == f"{o}; {t}" or t in r or o in t and r == t
                                               or t in o and r == o)))
        assert ok, f"reason for {k} is not traceable to a side"
    print(f"  {path}: {len(R)} entries (ours {len(O)} + theirs-only {len(new)}); "
          f"reasons: {n_base} kept-ours (branch copy was the base), {n_super} superset, "
          f"{n_union} clause-union; 0 lost, 0 dup, 0 blank")
    return len(R)


def resolve_manifest(path):
    p = os.path.join(W, path)
    ours_t, theirs_t = show(":2", path), show(":3", path)
    hdr = []
    for l in ours_t.splitlines():
        if not l.strip() or l.lstrip().startswith("#"):
            hdr.append(l)
        else:
            break
    O, T = set(entries(ours_t)), set(entries(theirs_t))
    U = sorted(O | T, key=lambda x: x.encode())
    body = "\n".join(hdr).rstrip("\n") + "\n" + "\n".join(U) + "\n"
    open(p, "w", encoding="utf-8").write(body)
    assert not any(x.startswith("<<<<<<<") for x in body.splitlines())
    assert set(entries(body)) == set(U), "entry set changed while writing"
    print(f"  {path}: {len(U)} entries (ours {len(O)}, theirs {len(T)}, "
          f"theirs-only {len(T - O)}, ours-only {len(O - T)}); ours' header kept")
    return len(U)


if __name__ == "__main__":
    mode, paths = sys.argv[1], sys.argv[2:]
    for pa in paths:
        (resolve_ledger if mode == "ledger" else resolve_manifest)(pa)
