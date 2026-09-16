# `030/repo2-gate` — lane report: the evidence gate (REPO-2, board card #683) and the evidence-class pass

Worktree `zene-030/wrepo2`, branch `030/repo2-gate`, cut from `release/0.3.0` @ `d7a402041`
(2026-09-16). Single-agent lane. Nothing pushed; no build tree created (no C++ was touched).

Row 56 of `docs/FEATURE-LIST-0.3.0.md` is the charter item this lane lands, and the board card is
`#683`. The owner's decision it executes is `CP-1` (`BACKLOG.md`, "Owner decisions on the change
plan — ANSWERS", 2026-09-13): *"Delete the evidence, keep its hashes, and land REPO-2's gate that
refuses evidence file types + oversized files"*.

## 1 · What the lane actually found, and what was left to do

Both halves of `CP-1` for the 0.2.x line had **already landed on the release line** before this lane
started: the gate as `a867fd493` (2026-09-13, "refuse committed evidence file types and oversized
files (REPO-2 / CP-1)") and the deletion as `85f4fc7d8` / `a1aeb020c` (`REPO-1`), whose
`tests/evidence-manifest.tsv` holds the sha256 of every removed file. What was left was narrower,
and it was measured rather than assumed:

| measurement on entry (at `d7a402041`) | result |
|---|---|
| `bash tests/evidence-gate.sh` (Gate 11) | **EXIT=1** — `6612 scanned, 1 refused`: `docs/reports/VST3-INSTRUMENT-ROW78-EVIDENCE.log` |
| `bash tests/no-upstream-regression-gate.sh` (Gate 6) | **EXIT=1** — exactly **1** `VIOLATION` line, the same path |
| `git ls-files docs/release-verification-0.2.0-alpha \| wc -l` | **0** — the "22 violations, all frozen evidence logs" of board card #634 are **already gone** (moved out of `docs/` by `3fe5addb9`, deleted by `REPO-1`; `tests/evidence-manifest.tsv` header records it) |
| row 56's own wording vs the gate's classes | **a gap**: the gate had the evidence class and the audio/renders class, but **no archive class at all** and only an incidental binary one — so a committed `.zip` or `.o` was caught only by the 1 MiB cap, which a compressed file defeats by construction |

So the lane did three things: closed the class gap in the gate (row 56's "binary/audio/archive
extension list"), made the gate's red/green control part of its own gate row, and executed the
evidence-class pass on the one live offender — turning both red gates green **by removal**.

## 2 · What was added, and where it is registered

| what | where | registration |
|---|---|---|
| **class 3, archives and packages**, refused by name anywhere: `zip tar tgz gz bz2 tbz2 xz txz 7z rar zst lz4 cab deb rpm apk whl jar` | `tests/evidence-gate.sh` (`ARCHIVE_SUFFIXES`, `is_archive_name`) | refused inside `refusal_of()`, checked by Gate 11 in `tests/run-all-gates.sh` and in CI's `static-gates` job (`.github/workflows/quality-gates.yml`, "Gate 11 - no committed evidence and nothing over the size cap") |
| **class 4, compiled/dependency artefacts**, refused by name anywhere: `o obj a lib so dylib dll exe pdb pyc pyo` | same | same |
| **`refusal_of()`** — the five refusal classes in one function so the scan loop stays a loop | same | output unchanged: `[REFUSED] <path>  (<why>)`, same order (four name classes, then the cap) |
| **the self-test grows from 4 fixtures / 6 verdicts to 9 / 13**, four of them asserting the refused path is **named** | same (`--self-test`) | the gate-11 row of `tests/run-all-gates.sh` now runs `--self-test` first and requires **both** exits 0; CI runs it as its own step ("Gate 11 control - the evidence gate's own red/green self-test") |
| **`EVIDENCE_GATE_ROOT`** — points the git-index path at another tree | same | used only by the self-test, to exercise the path `run-all-gates.sh`/CI take (a **tracked** over-cap file in a throwaway repo) without ever staging a 2 MB file in the real index |
| the four classes, the thirteen verdicts, the first live catch | `tests/QA-GATES.md` ("Gate 11" section) | the doc that `run-all-gates.sh`, CI and the board cite for the gate |
| the row's own status | `docs/FEATURE-LIST-0.3.0.md` row 56: `**to build**` → `**in the tree**`, with the classes, the measurement and the recorded open item | the charter's feature commitment list |
| **the offender's hash** | `tests/evidence-manifest.tsv` — block header (reason + the alternative considered) + one 3-column entry: `21694c1a…dad14  \t12559\t docs/reports/VST3-INSTRUMENT-ROW78-EVIDENCE.log` | the manifest `CP-1` names as the record |

Exit codes are unchanged: **0** nothing refused, **1** at least one refused, **2** setup error
(missing exemption file, blank reason, bad argument). Nothing was widened, re-anchored or exempted
to make anything pass; `tests/evidence-gate-exempt.txt` still has its four entries and no new ones.

**The offender, and why delete-with-hash rather than the re-home precedent.** It is a 12,559-byte
verbatim `ctest` + fixture-probe transcript (`1af7ccd7b`). The re-home precedent
(`SESSION-API-PROOF-transcript .txt → .md`, `727f73da5`) fits a lane's transcript of its own proof
whose citations point at it; this one is a copy of ctest output whose cases are **registered tests**
(`Vst3Instrument*` in `tests/CMakeLists.txt`), so the claim stays reproducible without shipping the
log — and re-classing run output to `*.md` is how a suffix list gets hollowed out one file at a
time. `CP-1` says the evidence file goes and the hash stays, so that is what was executed.

## 3 · What was PROVED (every command unpiped; `cmd > log 2>&1; echo EXIT=$?`)

**Red-then-green, the gate's own control (9 fixtures, 13 verdicts):**

```sh
bash tests/evidence-gate.sh --self-test > /tmp/repo2-selftest.log 2>&1; echo EXIT=$?
# EXIT=0, 13 PASS lines:
#   clean tree 0 · a .log 1 · over-cap 2 MB 1 (+ names blob.bin) · a 1 KB .zip 1 (+ names ctest-logs.zip)
#   a 4-byte .o 1 (+ names RoutingGraph.o) · a render outside data/ 1 · an exempted prefix 0
#   a blank reason 2 · a TRACKED 2 MB file through git ls-files 1 (+ names build/tracked-dump.bin)
```

**Red on the real tree, by staging one file of each new class (then unstaged, worktree left clean):**

```sh
git add -f .repo2-red/oversized-dump.bin .repo2-red/run-logs.zip   # 2 MB + 1 KB in the real index
bash tests/evidence-gate.sh > /tmp/repo2-gate11-red.log 2>&1; echo EXIT=$?
# EXIT=1
#   [REFUSED] .repo2-red/oversized-dump.bin  (2000000 bytes > 1048576 cap)
#   [REFUSED] .repo2-red/run-logs.zip  (an archive or package: a container is evidence, not source)
#   [REFUSED] docs/reports/VST3-INSTRUMENT-ROW78-EVIDENCE.log  (evidence file type: the output of a run)
# evidence-gate: 6614 file(s) scanned, 3 refused
git reset -q -- .repo2-red/ && rm -rf .repo2-red && git status --short   # → clean
```

**Green after the removal — the two gates the card names, before → after:**

| gate | at `d7a402041` (before) | at this tip (after) |
|---|---|---|
| Gate 11 `bash tests/evidence-gate.sh` | **EXIT=1**, `6612 scanned, 1 refused`, the `.log` named | **EXIT=0**, `6611 file(s) scanned, 0 refused (cap 1048576 bytes, 4 exemption(s))` |
| Gate 6 `bash tests/no-upstream-regression-gate.sh` | **EXIT=1**, `1` VIOLATION (the same `.log`) | **EXIT=0**, 0 violations — `PASS: every change to upstream-inherited code since 01148947… is declared (421 changed path(s) declared; the ledger holds 457 entries)` |

**The whole suite, with the gate-11 row as it is now wired (no build tree, mutation skipped):**

```sh
bash tests/run-all-gates.sh --no-mutation > /tmp/repo2-rag.log 2>&1; echo EXIT=$?
# EXIT=1 — SUMMARY: 3 no-tautology PASS · 6 upstream-regression PASS · 8 duplication PASS ·
#                   9 fork-sources PASS · 10 unregistered-tests PASS · 11 evidence PASS
#                   4 complexity FAIL · 7 file-length FAIL   (both INHERITED — see §4)
#                   1 ctest SKIP (no build/) · 2 coverage SKIP · 5 mutation SKIP
# gate 11's row output shows the self-test's 13 verdicts followed by
# "PASS: no committed evidence file types and nothing over the cap."
```

**The manifests still reproduce (nothing hand-patched; the deletion was expected not to touch them,
and that expectation is measured rather than asserted):**

```sh
bash tests/all-sources-reproduce.sh > /tmp/asr.log 2>&1;  echo EXIT=$?   # EXIT=0 — "REPRODUCES: the entry list in all-sources.txt is the recipe's own output."
bash tests/fork-sources-gate.sh    > /tmp/g9.log 2>&1;   echo EXIT=$?   # EXIT=0 — 629 fork-sources, 1103 all-sources, 40 tools-sources, 0 stale; the recipe's own REPRODUCES check runs inside it
```

**The manifest's counts:** 1,395 entries before this lane, **1,396** after; the new entry is
`sha256 21694c1ad9e18d542c0c8a883114b1c085c460bdbf4924ed0841431d32ddad14`, `12559` bytes, path
`docs/reports/VST3-INSTRUMENT-ROW78-EVIDENCE.log` (hash taken from the committed bytes:
`git show HEAD:… | sha256sum` equals `sha256sum <file>` — the same digest). The file still ends with
a newline and every entry line still has three TAB-separated fields, so the manifest's own verify
recipe (`awk -F'\t' '!/^#/ && NF==3 …'`) still matches all 1,396.

**The gate's own complexity, measured with the tool Gate 4 uses:**

```sh
lizard tests/evidence-gate.sh
# before the last commit: scan_tree CCN 11   (over the program's 10 target)
# after:  scan_list CCN 6 · scan_tree CCN 7 · refusal_of CCN 6 — every function under target
```

## 4 · What does not work yet — the exact, inherited reds

Neither is mine, both are already on the fix-up list's books, and both are measured here rather than
described:

- **Gate 4 (complexity), EXIT=1**: `5893 functions in the fork scope; 77 exceed CCN 10`, and the
  REGRESSION lines are the dawproject/interchange/import-detection/wasm and test-driver functions the
  2026-09-15 fix-up list already names (`dawProjectXmlFromModel` CCN 42, `parseDocument` 41,
  `dawProjectZipRead` 36, `applyDawProjectModel` 33, `check_result` 31 …). **No file this lane
  touched appears in that list** — `tests/*.sh` is not in the fork scope by construction (the recipe
  admits `\.(cpp|c|h|hpp|cc|cxx)$` plus named `.py` drivers).
- **Gate 7 (file-length), EXIT=1**: `628 fork-scope sources measured; 21 exceed 500 lines` —
  `include/ControlRegistryGroups.h` 671, `ControlRegistry.h` 509, `ControlReversibility.h` 518,
  `Vst3Host.cpp` 800→891, `tests/control-session-api-proof.py` 889, `tests/control-stable-ids-slice2.py`
  549 … again **nothing this lane touched** (`tests/evidence-gate.sh` is 381 lines,
  `tests/run-all-gates.sh` 263). The `tools` scope of both gates is green.
- **Gate 1 (ctest) was SKIPPED, not run**: this worktree has no `build/` and this lane touched no
  C++; a build is the expensive path the owner's directive says not to burn the window on. **The
  ctest side of the VST3 claim is therefore not re-verified here** — see §5.

## 5 · What could not be verified

1. **No build was configured and no ctest was run** in this worktree (`SKIP` in the suite above).
   The deleted log recorded `Vst3InstrumentFixtureProbe`, `Vst3InstrumentTest` and
   `Vst3InstrumentIntegrationTest` passing; that is a claim from the log's own text, not a
   measurement of this lane, and the tests remain registered in `tests/CMakeLists.txt` for the
   merge-tip run to re-measure.
2. **CI has not run** on this branch and cannot from here (no push this pass; `release/0.3.0` is
   local-only). The new CI step is a well-formed workflow edit (`yaml` lint clean) and is **not**
   observed green by this lane.
3. **The `plugins/RnnoiseDenoiser/testdata/` open item is NOT resolved.** Its `crash-evidence/*.log`
   and `strace_A.log` are run output that no script in the tree reads (re-measured: `grep -rn` over
   `*.py|*.sh|*.cpp|*.cmake|CMakeLists.txt` finds no reader), and they are exempt **with a reason**
   in `tests/evidence-gate-exempt.txt`, whose entry says in as many words that the owner's `CP-1`
   deletion set did not name that directory and that this is a recorded open item. Deleting them
   would also need a Gate 6 declaration (a deleted path under `plugins/` has no allowed class), so
   they are left for a pass that owns that decision.
4. **The 22-violation claim of board card #634 was not re-measured from the 0.2.x line itself** —
   this tree does not contain it. What is measured here is that **0** of those paths are tracked in
   this tree and that the only evidence-class path ever changed since the Gate 6 base was the one
   `.log` above, which is now removed. Card #634's premise is falsified **on this line**; the
   history of how the 22 went is a record (`3fe5addb9` + the manifest header), not a measurement.
5. **The acceptance contract's items 2 and 4 (CHARTER §3.1: a registered command group, a UI-absence
   line in `docs/KNOWN-LIMITATIONS.md` and the release notes) do not apply to this card and were not
   attempted.** Row 56's own "Command group / ids" cell says `n/a (a gate)`; the gate is driven by
   `run-all-gates.sh`, `--self-test` and CI, not by the control socket. No `KNOWN-LIMITATIONS.md` or
   release-notes line is owed for a repository-boundary check, and writing one would be a false
   claim about a user-visible feature.

## 6 · Hotspots and collisions

- `hotspot: tests/run-all-gates.sh` — the gate-11 row now runs two commands (self-test, then the
  tree); any sibling lane editing that row, or the summary block, must keep both exit codes.
- `hotspot: tests/evidence-gate.sh` — every lane that commits a fixture, a binary or an archive will
  now be refused by class 3/4. That is the point, but a lane with a legitimate `.zip`/`.o` fixture
  must add a reason-bearing entry to `tests/evidence-gate-exempt.txt` rather than a `.gitignore`.
- `hotspot: tests/evidence-manifest.tsv` — appended in the same format (3 TAB fields, file ends with
  a newline). A merge that re-anchors or reformats that file breaks the verify recipe in its header.
- `hotspot: docs/FEATURE-LIST-0.3.0.md` row 56 — one table cell, rewritten in place. The wave-9
  proof-debt lane also edits this table (rows 8/17/40/47/63/85); a merge should be cell-level.
- `hotspot: docs/reports/MERGE-TRAIN-030w9-PASS2.md` lines 109 and 114 — a DATE-STAMPED RECORD of the
  gate run that found the `.log` ("FAIL — NEW CLASS"). It deliberately still quotes the path and was
  NOT edited, per `docs/reports/README.md` ("editing a record of a run to match a later layout is how
  a record stops being evidence"). This lane report is the resolution record it should be read with.
- `hotspot: .github/workflows/quality-gates.yml` — one added step in the `static-gates` job.

## 7 · The single next action

**Merge `030/repo2-gate` (commits `c3c8d6fad`, `5aeb3678e`, `00b60d0e2` + the docs commit) into
`release/0.3.0` and re-run `bash tests/run-all-gates.sh --no-mutation` on the merged tip:** expect
**gate 6 and gate 11 PASS** (the two this card turns green, by removal), gates 3/8/9/10 PASS, gates
4 and 7 still FAIL on the inherited fix-up-list classes named in §4, and gates 1/2/5 SKIP without a
build — exit **3** only if the fix-up pass has meanwhile cleared gates 4 and 7, otherwise **1** for
those two, unchanged by this branch.
