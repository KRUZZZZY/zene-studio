# Integration merges into `post-alpha/integration` — wave 2 (the ten post-alpha lanes)

Worktree: `projects/lmms-fl-research/zene-pa-integration` (branch `post-alpha/integration`).
Entry tip: `c3d4d38fc` (14 lanes already merged, tree green: build clean under the CI's flags,
ctest 31/31, gates `0 / 0 / 3`).

Nothing was pushed; no PR, issue or remote was touched; no branch was rebased, amended, reset or
rewritten. One commit per lane. Every exit code below was measured unpiped
(`cmd > log 2>&1; echo EXIT=$?`). Gate 9 = `tests/fork-sources-gate.sh`,
Gate 6 = `tests/no-upstream-regression-gate.sh`, `run-all-gates.sh` is expected to exit **3**
(`PASS-WITH-SKIPS`: gate 2 coverage needs `--with-coverage`).

| # | Lane | Merge commit | Conflicts | Gate 9 | Gate 6 | `run-all-gates.sh` | ctest |
|---|------|--------------|-----------|--------|--------|--------------------|-------|
| 1 | `post-alpha/vst3-instrument-fixture` | `bc2c957ad` | 1 file | 0 | 0 | 3 | 31/31 |
| 2 | `post-alpha/plugin-hosting-in-release` | *(none — already up to date)* | — | — | — | — | — |
| 3 | `post-alpha/pipeline-hardening` | `87b9a5397` | 3 files | 0 | 0 | 3 † | 31/31 |
| 4 | `post-alpha/stem-export` | `a4fe66c4f` | 2 files | 0 | 0 | 3 | 32/32 |
| 5 | `post-alpha/lufs-wire` | `1ef366607` | 2 files | 0 | 0 | 3 | 33/33 |
| 6 | `post-alpha/midi-race` | `36d676919` | 5 files | 0 | 0 | 3 | 35/35 |
| 7 | `post-alpha/clip-slice0` | `5c6c9379c` | 3 files | 0 | 0 | 3 | 37/37 |
| 8 | `post-alpha/autosave` | `749d927b9` | 3 files | 0 | 0 | 3 | 38/38 |
| 9 | `post-alpha/automation-modes` | `89ab8d029` | 2 files | 0 | 0 | 3 | 39/39 |
| 10 | `post-alpha/midi-depth` | `0a92be489` | 3 files | 0 | 0 | 3 ‡ | ‡ |

† lane 3's first `run-all-gates.sh` reported gate 1 **FAIL** on `PdcMixerTest`; it is a
pre-existing teardown flake, not a regression — see "Two pre-existing defects found" below. The
re-run exited 3 (PASS-WITH-SKIPS) with ctest 31/31.
‡ lane 10's figures are in the final-verification section.

## Lane 2 has no merge commit, and why

`post-alpha/vst3-instrument-fixture` and `post-alpha/plugin-hosting-in-release` are the **same
commit** (`a24488ba53647e802c85fde43ab459dcd5d99af1`, verified with `git rev-parse` on both refs);
lane 2 was stacked on lane 1's branch. Its six commits interleave both lanes' work
(`bb554ca83`, `f32dc7cd1` = lane 1; `f816ce8f5`, `71306eeae`, `f3be490c4`, `a24488ba5` = lane 2).
So merging lane 1 lands lane 2 as well, and `git merge post-alpha/plugin-hosting-in-release` reports
**`Already up to date.`** — there is no commit for it to create, and none was fabricated: an
already-merged ref cannot produce a merge commit without rewriting history, which is forbidden here.
Its content (the CI provisioning of the pinned VST3 SDK + CLAP headers, `tests/release-honesty-gate.sh`,
`tools/local-ci.sh` wiring) is in `bc2c957ad`.

## Method: how a ledger conflict was resolved

Every conflict in this wave was in an append-only registry or a CI/CMake list. Nothing was resolved
by unioning **lines** across conflict markers, and nothing was resolved by hand-editing a ledger.

For each conflicted ledger the two sides were read from the index (`git show :2:<file>` = integration,
`:3:<file>` = lane), parsed into `(preamble, [ (path, line, comment-block-above) ])`, the entry **set**
was unioned by path, and the result was re-emitted keeping integration's order as the skeleton while
each lane-only entry was inserted directly after the nearest preceding entry the two sides share — so
a file that is sorted stays sorted, and the grouped ledgers (`upstream-modifications.txt` is grouped
by lane, not sorted) keep their grouping and their comment blocks in place.

