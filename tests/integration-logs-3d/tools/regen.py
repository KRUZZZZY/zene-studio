#!/usr/bin/env python3
"""Merge-train 3B manifest regenerator/verifier.

The three commands below are transcribed verbatim from the "Regenerate with" /
"Verify it" blocks of tests/fork-sources.txt, tests/all-sources.txt and
tests/tools-sources.txt as committed at merge 3A-1 (pull request
merge train 3B #1).  `--rev HEAD` reproduces the header's command exactly (it is
what the header says, and it is the post-commit proof); `--rev INDEX` is the same
command against the index so a merge can be checked before it is committed.
"""
import subprocess, sys, os

W = "/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-pa-integration"
BASE = "4e677cb6c6ab"

AWK = (r"awk '/^tests\// && !/^tests\/src\/core\/("
       r"ClipSerialisationTest|DataFileSaveIntegrityTest|LufsMeterTest|MidiLearnTest|MixerConcurrencyTest|ProjectOpenIntegrityTest|SessionModelTest|StemExportTest"
       r")\.(cpp|h)$/ && !/^tests\/src\/core\/StemExportTestSupport\.h$/ && "
       r"!/^tests\/src\/tracks\/SampleClipWindowTest\.cpp$/ { next } { print }'")

EXCL = r"grep -vE '^(src/3rdparty/|plugins/NeuralAmp/rtneural/|plugins/NeuralAmp/nam/|plugins/NeuralAmp/tests/|plugins/RnnoiseDenoiser/rnnoise/)'"


def fork_cmd(rev):
    spec = f"{BASE}..{rev}" if rev != "INDEX" else None
    if rev == "INDEX":
        d = f"git diff --name-only --cached --diff-filter=A {BASE}"
    else:
        d = f"git diff --name-only --diff-filter=A {BASE} {rev}"
    return (f"{{ {d} -- src include plugins tests/src/core tests/src/tracks "
            f"| grep -E '\\.(cpp|c|h|hpp|cc|cxx)$' | {EXCL} | {AWK} ; "
            f"{d} -- tools/local-ci.sh tools/ncpu-shim.c plugins/Vst3Instrument/logo.png ; }} | LC_ALL=C sort")


def all_cmd(rev):
    return ("git ls-files '*.c' '*.cc' '*.cpp' '*.cxx' '*.h' '*.hpp' "
            "| grep -vE '^(src/3rdparty/|tests/reference/|plugins/NeuralAmp/(rtneural|nam|tests)/|plugins/RnnoiseDenoiser/rnnoise/)' "
            "| LC_ALL=C sort")


def tools_cmd(rev):
    if rev == "INDEX":
        d = f"git diff --name-only --cached --diff-filter=A {BASE}"
    else:
        d = f"git diff --name-only --diff-filter=A {BASE} {rev}"
    return (f"{d} -- tools | grep -E '\\.(py|sh|cpp|c|h|hpp|cc|cxx)$' "
            "| grep -vE '^(tools/local-ci\\.sh|tools/mmpz-git/scratch/(qtesc|qtroundtrip|qtsave|qtsave2)\\.cpp|tools/ncpu-shim\\.c|tools/wasm/wat2wasm\\.cpp)$' "
            "| LC_ALL=C sort")


MANIFESTS = {
    "fork-sources.txt": fork_cmd,
    "all-sources.txt": all_cmd,
    "tools-sources.txt": tools_cmd,
}


def sh(c):
    r = subprocess.run(["bash", "-c", c], cwd=W, capture_output=True, text=True)
    return r.returncode, r.stdout, r.stderr


def body(path):
    """(header_lines, entry_lines) of a manifest."""
    txt = open(os.path.join(W, path), encoding="utf-8").read().splitlines()
    k = next((i for i, l in enumerate(txt) if l.strip() and not l.lstrip().startswith("#")), len(txt))
    return txt[:k], [l for l in txt[k:] if l.strip()]


def regen(name, rev, write=False):
    hdr, cur = body(f"tests/{name}")
    rc, out, err = sh(MANIFESTS[name](rev))
    if rc != 0:
        print(f"FAIL {name}: regeneration command exited {rc}: {err[:300]}")
        return 1, None
    new = [l for l in out.splitlines() if l.strip()]
    added = [e for e in new if e not in set(cur)]
    removed = [e for e in cur if e not in set(new)]
    ok = added == [] and removed == [] and len(new) == len(set(new))
    print(f"{name:20s} rev={rev:5s} entries={len(new)} duplicates={len(new)-len(set(new))} "
          f"+{len(added)} -{len(removed)} {'REPRODUCES' if ok else 'DOES-NOT-REPRODUCE'}")
    for e in added:
        print("   +", e)
    for e in removed:
        print("   -", e)
    if write:
        open(os.path.join(W, f"tests/{name}"), "w", encoding="utf-8").write(
            "\n".join(hdr) + "\n" + "\n".join(new) + "\n")
    return (0 if ok else 1), new


if __name__ == "__main__":
    args = sys.argv[1:]
    rev = "INDEX"
    write = False
    names = list(MANIFESTS)
    for a in args:
        if a == "--write":
            write = True
        elif a == "--head":
            rev = "HEAD"
        elif a in ("INDEX",):
            rev = "INDEX"
        else:
            names = [a]
    bad = 0
    for n in names:
        rc, _ = regen(n, rev, write)
        bad |= rc
    print("VERIFY RESULT:", "ALL-REPRODUCE" if bad == 0 else "MISMATCH")
    sys.exit(1 if bad else 0)
