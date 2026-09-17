# ATTR-1 LANE 030/attr-1 (the `creator=` revert) — 2026-09-17

Worktree `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-030/wattr`,
branch **`030/attr-1`** (@ `f16baff79` when this lane started; the 0.3.0 release tip).
**Nothing was pushed, nothing was merged, no board was written.** No build directory exists in
this tree and none was needed: every file this lane touches is data, and the one registered test
that reads them needs no build (below).

| # | commit | subject |
|---|---|---|
| 1 | `1d8512cb9` | `fix(attr): revert creator="Zene Studio" on the 38 shipped .mmpz demos (ATTR-1)` |
| 2 | HEAD at handover | `docs(attr): the ATTR-1 report and the one fork holder in the rename record` — this file |

## 1 · The premise, corrected by measurement

The card (board 687) and the lane brief say "revert the 226 upstream-shipped preset/demo files".
Measured at `f16baff79`, that is not the state of the tree:

* **`c6dbd2d55`** (`fix(attr): revert the creator rewrite on inherited presets and name one fork
  holder`, 2026-09-13) is an ancestor of this tip. It reverted the creator on **184** plain-text
  files (175 `.xpf`, 6 `.mpt`, 3 `.mmp`) and left the 38 `.mmpz` deliberately, because they are
  qCompress containers and it would not hand-edit a binary.
* What was left for this lane is therefore the **38 `.mmpz` demo/shortie projects** — the revert
  itself, through the container — plus the ledger correction, the holder name in the manifests and
  docs, and the proof.

The 38 files now carry the upstream value, and the ATTR-1 candidate list is finished. The rest of
this report is the measurement behind every one of those statements.

## 2 · The candidate list — derived, not assumed

The card's number comes from `git diff --name-status 4e677cb6c6ab b099fd6cb -- data | grep -c '^M'`,
which reproduces **226** at `b099fd6cb` (2026-09-12). That command counts every modified file under
`data/` in a two-day window, so its set is not the layer-3 rewrite set:

```
$ git show --name-status --format= c30c93082 -- data | sed -n 's/^M\t//p' | sort | wc -l   # 222
$ git diff --name-status 4e677cb6c6ab b099fd6cb -- data | grep -c '^M'                      # 226
$ git show --name-only --format= c30c93082 | sed '/^$/d' | wc -l                            # 226 (commit total)
```

The first form (`sed -n 's/^M\t//p'`, not `awk '{print $NF}'`) matters: 16 of these paths contain a
space, and a last-field `awk` silently truncates them.

* the layer-3 commit `c30c93082` touched **226** files in total — **222 under `data/`** plus
  `src/core/DataFile.cpp`, `tests/CMakeLists.txt`, `tests/all-sources.txt`,
  `tests/src/core/DataFileFormatTest.cpp`;
* the card's 226-file set contains **4 paths layer 3 never rewrote** (`data/CMakeLists.txt`, two
  `data/backgrounds/*.png`, `data/themes/default/splash.png`) and excludes those 4 source paths;
* so the list this lane works from is the **222 data files the layer-3 commit itself modified**,
  which is also the count `docs/RENAME-COMPLETE.md` §4 carries.

Measured composition of the 222 (all commands re-runnable at the tip):

| class | files | divergence from `4e677cb6c6ab` |
|---|---|---|
| `.xpf` presets, `multimedia-project` root | 62 | none — **byte-identical** |
| `.xpf` presets, `lmms-project` root renamed | 113 | root-element rename only |
| `.mpt` templates | 6 | root-element rename only |
| `.mmp` projects | 3 | root-element rename only |
| `.mmpz` demos/shorties (qCompress) | 38 | root-element rename only — **reverted by this lane** |
| **total** | **222** | |

The base commit used is **`4e677cb6c6ab`**, the upstream commit this fork is based on: it is the
commit named in the header of `tests/fork-sources.txt` and in `tests/no-upstream-regression-gate.sh`
(which verifies `git ls-tree -r --name-only 4e677cb6c6ab -- tools` prints nothing), and the ledger's
own SCOPE note names it as the fork point. `git merge-base HEAD origin/master` is *not* used: this
clone's release line is far ahead of the fork point and the base the whole gate suite and the ledger
refer to is `4e677cb6c6ab`.

