#!/usr/bin/env python3
"""Union two sides of a conflicted append-only ledger/manifest (merge train 3B).

usage: merge_ledger.py <ours> <theirs> <out> [--banner-file F] [--keyledger]

For a path<TAB>reason ledger (--keyledger):
  - the result starts as ours verbatim;
  - a path only in theirs is appended (under the banner, if given);
  - a shared path whose reason differs keeps the superset when one side's text
    contains the other's, otherwise both as clause segments (ours first).
For a plain entry list (default): result = ours + theirs-only entries, sorted.

Asserts: every entry from both sides present; no blank reason; no ';;'; no markers.
"""
import sys


def read(p):
    return open(p, encoding="utf-8").read().splitlines()


def parse_key(ls):
    d = {}
    for l in ls:
        if not l.strip() or l.startswith("#"):
            continue
        k, _, v = l.partition("\t")
        d[k] = v
    return d


def parse_plain(ls):
    return set(l for l in ls if l.strip() and not l.lstrip().startswith("#"))


def main():
    ours_p, theirs_p, out_p = sys.argv[1:4]
    banner = None
    if "--banner-file" in sys.argv:
        banner = [l.rstrip("\n") for l in read(sys.argv[sys.argv.index("--banner-file") + 1])]
    keyledger = "--keyledger" in sys.argv
    ours, theirs = read(ours_p), read(theirs_p)
    if keyledger:
        Do, Dt = parse_key(ours), parse_key(theirs)
        out = list(ours)
        idx = {l.split("\t")[0]: i for i, l in enumerate(out) if l.strip() and not l.startswith("#")}
        for p, w in Dt.items():
            if p not in Do:
                continue
            if Do[p] == w:
                continue
            if Do[p] in w:
                out[idx[p]] = p + "\t" + w
            elif w in Do[p]:
                pass
            else:
                out[idx[p]] = p + "\t" + Do[p] + "; " + w
        new = [p for p in Dt if p not in Do]
        if new:
            out += [""] + (banner or []) + [p + "\t" + Dt[p] for p in new]
        res = parse_key(out)
        assert set(Do) <= set(res) and set(Dt) <= set(res), "entry lost"
    else:
        out = ours + [""] + (banner or []) + sorted(parse_plain(theirs) - parse_plain(ours), key=lambda x: x.encode())
        res = parse_plain(out)
        assert parse_plain(ours) <= res and parse_plain(theirs) <= res, "entry lost"
    assert all(v.strip() for v in res.values()) if keyledger else True
    assert not any(l.startswith("<<<<<<<") or l.startswith(">>>>>>>") or l.rstrip() == "=======" for l in out)
    body = "\n".join(out) + "\n"
    if keyledger:
        assert ";;" not in body.replace("\t;\t", ""), "double clause separator"
        assert not any(";;" in v for v in res.values())
    open(out_p, "w", encoding="utf-8").write(body)
    print(f"ours={len(ours)} theirs={len(theirs)} result={len(res)} entries "
          f"(ours-only {len(set(parse_key(ours))-set(parse_key(theirs))) if keyledger else len(parse_plain(ours)-parse_plain(theirs))}, "
          f"theirs-only {len(set(parse_key(theirs))-set(parse_key(ours))) if keyledger else len(parse_plain(theirs)-parse_plain(ours))}) -> {out_p}")


main()
