#!/usr/bin/env python3
"""Pre-commit registration/ledger check for merge train 3B.

Replicates Gate 6's mechanical rule (tests/no-upstream-regression-gate.sh) against
the merged result so the registration checklist can be applied *in* the merge
commit, and sanity-checks the three scope manifests and the divergence ledger.

usage: precommit_check.py [INDEX|HEAD] [--sides <dir>]
"""
import subprocess, sys, os, glob

W = "/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-pa-foreign"
BASE = "01148947ea4d8bdb05c237942758d61acc867223"


def sh(c):
    r = subprocess.run(["bash", "-c", c], cwd=W, capture_output=True, text=True)
    return r.returncode, r.stdout, r.stderr


def entries(rel):
    rc, o, e = sh(f"grep -vE '^[[:space:]]*(#|$)' {rel}")
    ls = [l for l in o.splitlines() if l.strip()]
    return ls


def ok6(f):
    if (f.startswith("tests/") or f.startswith(".github/") or f.endswith(".md")
            or f in ("CMakeLists.txt", ".gitignore") or f.endswith(".cmake")
            or f.endswith("/CMakeLists.txt")):
        return True
    return False


rev = sys.argv[1] if len(sys.argv) > 1 and not sys.argv[1].startswith("--") else "INDEX"
sides = None
if "--sides" in sys.argv:
    sides = sys.argv[sys.argv.index("--sides") + 1]

if rev == "INDEX":
    sh(f"git diff --name-only --cached {BASE}")
    rc, o, e = sh(f"git diff --name-only --cached {BASE}")
else:
    rc, o, e = sh(f"git diff --name-only {BASE}..{rev}")
changed = [l for l in o.splitlines() if l.strip()]

fork = entries("tests/fork-sources.txt")
alls = entries("tests/all-sources.txt")
tools = entries("tests/tools-sources.txt")
led = {}
for l in open(f"{W}/tests/upstream-modifications.txt", encoding="utf-8"):
    if l.startswith("#") or not l.strip():
        continue
    p, _, w = l.partition("\t")
    led[p] = w

fs, as_, ts = set(fork), set(alls), set(tools)
viol = [f for f in changed if not ok6(f) and f not in fs and f not in ts and f not in led]
# NB: the real Gate 6 does NOT consult tests/all-sources.txt - only fork-sources,
# tools-sources and the ledger (plus the path/extension categories in ok6).
print(f"rev={rev} changed-vs-base={len(changed)}")
print(f"manifests: fork={len(fork)} (dup {len(fork)-len(fs)}) all={len(alls)} (dup {len(alls)-len(as_)}) "
      f"tools={len(tools)} (dup {len(tools)-len(ts)})")
print(f"ledger={len(led)} blank={[p for p,w in led.items() if not w.strip()]} "
      f"';;'={[p for p,w in led.items() if ';;' in w]}")
print(f"GATE6-REPLICA violations: {viol if viol else 'NONE'}")
# cross-manifest duplicates that the one-file-one-home rule forbids for non-C/C++
dup_cross = sorted((fs & ts) | (as_ & ts))
print(f"cross-manifest entries (fork∩tools, all∩tools): {dup_cross if dup_cross else 'NONE'}")
if sides:
    def rd(p):
        return set(l for l in open(p, encoding="utf-8").read().splitlines()
                   if l.strip() and not l.lstrip().startswith("#"))
    for f in sorted(glob.glob(os.path.join(sides, "*"))):
        name = os.path.basename(f)
        if name.endswith(".ours"):
            base = name[:-5]
            th = os.path.join(sides, base + ".theirs")
            if not os.path.exists(th):
                continue
            if base == "upstream-modifications.txt":
                def pathsof(p):
                    return set(l.split("\t")[0] for l in open(p, encoding="utf-8").read().splitlines()
                               if l.strip() and not l.startswith("#"))
                O, T, res = pathsof(f), pathsof(th), set(led)
            else:
                def pathsof(p):
                    return set(l for l in open(p, encoding="utf-8").read().splitlines()
                               if l.strip() and not l.lstrip().startswith("#"))
                O, T = pathsof(f), pathsof(th)
                res = {"fork-sources.txt": fs, "all-sources.txt": as_,
                       "tools-sources.txt": ts}.get(base, set())
            print(f"union[{base}]: ours={len(O)} theirs={len(T)} result={len(res)} "
                  f"ours-only-missing={sorted(O-res)} theirs-only-missing={sorted(T-res)}")
sys.exit(1 if viol else 0)