## 3 · The revert — the container round trip

`data/**/*.mmpz` is `[4-byte big-endian uncompressed length][zlib stream]` (Qt `qCompress`). The
same round trip layer 3 wrote them with is used in reverse, with nothing assumed about the encoder:

```
zlib level 6 reproduces every one of the 38 files byte-for-byte, at BOTH commits:
  struct.pack(">I", len(x)) + zlib.compress(x, 6) == git show <rev>:<path>      # 38/38, rev = 4e677cb6c6ab and HEAD
```

Per file the revert script asserts, before anything is written:

1. the root element's `creator` value in the working tree is `Zene Studio` and occurs exactly once;
2. the restored value is read from `4e677cb6c6ab`, never assumed (**all 38 read `LMMS`**);
3. the produced XML, diffed against the base commit's XML, differs by the **root-element rename and
   nothing else** (DOCTYPE + root open tag + root close tag);
4. no `creator="Zene Studio"` survives in the produced XML;
5. the recompressed bytes decompress back to exactly the XML that was written, and the declared
   length matches;
6. the XML is well-formed.

What each file looks like now (decompressed, first elements):

```xml
<!DOCTYPE zene-project>
<zene-project creator="LMMS" creatorversion="1.2.0" type="song" version="1.0">
```

## 4 · The two counts, measured and named

| count | value |
|---|---|
| ATTR-1 candidate list (layer-3 `data/` files) | **222** |
| of those, byte-identical to `4e677cb6c6ab` | **62** |
| of those, differing by the retained root-element rename only | **160** |
| of those, differing in any other way | **0** |
| of those, still carrying `creator="Zene Studio"` | **0** |
| files carrying `creator="Zene Studio"` in the whole tree | **8** — all fork-authored (none exists at `4e677cb6c6ab`), 5 of them as an XML attribute in fixtures/tooling |
| files carrying the holder `Zene Studio contributors` (at `1d8512cb9`) | **697** |

The 8 that keep the fork creator are exactly the fork-authored set — no upstream-inherited file
carries it:

```
docs/KNOWN-LIMITATIONS.md                 (prose)      tests/data/modulation-layer-fixture.mmp   (XML fixture)
docs/RENAME-COMPLETE.md                   (prose)      tests/src/core/DataFileFormatTest.cpp     (inline XML fixture)
tests/data/clip-window/render-proof.sh    (fixture)    tests/upstream-modifications.txt          (prose: this revert)
tests/data/warp/render-proof.sh           (fixture)    tools/rack-render-fixture.py              (generated XML)
```

`Zene Studio contributors` is the ONE holder for fork-authored files (ATTR-2 / ATTR-4), and
`creator="Zene Studio"` is the attribute the build's writer emits for a file it writes
(`src/core/DataFile.cpp:141` in the constructor, `:328` in `write()`); `CMakeLists.txt`'s
`PROJECT_COPYRIGHT` carries the same holder beside the upstream one. No new name was invented here
and no third spelling introduced.

## 5 · Proof

**Byte-identity / classification — one loop over the list.** `/tmp/attr1_proof.py` (recipe in §9)
prints, at this tip:

```
candidate list            : 222 files (layer-3 commit c30c93082, data/)
byte-identical to 4e677cb6c6ab : 62
root-rename divergence only: 160
anything else (FAIL)      : 0
still carrying creator="Zene Studio": 0
PROOF OK
```

The same check as one shell command — the files that must be byte-identical are the candidate list
minus the base-diff set, so the count is a `comm`:

```bash
git show --name-status --format= c30c93082 -- data | sed -n 's/^M\t//p' | sort > /tmp/l3.txt   # 222
comm -23 /tmp/l3.txt <(git diff --name-only 4e677cb6c6ab -- data | sort) | wc -l                #  62
comm -23 /tmp/l3.txt <(git diff --name-only 4e677cb6c6ab -- data | sort) | grep -c '\.xpf$'     #  62
```