Everything was then verified programmatically, in the resolver itself:

* the parser/emitter round-trip is **byte-exact for either side alone** (idempotence — this is the
  assertion that catches the class of bug a previous lane hit, where a `list.extend` re-emitted a
  comment block one character per line);
* no conflict markers in the result;
* no duplicate path;
* **every entry from both sides is present**;
* every reason non-empty, no `;;`, no blank segment (Gate 6 refuses a blank reason with exit 2).

For `tests/CMakeLists.txt` the hunks were resolved as a union of the *entries* inside the list
(`src/core/XTest.cpp` lines), never a line union — a structural `)` or entry silently deduped is the
failure mode that ruins a CMake list.

Reasons were **read off the diff** in every case. Where both sides had independently rewritten the
same reason, the rule was: keep the superset when one side's text extends the other's, otherwise keep
both as clause segments (ours first). Lane 3 already declared its own hooks, as did lanes 4–10, so no
reason had to be authored from scratch in this wave — only merged.

## Lane 1 — `post-alpha/vst3-instrument-fixture` (`bc2c957ad`)

The pinned VST3 SDK recipe, the `SYSTEM`-property fix that lets `lmms_vst3_sdk` build under
`-DUSE_WERROR=ON`, and a validated MIT instrument fixture. Also carries lane 2 (above).

**Conflicted:** `tests/all-sources.txt` only — one two-way hunk, resolved as an entry union:
`+tests/data/vst3-test-instrument/vst3-test-instrument.cpp`,
`+tests/src/plugins/Vst3InstrumentFixtureProbe.cpp` (both in their sorted position). `.github/workflows/build.yml`
auto-merged here; the conflict with lane 3 on that file came later, as expected.

**Checklist.** (a) the lane's two new C++ sources are registered (the union is what registers
`Vst3InstrumentFixtureProbe.cpp`, and the lane had already added the fixture source to
`tests/all-sources.txt`). (b) the only inherited-ish files it touches are `.github/workflows/build.yml`
and `tools/local-ci.sh`; Gate 6 treats CI config as non-runtime and `tools/local-ci.sh` is already
fork-NEW, so **no new declaration was required** — confirmed by replicating Gate 6's rules against the
index before committing (0 violations), not by assumption.

**Gates: 0 / 0 / 3**, ctest 31/31 (Gate 9: 125 fork-NEW, 998 inherited).

## Lane 3 — `post-alpha/pipeline-hardening` (`87b9a5397`)

The package-upload `if-no-files-found: error` guard on all six package jobs, and a real scope for the
fork's own tooling under `tools/` (Gates 4/7/8 `--scope tools` + `tests/tools-sources.txt`, run in the
same gate rows as the product scopes).

**Conflicted (3, none in product code):**

* `.github/workflows/build.yml` — **semantic, not textual**: six hunks, each pairing the wave-R rename
  (ours: `path: build/zene-*`) against lane 3's guard (theirs: `path: build/lmms-*` plus the
  `if-no-files-found: error` block and its comment). Both changes are wanted and neither naive side is
  right: taking theirs reverts the rename, taking ours drops the guard on every release job. Resolved
  programmatically as *ours' `path:` line + theirs' remaining lines*, then asserted: 0 markers,
  6 guards, 6 `build/zene-*` globs, no `lmms-*` glob survived, YAML parses.
