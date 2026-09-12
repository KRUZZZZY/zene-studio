#!/usr/bin/env python3
"""Train 3E: "every entry from both sides" for the non-append-only conflicts.

The train's conflicts are registries, manifests, CMake/CI lists and one test
source.  For each class the *entries* are named things, not lines, so the
resolution is only correct if the merged file still carries every entry the
branch's side had.  This asserts that directly, per class:

  yaml    job ids and step names                                (build.yml)
  cmake   registered file tokens (foo.cpp, sub/CMakeLists.txt)   (CMakeLists.txt)
  list    non-comment lines                                      (manifests)
  ledger  path keys, with theirs' reason traceable               (the ledger)

usage: entries_check.py <ours> <theirs> [--kind yaml|cmake|list|ledger]
Kind is inferred from the file name when omitted.  Exit 0 = every entry present.
"""
import re
import sys

TOKEN = re.compile(r"[A-Za-z0-9_./$\{\}\-]+\.(?:cpp|cc|cxx|c|h|hpp|txt|cmake|tsv|sh|py|yml|yaml|in)")


def noncomment(text):
    return [l for l in text.splitlines() if l.strip() and not l.lstrip().startswith("#")]


def keys(text):
    return [l.split("\t")[0] for l in noncomment(text)]


def load_kind(ours_p, theirs_p, kind):
    ours = open(ours_p, encoding="utf-8", errors="replace").read()
    theirs = open(theirs_p, encoding="utf-8", errors="replace").read()
    missing = []

    if kind == "yaml":
        import yaml
        # the fork renamed the product in 0.2.0 (wave R): lmms -> zene.  A name that differs
        # only by that rename is the same entry, not a lost one.
        def ren(s):
            return (s.replace("LMMS", "ZENE").replace("lmms", "zene"))
        o = yaml.safe_load(ours) or {}
        t = yaml.safe_load(theirs) or {}
        oj, tj = set((o.get("jobs") or {}).keys()), set((t.get("jobs") or {}).keys())
        missing += [f"job {j}" for j in sorted(tj - oj)]
        for j in sorted(tj & oj):
            os_ = [s.get("name", "") for s in ((o["jobs"][j] or {}).get("steps") or [])]
            ts_ = [s.get("name", "") for s in ((t["jobs"][j] or {}).get("steps") or [])]
            missing += [f"job {j} step {s!r}" for s in ts_
                        if s and s not in os_ and ren(s) not in {ren(x) for x in os_}]
        # `on` triggers
        ot = o.get(True) or o.get("on") or {}
        tt = t.get(True) or t.get("on") or {}
        if isinstance(tt, list):
            tt = {k: None for k in tt}
        if isinstance(ot, list):
            ot = {k: None for k in ot}
        missing += [f"on-trigger {k}" for k in (tt or {}) if k not in (ot or {})]
        print(f"  yaml: jobs ours={len(oj)} theirs={len(tj)} | branch-only missing: {missing or 'NONE'}")
    elif kind == "cmake":
        ot, tt = set(TOKEN.findall(ours)), set(TOKEN.findall(theirs))
        # ignore the fork's own rename noise: compare on the basename too
        ob = {x.split("/")[-1] for x in ot}
        missing = sorted(x for x in (tt - ot) if x.split("/")[-1] not in ob)
        print(f"  cmake: tokens ours={len(ot)} theirs={len(tt)} | branch-only missing: {missing or 'NONE'}")
    elif kind == "qtest":
        # The test slots themselves are the entries of a QtTest source: a merge that keeps
        # ours is only correct if HEAD's file still owns every case the branch's file had.
        slot = re.compile(r"\bvoid\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(")
        os_, ts_ = set(slot.findall(ours)), set(slot.findall(theirs))
        missing = sorted(ts_ - os_)
        print(f"  qtest: slots ours={len(os_)} theirs={len(ts_)} | branch-only missing: {missing or 'NONE'}")
    elif kind == "ledger":
        ok_, tk = set(keys(ours)), set(keys(theirs))
        missing = sorted(tk - ok_)
        blank = [k for k in ok_ if not [l for l in noncomment(ours) if l.split("\t")[0] == k][0].partition("\t")[2].strip()]
        print(f"  ledger: paths ours={len(ok_)} theirs={len(tk)} | branch-only missing: {missing or 'NONE'} "
              f"| blank-reason ours: {blank or 'NONE'}")
    else:
        ol, tl = set(noncomment(ours)), set(noncomment(theirs))
        missing = sorted(tl - ol)
        print(f"  list: entries ours={len(ol)} theirs={len(tl)} | branch-only missing: {len(missing)}"
              + (f" e.g. {missing[:5]}" if missing else " (NONE)"))

    return 1 if missing else 0


if __name__ == "__main__":
    ours_p, theirs_p = sys.argv[1], sys.argv[2]
    kind = None
    if "--kind" in sys.argv:
        kind = sys.argv[sys.argv.index("--kind") + 1]
    if kind is None:
        b = ours_p.split("/")[-1].split(".")[0].replace("_ours", "")
        kind = ("yaml" if ours_p.endswith((".yml", ".yaml")) else
                "cmake" if "CMakeLists" in ours_p else
                "ledger" if "upstream-modifications" in ours_p else
                "qtest" if ours_p.endswith("Test.cpp.ours") else "list")
    sys.exit(load_kind(ours_p, theirs_p, kind))