**XML well-formedness — 38/38 edited files, before the write and after the read-back.** `xmllint`
is **not installed on this box** (`command -v xmllint` → nothing, and no `libxml2-utils`), so the
validator used is Python's `xml.etree.ElementTree` (expat), applied to the **decompressed** payload —
which is the only way to validate a `.mmpz` at all: pointing `xmllint` at the container's raw bytes
would be validating a zlib stream, not XML. 38/38 parsed; #4's read-back assertion passed for all 38.

**The registered test that reads these files.** `tools/mmpz-git/tests/test_mmpz_git.py` (registered
as ctest `MmpzGitDepthTest`, `tests/CMakeLists.txt:3126`) globs **every** `data/projects/**/*.mmpz`
as a real fixture and uses `data/projects/shorties/sv-DnB-Startup.mmpz` — one of the 38 — for its
merge, diff, conflict and binary-safety cases. Run before and after the change:

```
$ python3 tools/mmpz-git/tests/test_mmpz_git.py -v
Ran 45 tests in 86.570s   OK (skipped=7)        # before
Ran 45 tests in 92.521s   OK (skipped=7)        # after
```

The 7 skips are the binary-dependent classes (`build/zene` absent, exactly as the ctest comment
documents) — identical in both runs, so no test moved. `tests/control-golden-audio.py`
(`tests/CMakeLists.txt:2676`) also loads `data/projects/shorties/Root84-TrancyLoop.mmpz`, but it
drives a live instance of a built binary and this tree has no build; it is not run here (§8).

## 6 · The ledger, and where the holder name was written

`tests/upstream-modifications.txt` — in place, no path added or removed, order and grouping
untouched (its entries are grouped by section comment, not globally sorted; the file's generated
sibling `tests/fork-sources.txt` is the one with the sorted-entry + reproduce recipe, and it was not
touched by this change):