* `tests/all-sources.txt` — entry union, +3.
* `tests/upstream-modifications.txt` — 112 entries, **0 added** (the lane's 39 are a subset), 4 reason
  unions for the paths three lanes had each rewritten (the MIDI quartet). `src/gui/MainWindow.cpp`'s
  existing reason was already a superset of the lane's.

**Carry-forward the lane measured and deliberately did not apply — applied here, in this commit:**
three tooling files the fork added after the lane measured its baselines had no home in any scope list
(`tools/mmpz-git/demo_check.py`, `tools/mmpz-git/depth-demo.sh`, `tools/mmpz-git/render-recipe.sh`).
Registered in `tests/tools-sources.txt` (**8 → 11 files**). They deliberately **keep** their
`tests/upstream-modifications.txt` entries as well, because Gate 6 does not read `tools-sources.txt`
(it honours `fork-sources.txt` or the ledger), so dropping the ledger entry would turn a true statement
into a Gate 6 violation; the header of `tools-sources.txt` records that two-home wart.

That registration made both `tools` baselines stale, because the post-alpha/mmpz-git-depth lane had
already deepened the tool on integration (`mmpz_git.py` **818 → 1896** lines, plus a new 855-line test
suite) — the lane's baselines were measured pre-depth. Both ratchets were red on the merged tree for
reasons that predate the merge, and both were refreshed through the gate's own documented valve, never
by hand and never by trimming code (`docs/CONVENTIONS.md` rule 4):

```
bash tests/file-length-gate.sh --reanchor "<reason>" --scope tools   -> EXIT=0
  RE-ANCHORED: 2 file(s) over 500 lines (mmpz_git.py 1896 was 818; test_mmpz_git.py 855 new)
bash tests/complexity-gate.sh  --reanchor "<reason>" --scope tools   -> EXIT=0
  RE-ANCHORED: 13 over-target function(s), incl. main@demo_check.py CCN 20
```

Measured before the fix: file-length 2 regressions, complexity 6. After: `--check --scope tools` is
EXIT=0 for both. The `fork` and `all` baselines are untouched, and the previous top complexity entry
(`merge_elem` CCN 53) correctly drops out because mmpz-git-depth split that function — it now measures
**CCN 2**. Reasons recorded in `tests/QA-GATES.md` (tooling-scope block) **and** in the commit message.

**Gates.** First `run-all-gates.sh`: gate 1 **FAIL** on `PdcMixerTest`
(`QFATAL … QThread: Destroyed while thread is still running` in `cleanupTestCase()`; all 10 test
functions had passed). This is a pre-existing teardown flake — see below. Re-run: **0 / 0 / 3**,
ctest 31/31. Gate 9 now reports **11 tooling** files, 0 stale.

## Lane 4 — `post-alpha/stem-export` (`a4fe66c4f`)

Headless `exportstems <project> -o <dir> [--tail-bars N]`, the aligned-length fix (every stem renders
to the whole-project length) and the filename-sanitisation fix.

**Conflicted (2, one in product code):**

* `src/core/main.cpp` — two hunks, both semantic:
  * includes: ours' `<QDesktopServices>` (Lua console) and the lane's `<QDir>` are independent → **both kept**;
  * the `--help` block: ours' renamed wording (`If -e is specified zene exits…`) plus the lane's widened
    option line (`Options for "render", "rendertracks" and "exportstems":`). Taking theirs reverts a
    user-visible rename; taking ours drops the new action from `--help`. Resolved as the union, then
    asserted (0 markers, both includes, rename kept, `exportstems` kept, and no stale `lmms` product
    string introduced — the only remaining `lmms` occurrences are the `lmms::` namespace, the
    `LMMS_*` macros and the GPL header).
* `tests/upstream-modifications.txt` — 114 entries (112 + 2), 0 duplicates, no markers, all entries from
  both sides: the lane's two `RenderManager` entries added, and three reasons extended by superset
  (`include/Song.h`, `src/core/Song.cpp`) or clause union (`src/core/main.cpp`, which now carries
  crash-reporter, Lua-console, wave-R and stem-export clauses).

**Checklist.** (a) the lane registered its three new sources in `tests/fork-sources.txt`; its two test
sources were added here to `tests/all-sources.txt` (whole-tree scope, the treatment earlier lanes' test
files got). (b) the lane declared both `RenderManager` files itself (both upstream-inherited and
previously unchanged since the gate base) and extended `Song.h`/`Song.cpp`/`main.cpp` itself, so no
reason had to be authored here.

**Gates: 0 / 0 / 3**, ctest 32/32. Build first: the merge changed product code, so `cmake --build .`
was run before the gate suite (`EXIT=0`, 0 errors) — `run-all-gates.sh` does **not** build, so a green
ctest without a build would have tested stale binaries.

## Lane 5 — `post-alpha/lufs-wire` (`1ef366607`)

An EBU R128 loudness report on the render path: a passive tap in `ProjectRenderer`, the `.loudness.txt`
sidecar and console line, `OutputSettings` carrying the request, the export dialog's checkbox and
result label, and the CLI's `--loudness-report`.

**Conflicted (2, both ledgers — no product-code conflict):**

* `tests/all-sources.txt` — entry union, +1 (`LoudnessReportTest.cpp`).
* `tests/upstream-modifications.txt` — 119 entries (114 + 5), 0 duplicates, no markers, all entries from
  both sides: five added and three reasons extended by clause union for the paths lane 4 had just
  touched. `src/core/main.cpp` now carries **five** clauses.

**Auto-merged but not assumed:** `include/RenderManager.h`, `src/core/RenderManager.cpp`,
`src/core/main.cpp` and `tests/CMakeLists.txt` were touched by both this lane and the lane merged
immediately before it, so the merged content was read back and checked for both lanes' symbols
(`exportstems` + `--loudness-report` + `StemExportOptions` + `LoudnessReport` in `main.cpp`; the
stem-export API **and** the `loudnessReport*`/`loudnessReportReady` surface in `RenderManager`;
`LoudnessReportTest` + `StemExportTest` in `tests/CMakeLists.txt`). Lane 5's surface is `QString`-based,
so the absence of the `LoudnessReport` type in `RenderManager.h` is correct, not a lost hunk.

**Checklist.** (a) the lane registered its two new headers/sources in `tests/fork-sources.txt`; its test
source was added here to `tests/all-sources.txt`. (b) all eight inherited files the lane touched were
declared by the lane itself, reasons read off its own diff.

**Gates: 0 / 0 / 3**, ctest 33/33 (build first, `EXIT=0`).

## Lane 6 — `post-alpha/midi-race` (`36d676919`) — merged after lane 5 on purpose

The MIDI-learn race fix (a learn binding is built on the GUI thread, not the MIDI input thread) plus
its headless proofs.

**The cherry-picks, verified rather than assumed.** Lane 6's history is five commits; three of them
(`e4fc8cd7e`, `e77fa7b4b`, `99724e970`, all `test(gates): …`) are cherry-copies of commits **already in
this branch**. Each commit reachable from the merge was hashed with `git show <c> | git patch-id
--stable` and compared against the patch-ids of all 8,752 commits reachable from integration: they
match `879e251efd`, `9763a185c3`, `d96db9e4cf` exactly. Only `838e38c22` (the fix) and `48f705dbf`
(docs) carry new content, which is why lane 5 was landed first.

**Conflicted (5, none in product code):**

* `tests/fork-sources-gate.sh` (add/add), `tests/run-all-gates.sh`, `.github/workflows/quality-gates.yml`
  — against the cherry-copied gate work. Resolved by **keeping one copy**: ours is a strict superset
  (three scope manifests including `tools-sources.txt`; the `--scope tools` steps inside gates 4/7/8's
  rows; the exit-3 skipped-gate block; the Gate 9 invocation at `run-all-gates.sh:161`). Verified, not
  assumed: `git diff :2: :3:` on `quality-gates.yml` showed **zero** theirs-only lines, and on both
  shell scripts every theirs-only line is an older rewrite of a line ours has in newer form; 3
  `--scope tools` occurrences survive in each file after the merge, and `bash -n` / YAML parse pass.
  Because ours wins, these three files are byte-identical to integration's and carry **no change** in
  this commit.
* `tests/all-sources.txt` — entry union, +2 (`MidiLearnThreadTest.cpp`, `MidiLearnGuiTest.cpp`), 1115 entries.
* `tests/upstream-modifications.txt` — entry union first (119 entries, 0 added, 0 duplicates, no
  markers, every entry from both sides present), then the five shared paths **deduped**: three lanes
  had each rewritten the *same* MIDI-learn divergence there, so the union held three descriptions of one
  change. Per "keep one copy", the newest (this lane's) copy was kept for `include/MainWindow.h`,
  `include/MidiController.h`, `src/core/midi/MidiAlsaSeq.cpp` and `src/core/midi/MidiClient.cpp`,
  together with every clause describing a *different* change in the same file (`MainWindow.h` keeps its
  plugin-scan clause; `src/gui/MainWindow.cpp` keeps all five clauses, having had only one MIDI clause).
  Each kept segment is asserted to be one of the union's own segments — the text is selected from the
  ledger, never authored. Result: 119 entries, 0 duplicates, 0 blank reasons.

**Checklist.** (a) the lanes' new sources are registered (this merge adds its two tests to
`tests/all-sources.txt`). (b) the five inherited files it touches were declared — by the midi-learn
lane originally, already on integration, with the reasons kept as above.

**Gates: 0 / 0 / 3**, ctest 35/35 (build first, `EXIT=0`).

## Lane 7 — `post-alpha/clip-slice0` (`5c6c9379c`)

The clip model's Slice 0+1: the authored source window, the read-only playback path, and its persistence.

**Conflicted (3, none in product code):**

* `tests/fork-sources.txt` — entry union: `+include/SampleWindow.h` plus the lane's two test sources
  with its explanatory comment block kept in place (133 entries, 0 duplicates, no markers).
* `tests/upstream-modifications.txt` — entry union: +6 (the six inherited clip files), 125 entries,
  0 reason changes.
* `tests/CMakeLists.txt` — one hunk, resolved as an **entry union**: both
  `src/core/LoudnessReportTest.cpp` and `src/core/ClipSerialisationTest.cpp` are registered.

**Gate 7 was RED on this merge, exactly as forewarned, and it was reproduced before acting on it.**
`file-length-gate.sh --check` reported **one** regression:
`tests/src/tracks/SampleClipWindowTest.cpp` **511 lines** against the 500 target, which this merge is
what registers in `tests/fork-sources.txt` for the first time. The lane's own report claims "gate 7
PASS"; that does not reproduce on the merged tree — the other nine over-500 files were already
grandfathered (1169, 992, 880, 875, 714, 597, 549, 529, 526).

Route taken: the gate's **documented valve**, not a hand-edited baseline and not trimming
(`docs/CONVENTIONS.md` rule 4), taking the `src/core/CrashReporter.cpp` (526) trade made earlier the
same night one step further:

```
bash tests/file-length-gate.sh --reanchor "<reason>"    -> EXIT=0
  RE-ANCHORED: baseline rewritten from the current tree (10 file(s) over 500 lines)
  new entry:  tests/src/tracks/SampleClipWindowTest.cpp   511
```

The reason, recorded in `tests/QA-GATES.md` (Gate 7) **and** in the commit message: the file is **one
coherent QTest class** — twelve slots that all share a single nine-helper anonymous-namespace block
(`makeTone`/`makeStep`/`makeToneClip`/`makeStepClip`/`drainPlayHandles`/`trimIn`/`trimOut`/`asNumber`),
~45 of its lines are the mandatory GPL header and ~50 are the RED-test provenance comments naming the
defect the file exists to catch; splitting it to fit the count means extracting the shared helpers into
a new support header and adding a second test binary purely for the metric. Splitting was therefore
judged to be the metric-driven reshaping rule 4 warns about rather than the honest fix, and the
documented re-anchor was used instead. The nine pre-existing entries are unchanged and any *other* new
file over 500 still fails: `--check` is EXIT=0 afterwards.

**Gates: 0 / 0 / 3**, ctest 37/37. The re-anchor is a *fork-scope* baseline (`file-length-baseline.tsv`)
and touches no other scope.

## Lane 8 — `post-alpha/autosave` (`749d927b9`)

The recovery-prompt safety fix: a stale or other-project `recover.mmp` is no longer offered at start-up
(`ProjectRecovery` + the autosave identity sidecar).

**Conflicted (3, none in product code):** `tests/CMakeLists.txt` (entry union — `PluginScanCacheTest.cpp`
**and** `ProjectRecoveryTest.cpp`); `tests/fork-sources.txt` (entry union, +2 → 135 entries, 0 duplicates);
`tests/upstream-modifications.txt` (125 entries, 0 added, 0 duplicates; the lane's two reason extensions
kept as clause unions for `src/core/main.cpp` and `src/gui/MainWindow.cpp`).

**Checklist.** (a) the lane registered its two new product sources in `tests/fork-sources.txt` but its
test source `tests/src/core/ProjectRecoveryTest.cpp` was in **NO** scope list — registered here in
`tests/all-sources.txt` (whole-tree scope, the documented home for fork-authored tests; measured 372
lines, no function over CCN 10). (b) both inherited files it touched were declared by the lane itself.

**Gates: 0 / 0 / 3**, ctest 38/38 (build first, `EXIT=0`).

## Lane 9 — `post-alpha/automation-modes` (`89ab8d029`)

Read/Touch/Latch/Write automation modes: additive mode state on the model, a touch-gesture timeout, a
trim offset, the transport run token, and the mixer fader opening/closing a touch gesture.

**Conflicted (2, neither in product code):** `tests/CMakeLists.txt` (entry union —
`AutomationModesTest.cpp` added, the four entries earlier merges registered kept);
`tests/upstream-modifications.txt` — 128 entries (125 + 3), 0 duplicates, no markers, all entries from
both sides: three added (`include/AutomatableModel.h`, `src/core/AutomatableModel.cpp`,
`src/gui/widgets/Fader.cpp`) and `src/core/Song.cpp` extended by clause union (it now carries the
Session View, stem-export and automation-modes clauses).

**Checklist.** (a) the lane registered **no** new source in any scope list, so its test source
`tests/src/core/AutomationModesTest.cpp` was in none — registered here in `tests/all-sources.txt`.
*For the record: that file measures **652 lines**, i.e. it is over the 500-line target.* It is
deliberately not added to `tests/fork-sources.txt`: `tests/QA-GATES.md` documents that the fork
ratchets measure **product** sources and not test harnesses, and the whole-tree ratchet that would
measure it is already stale at this commit (five pre-existing over-500 entries). Fixing that needs a
deliberate, separate whole-tree re-anchor — which must not be smuggled into a feature merge, so it is
surfaced here instead. (b) the lane declared all four inherited files it touched itself.

**Gates: 0 / 0 / 3**, ctest 39/39 (build first, `EXIT=0`).

## Lane 10 — `post-alpha/midi-depth` (`0a92be489`)

Note probability + seeded velocity jitter (`prob`/`veljit` on the note model, the project `midiSeed`,
the optional XML attributes, the `InstrumentTrack` roll) and the note transform.

**Conflicted (3, none in product code):** `tests/CMakeLists.txt` (entry union —
`MidiProbabilityPersistenceTest.cpp` added, `MidiLearnTest.cpp` and `MidiLearnThreadTest.cpp` kept);
`tests/fork-sources.txt` (entry union, +4 → 139 entries, 0 duplicates, no markers);
`tests/upstream-modifications.txt` — 131 entries (128 + 3), 0 duplicates, no markers, all entries from
both sides: three added (`include/Note.h`, `src/core/Note.cpp`, `src/tracks/InstrumentTrack.cpp`), the
lane's own banner comment carried in place above its group, and `include/Song.h` / `src/core/Song.cpp`
extended by clause union.

**Checklist.** (a) the lane registered its four new product sources in `tests/fork-sources.txt`; its
three test sources were in none and are registered here in `tests/all-sources.txt` (290 / 270 / 459
lines, no function over CCN 10). Its `tests/data/midi-depth/*.mmp` fixtures and `*.py` generators are
outside every scope list by extension, like the other lanes' generated fixtures. (b) all five inherited
files it touched were declared by the lane itself.

**Gates: 0 / 0 / 3**, ctest ‡.

## Two pre-existing defects found (not caused by these merges)

1. **`PdcMixerTest` is flaky at teardown.** Lane 3's first full gate run went red on gate 1 with
   `QFATAL … QThread: Destroyed while thread is still running` in `cleanupTestCase()` — all ten test
   *functions* passed. Evidence that it is not a regression and not this merge: (i) the binary was
   unchanged between merge 1's run (31/31 PASS) and merge 3's (FAIL) — merge 3 touched only
   `.github/`, `docs/`, `tests/*.sh` and ledgers; (ii) three isolated re-runs of the same binary gave
   PASS, PASS, FAIL; (iii) the failure is in teardown, not in a measured behaviour. The full suite was
   re-run and exited 3 with 31/31.
2. **`run-all-gates.sh --whole-tree` is already red at this tree**, independent of these merges:
   `file-length-gate.sh --check --scope all` reports five regressions in files that predate this wave
   (`RemotePluginAudioPortsTest.cpp` 665 new; `ScriptEngineTest.cpp` 546→592;
   `PluginPortsHarness.h` 1156→1196; `PluginPortsMigrationTest.cpp` 632→731;
   `WasmSandboxTest.cpp` 948→1022). The `-all` baseline is stale. It was deliberately **not**
   refreshed: `--reanchor --scope all` rewrites the whole-tree baseline from the current tree, which
   would grandfather all five unreviewed regressions — a real weakening of a gate, which this task
   forbids. This also constrained lane 7's registration (see lane 7). The default gate run — what the
   three gates in this task measure — is unaffected.

## Not merged, and why

* Not in this task's list, or in flight / not verified: `router-live`, `warp`, `session-scheduler`,
  `mpe`, `telemetry`, `racks`, `instrument-hosting-impl`, `mastering`, `determinism`,
  `mixer-concurrency`, `recording-realtime`, `saveload-integrity`, `rename-complete`.
* Another session's work, never touched: `agent-control-surface`, `agent-surface-gate`, `cmd-notes`,
  `cmd-plugins`, `control-hardening`.
* `vst3-instrument-fixture`'s descendants beyond the two listed: not merged.

## Final verification of the finished tree

All ten lanes in, each with its own commit (`git log --oneline --merges c3d4d38fc..HEAD` above
returns exactly the nine merge commits in the table; lane 2 has none, by construction). Nothing was
pushed: `git status -sb` shows no upstream and no remote contains this branch.

```
cd build && cmake --build . -j4                                   -> EXIT=0
     100% built, 0 errors, 9.2 min under load average 25-44
cd build/tests && QT_QPA_PLATFORM=offscreen ctest --output-on-failure
                                                                  -> EXIT=0
     100% tests passed, 0 tests failed out of 42
bash tests/fork-sources-gate.sh                                   -> EXIT=0
     139 fork-sources entry(ies), 1011 all-sources (whole-tree),
     11 tools-sources (fork tooling), 0 stale entry(ies)
     PASS: every tracked source in scope is registered
bash tests/no-upstream-regression-gate.sh                         -> EXIT=0
     PASS: every change to upstream-inherited code since 01148947ea is declared
     (107 file(s) in the ledger — the ledger holds 131 entries)
bash tests/run-all-gates.sh                                       -> EXIT=3
     gate 1 ctest PASS · 2 coverage SKIP · 3 no-tautology PASS · 4 complexity PASS ·
     5 mutation PASS · 6 upstream-regression PASS · 7 file-length PASS ·
     8 duplication PASS · 9 fork-sources PASS
     RESULT: PASS-WITH-SKIPS (exit 3) — 1 of 9 gates did not run (coverage needs
     --with-coverage).  Exit 3 is the expected green result; it is not exit 0.
```

The tree is clean after the run (the mutation gate restores the file it mutates, verified) — the only
untracked path is this report before it is committed.

One real headless render, through the fork's own recipe (which also exercises the rename fix-up):

```
bash tools/mmpz-git/render-recipe.sh data/projects/shorties/sv-DnB-Startup.mmpz \
     -o /tmp/zene-merge-2/render.wav                              -> EXIT=0
renderer: build/zene (123,187,160 bytes, rebuilt for this run)
sha256  : 6b51f70fc32e993d7aaad3592e356954622ce842a0cbf1d6043962f185f24cb1
format  : 16-bit, 2 channel(s), 44100 Hz
frames  : 544,256              duration: 12.341 s
peak    : 0.946686 (-0.48 dBFS)     RMS: 0.167444 (-15.52 dBFS)
non-zero: 1,084,197 / 1,088,512 samples (99.60%)   -> NON-SILENT
```

The sha256 is **byte-identical** to the render the previous integration wave recorded for the same
project before these ten merges — ten more lane-merges changed the reference render by zero bytes,
which is the strongest available evidence that the ledger resolution and the registration work did not
alter the product's audio path.

## The ten lanes, as merged

1. `post-alpha/vst3-instrument-fixture` — `bc2c957ad` (carries `plugin-hosting-in-release`)
2. `post-alpha/plugin-hosting-in-release` — already up to date (same ref; see above)
3. `post-alpha/pipeline-hardening` — `87b9a5397`
4. `post-alpha/stem-export` — `a4fe66c4f`
5. `post-alpha/lufs-wire` — `1ef366607`
6. `post-alpha/midi-race` — `36d676919` (deliberately after 5)
7. `post-alpha/clip-slice0` — `5c6c9379c`
8. `post-alpha/autosave` — `749d927b9`
9. `post-alpha/automation-modes` — `89ab8d029`
10. `post-alpha/midi-depth` — `0a92be489`