* the **38 `.mmpz` reasons** stopped claiming `creator="Zene Studio"` and stopped claiming a
  `multimedia-project` root these files never had (their root was `lmms-project`, renamed by layer
  3). Each now reads: *"rename layer 3: shipped demo project written in the new format
  (zene-project root); ATTR-1 (2026-09-17): creator reverted to the upstream value from
  4e677cb6c6ab through the [4-byte BE length][zlib] container round trip (owner decision, BACKLOG
  `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file"*;
* the **"ATTR-1 remainder" note** is replaced by one recording the finished revert (222 = 62 + 160),
  the round-trip recipe, the zero-fork-creator result, and the one identity fork-authored files keep:
  the writer's `creator="Zene Studio"` and the holder **"Zene Studio contributors"**
  (ATTR-2 / ATTR-4, `docs/RENAME-COMPLETE.md` §5(i));
* entry count is unchanged (**460**), and Gate 6 re-reads all of them with 0 violations.

`docs/RENAME-COMPLETE.md` — the rename record, which is the document the decision names:

* the layer-3 sweep table keeps its historical "layer 3, final tree" reading and gains a dated
  **remeasured** note: `git grep -l 'creator="LMMS"' | wc -l` → **220** files (183 under `data/`),
  plus the 38 `.mmpz` whose decompressed XML carries it again — 258 in all;
* §5 gains **(i)**: the creator revert (62 + 160 measured), the retained root rename, the container
  round trip, and the single fork holder **"Zene Studio contributors"**.

## 7 · Gates — exit codes as run

| gate | command | exit |
|---|---|---|
| Gate 4 (per-method complexity) | `bash tests/complexity-gate.sh --check` | **0** |
| Gate 6 (no undeclared divergence) | `bash tests/no-upstream-regression-gate.sh` | **0** — "422 changed path(s) declared; the ledger holds 460 entries", 0 violations |
| Gate 7 (file-length ratchet) | `bash tests/file-length-gate.sh --check` | **0** |
| Gate 8 (duplication) | `bash tests/duplication-gate.sh --check` | **0** — duplicated lines 0.52% (budget 5%) |
| Gate 9 (sources registered) | `bash tests/fork-sources-gate.sh` | **0** — 1732 files scanned, 0 stale entries |

Gates 4, 7 and 8 are not required to move for a `data/`-only change (`tests/fork-sources.txt` is
untouched, so no ratchet scope moved); they are reported because they were red at an earlier base
and are green at this one. Gate 6 is the gate this lane had to clear, and it re-classifies all 422
changed paths since its base `01148947e` with the ledger's new reasons.

## 8 · Residuals, and what is not verified

* **The 160 files are not byte-identical to upstream, and are not meant to be.** The layer-3
  root-element rename (`lmms-project` → `zene-project`, DOCTYPE included) is a separate deliberate
  layer that ATTR-1 does not revert — `c6dbd2d55` kept it on the 122 plain-text files and this lane
  kept it on the 38 `.mmpz` for the same reason. So the byte-identity count is **62 of 222**, not
  226 of 226. If the owner wants the demo data reduced to zero divergence, restoring the base blobs
  is a one-line change per file (`git checkout 4e677cb6c6ab -- <path>`) — it was available and was
  not taken, because it would reverse a decision this card does not own.
* **No binary load test.** There is no build in this tree, and the only built binary in the
  workspace (`zene-030/build/zene`) does not start: `libwasmtime.so` is absent from the box
  (`find /home/kruzzzzy -maxdepth 6 -name 'libwasmtime.so*'` → nothing), so a render of a reverted
  project could not be driven. The load-level evidence is the registered mmpz suite (§5) plus the
  decompress/parse/recompress invariants, not a render.
* **`xmllint` is not installed**; well-formedness was checked with expat/ElementTree on the
  decompressed payload (§5).
* **The 38 ledger entries stay** even though the files no longer differ from the base commit. They
  are still "changed" against Gate 6's own base `01148947e` (content there is the fork version), so
  the gate needs them declared; the 62 files whose paths left the gate's changed set entirely had
  their entries removed by `c6dbd2d55`, which is the ledger's stated policy.
* **`docs/RENAME-COMPLETE.md` §4's own arithmetic reads oddly** against this measurement: it says
  "174 shipped presets" were rewritten in the new form, while the measured root-renamed preset count
  is 113 (and 62 presets were attribute-only, 175 `.xpf` in all). The doc's numbers are left as the
  historical record; this report is the remeasurement.
* **Ledger lines are edited in place**, so a sibling lane editing the same lines would conflict —
  this lane changes only `data/**` reasons and the note; the sibling `030/arch2-api` work is source,
  not `data/`.
* **Unverified by this lane:** that the reverted demos render identically to before. The creator
  attribute is not read by the loader for anything but the version notice, and the change is
  asserted to be XML-equivalent-to-base-except-the-root-tag, but no render was run (§8, "no binary
  load test").

## 9 · Reproduce

```bash
WT=/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-030/wattr
cd "$WT"

# candidate list (222) and the card's 226-command for comparison
git show --name-status --format= c30c93082 -- data | sed -n 's/^M\t//p' | sort > /tmp/l3.txt
git diff --name-status 4e677cb6c6ab b099fd6cb -- data | grep -c '^M'        # 226 (2-day window)

# the proof: classification + zero fork creator  (script in the appendix)
python3 /tmp/attr1_proof.py

# the same proof, self-contained (no script file), if the appendix is not to hand
comm -23 /tmp/l3.txt <(git diff --name-only 4e677cb6c6ab -- data | sort) | wc -l      # 62 byte-identical
git grep -c 'creator="Zene Studio"' -- . | wc -l                                     # 8, all fork-authored

# the container round trip, per file
python3 - <<'PY'
import subprocess, zlib, struct
def blob(rev, p): return subprocess.run(["git","show",f"{rev}:{p}"],capture_output=True,check=True).stdout
def comp(x): return struct.pack(">I", len(x)) + zlib.compress(x, 6)
for p in [l for l in open("/tmp/l3.txt").read().splitlines() if l.endswith(".mmpz")]:
    assert comp(zlib.decompress(blob("4e677cb6c6ab", p)[4:])) == blob("4e677cb6c6ab", p)   # level 6 is the encoder
    assert comp(zlib.decompress(blob("HEAD", p)[4:])) == blob("HEAD", p)
print("38/38 containers reproduce at zlib level 6")
PY

# gates
bash tests/no-upstream-regression-gate.sh ; echo "gate6=$?"
bash tests/complexity-gate.sh --check ; echo "gate4=$?"
bash tests/file-length-gate.sh --check ; echo "gate7=$?"
bash tests/duplication-gate.sh --check ; echo "gate8=$?"
bash tests/fork-sources-gate.sh ; echo "gate9=$?"

# the registered test that reads the demos
python3 tools/mmpz-git/tests/test_mmpz_git.py -v        # Ran 45 tests ... OK (skipped=7)
```

---

## Appendix · the proof script, verbatim

Run from the worktree root with `/tmp/l3.txt` present (recipe above), or without it: the
script derives the candidate list itself.

```python
#!/usr/bin/env python3
"""ATTR-1 proof: over the measured candidate list (the 222 data/ files the
layer-3 commit c30c93082 modified), assert that

  1. no file carries the fork creator any more, and
  2. every file is either byte-identical to 4e677cb6c6ab (the upstream base
     commit) or differs from it by the retained layer-3 root rename ONLY
     (DOCTYPE + root open tag + root close tag) -- nothing else,

and print the two counts with the file lists.  The .mmpz container is
[4-byte big-endian length][zlib stream]; the check decompresses it first.

Usage: python3 /tmp/attr1_proof.py [--list <path>]
"""
import difflib, os, subprocess, sys, zlib

WT = "/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-030/wattr"
BASE = "4e677cb6c6ab"
FORK_CREATOR = b'creator="Zene Studio"'

def git(*a, check=True):
    r = subprocess.run(["git", "-C", WT] + list(a), capture_output=True)
    if check and r.returncode:
        raise SystemExit(r.stderr.decode())
    return r.stdout

def blob(rev, path):
    return git("show", f"{rev}:{path}")

def payload(path, data):
    """decompressed text for the .mmpz container, else the bytes as they are."""
    if path.endswith(".mmpz"):
        n = int.from_bytes(data[:4], "big")
        x = zlib.decompress(data[4:])
        assert n == len(x), path
        return x
    return data

def root_rename_only(base_lines, cur_lines):
    """every changed line is one of the three layer-3 root renames."""
    changed = [l for l in difflib.unified_diff(base_lines, cur_lines, lineterm="", n=0)
               if l[:1] in ("+", "-") and l[:3] not in ("+++", "---")]
    if not changed:
        return True, changed
    rem = [l[1:] for l in changed if l.startswith("-")]
    add = [l[1:] for l in changed if l.startswith("+")]
    if len(rem) != len(add):
        return False, changed
    for r, a in zip(rem, add):
        rr = a
        for old, new in (("<!DOCTYPE lmms-project>", "<!DOCTYPE zene-project>"),
                         ("<lmms-project", "<zene-project"),
                         ("</lmms-project>", "</zene-project>")):
            rr = rr.replace(new, old)
        if rr != r:
            return False, changed
    return True, changed

cand = sys.argv[sys.argv.index("--list") + 1] if "--list" in sys.argv else None
if cand:
    paths = [l for l in open(cand).read().splitlines() if l]
else:
    paths = sorted(l.split("\t")[-1] for l in
                   git("show", "--name-status", "--format=", "c30c93082", "--", "data").decode().splitlines()
                   if l.startswith("M"))

identical, renamed, bad, carrier = [], [], [], []
for p in paths:
    base_raw, cur_raw = blob(BASE, p), open(os.path.join(WT, p), "rb").read()
    b, c = payload(p, base_raw), payload(p, cur_raw)
    if FORK_CREATOR in c:
        carrier.append(p)
    if b == c:
        identical.append(p)
        continue
    ok, changed = root_rename_only(b.decode("latin-1").splitlines(),
                                   c.decode("latin-1").splitlines())
    (renamed if ok else bad).append(p)
    if not ok:
        print("NOT root-rename-only:", p)
        for l in changed[:8]:
            print("    ", l[:160])

print("candidate list            : %d files (layer-3 commit c30c93082, data/)" % len(paths))
print("byte-identical to %s : %d" % (BASE, len(identical)))
print("root-rename divergence only: %d" % len(renamed))
print("anything else (FAIL)      : %d" % len(bad))
print("still carrying creator=\"Zene Studio\": %d" % len(carrier))
open("/tmp/attr1_identical.txt", "w").write("\n".join(identical) + "\n")
open("/tmp/attr1_renamedonly.txt", "w").write("\n".join(renamed) + "\n")
assert not bad and not carrier
print("PROOF OK")
```
