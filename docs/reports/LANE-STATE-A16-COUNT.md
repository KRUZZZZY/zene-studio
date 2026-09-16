# Lane report — board card #677: the A16 row count as a SELF-CHECKING derivation

**Lane** `030/a16-count`, worktree `zene-030/wa16`, branch cut from `d7a402041` (the wave-9 second-pass
integration train's record). **Wave 10.** Single-agent lane; nothing outside this tree was touched.

## 1 · What the card asked for, and what landed

| part | where it landed |
|---|---|
| (a) the derivation, computed from the table on every run, failing on drift | `tests/src/core/ReversibilityContractTest.cpp` — `theTableHistogramIsTheDocumentedOne()` re-derives the histogram from the live table and READS the one published figure out of the release notes on every run (path compiled in by `tests/CMakeLists.txt` as `A16_HISTOGRAM_DOC`). New invariants: declared rows == keyed entries (duplicates fail BY NAME) and Σ classes == rows. The hand-refreshed `documentedHistogram()` constant and its `#ifdef` additions are GONE. |
| (a) the same check outside ctest | `tools/dawproject-a16-histogram.cpp` — the probe prints `DECLARED rows=… entries=… duplicates=…`, names every command declared twice and exits **1** when that count is not 0, so `bash tools/dawproject-proof.sh` (part 2) fails rather than just reporting. |
| (b) the 24 duplicates | `src/core/ControlReversibilityTableLive.cpp` — retired (they were second declarations of rows `ControlReversibilityTablePassive.cpp` carries; the passive copy is the one the keyed map kept). The retirement note is in the file; `src/core/ControlReversibilityTable.cpp`'s stale header note is corrected to match. |
| (c) ONE published number + its command | `docs/RELEASE-NOTES-v0.3.0-alpha.md` §"The A16 contract table, and its histogram" — rewritten around **334 rows (158 `true_inverse` / 32 `snapshot` / 10 `irreversible` / 134 `not_mutating`)** with the measuring command, inside an `A16-HISTOGRAM-BEGIN/END` block that the ctest reads. KB article + `V0.3-EVIDENCE-MATRIX.md` block: §7. |
| (d) the other figures out of circulation | the release notes' 284/283/227/231/304/306 and per-lane delta narration is replaced by the one block plus a pointer to `docs/reports/MERGE-TRAIN-030w*.md`; `docs/A16-REVERSIBILITY.md`, `docs/SESSION-ARRANGEMENT-RECORD-MEASURED.md` and `docs/DAWPROJECT-INTERCHANGE.md` now say their figures are dated lane measurements and name where the release figure lives. |

## 2 · The measurement (both numbers measured, not computed)

Instrument: `bash tools/dawproject-proof.sh` part 2 (compiles the tree's own
`src/core/ControlReversibilityTable*.cpp` with the project's flags from `build/compile_commands.json`).

```
BEFORE (the tree as cut from d7a402041):
joined rows=264  snapshot rows=18  passive rows=76  stems rows=0
MEASURED rows=334 true_inverse=158 snapshot=32 irreversible=10 not_mutating=134

AFTER (the 24 duplications retired):
joined rows=240  snapshot rows=18  passive rows=76  stems rows=0
MEASURED rows=334 true_inverse=158 snapshot=32 irreversible=10 not_mutating=134
DECLARED rows=334 entries=334 duplicates=0
```

Same table, same class split, 24 fewer declarations: the retirement is behaviour-preserving for the
table, which is the property the derivation now makes safe to assert.

The 24 were `app.version`, `arrangement.get_state`, `audio.device_list`, `automation.get_state`,
`control.commands_list`, `control.ping`, `control.surface_report`, `control.transactions`,
`control.version`, `dsp.get_state`, `midi.device_list`, `mixer.get_state`, `plugin.list`,
`plugin.param_get`, `plugin.preset_list`, `project.get_state`, `roll.get_state`, `script.list`,
`settings.get`, `telemetry.status`, `track.get_state`, `track.list`, `transport.get_state`,
`warp.list` — every one `not_mutating` on BOTH sides, every one in `ControlReversibilityTableLive.cpp`
AND `ControlReversibilityTablePassive.cpp`, every one won by the passive copy (the constructor inserts
joined → snapshot → passive → stems). At the tip this lane was cut from, the count was a note in the
trains' reports ("233 rows = 233 registered, 0 losses/duplicates in count but 24 pre-existing
cross-file duplicates").

## 3 · The option rows are measurements too (the retired constant was wrong in its parts)

The published block declares what each build option adds, and each delta is a MEASUREMENT: the same
probe run five times, the telemetry header / the `LMMS_HAVE_WASM` define / the session-view option /
`WANT_STEM_SPLIT` moved in turn.

| configuration | rows | true_inverse | snapshot | irreversible | not_mutating | delta |
|---|---|---|---|---|---|---|
| **release** (telemetry, wasmtime, session in; stem out) | **334** | 158 | 32 | 10 | 134 | — |
| telemetry off | 332 | 158 | 32 | 10 | 132 | −2 `not_mutating` |
| wasmtime off | 326 | 158 | 29 | 10 | 129 | −3 `snapshot`, −5 `not_mutating` |
| session view off | 317 | 151 | 32 | 10 | 124 | −7 `true_inverse`, −10 `not_mutating` |
| stem engine on | 341 | 158 | 32 | 10 | 141 | +7 `not_mutating` |

The retired constant added the wasmtime group as "6 rows, 3 `snapshot`, 3 `not_mutating`" — true when the
`wasm.*` group was six rows, never re-taken after the host-chunking group added `wasm.pool` and
`wasm.render_offline` (measured delta **8**), with a base two rows high in the same direction: its total
agreed with the release figure while its decomposition did not. A parse simulation of the new
derivation over these five measurements reproduces all five figures exactly.

## 4 · What this lane PROVED (command, exit code)

All commands run in `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-030/wa16`.

**4.1 The histogram slot passes on the real binary** (build: `cmake -S . -B build
-DCMAKE_BUILD_TYPE=RelWithDebInfo -DWANT_QT6=ON -DUSE_WERROR=ON -DUSE_COMPILE_CACHE=ON -DWANT_CLAP=ON
-DLMMS_CLAP_PATH=…/vendor/clap-195b42a0
-DWASMTIME_ROOT=…/zene-030/third_party/wasmtime -DWANT_VST3=OFF` → `CONFIGURE_EXIT=0`; then
`cmake --build build --target ReversibilityContractTest -j2` → `BUILD_EXIT=0`, 556 compile units):

```
$ cd build/tests && QT_QPA_PLATFORM=offscreen LD_LIBRARY_PATH=…/zene-030/third_party/wasmtime/lib \
      ./ReversibilityContractTest theTableHistogramIsTheDocumentedOne
PASS   : ReversibilityContractTest::theTableHistogramIsTheDocumentedOne()
Totals: 3 passed, 0 failed, 0 skipped
SLOT_EXIT=0
```

**4.2 Doc drift is caught (no rebuild — the figure is read at runtime).** The published line was
perturbed `rows=334` → `rows=335` in the release notes only:

```
FAIL!  : …theTableHistogramIsTheDocumentedOne() … (the table measures 334 rows = 158 true_inverse +
32 snapshot + 10 irreversible + 134 not_mutating; …/docs/RELEASE-NOTES-v0.3.0-alpha.md publishes
335 rows = …, which for this build is 335 rows = … - re-measure it with `bash
tools/dawproject-proof.sh` (part 2) and re-take the published block in the same change)
DRIFT_DOC_EXIT=1
```

Reverted, re-run: `RERUN_EXIT=0` (3 passed).

**4.3 Table drift is caught.** One row added to
`src/core/ControlReversibilityTableHostChunking.cpp` (`a16.drift_demo`), rebuilt (1 TU + link,
`DRIFT_BUILD_EXIT=0`):

```
FAIL!  : … (the table measures 335 rows = 158 true_inverse + 32 snapshot + 10 irreversible +
135 not_mutating; … publishes 334 rows = …, which for this build is 334 rows = …)
DRIFT_ROW_EXIT=1
```

Reverted (`git diff` on that file empty), rebuilt (`RESTORE_BUILD_EXIT=0`).

**4.4 A duplicate declaration is refused by the measuring command.** With that row in place, a SECOND
declaration of it was added and the probe re-run (no rebuild needed):

```
DECLARED rows=336 entries=335 duplicates=1
  DECLARED TWICE: a16.drift_demo
PROBE_DUPLICATE_EXIT=1
```

**4.5 The five configuration measurements** of §3 (each `PROBE_EXIT=0`, `duplicates=0`).

**4.6 The gates this lane could move, run on the built tree:**

| gate | exit | note |
|---|---|---|
| 4 complexity (fork scope, `--check`) | 1 | **57 pre-existing regressions**, none in a file this lane touched. My first draft's `readPublishedFigure` was CCN 16 — reported by this gate — and is now split (CCN 9, all new functions ≤ 10). |
| 4 complexity (tools scope, `--check`) | **0** | clean, including the probe's new `declaredTwice` |
| 7 file-length (fork, `--check`) | 1 | 11 pre-existing regressions, none in a file this lane touched; the four files this lane edits are 460 / 132 / 499 / 223 lines (limit 500) |
| 8 duplication | **0** | `PASS: duplicated lines 0.50% (budget 5%)` |
| 9 fork-sources gate | **0** | `PASS: every tracked source in scope is registered (629 fork-NEW, 1103 inherited, 40 tooling)` |
| 9 all-sources reproduce | **0** | `REPRODUCES: the entry list in all-sources.txt is the recipe's own output.` |
| 6 no-upstream-regression | 1 | ONE violation, **pre-existing** (`docs/reports/VST3-INSTRUMENT-ROW78-EVIDENCE.log`, recorded in the wave-9 PASS2 report §6); every path this lane touched reports `docs (allowed)` / `tests (allowed)` / `fork-NEW (allowed)` |

## 5 · What does not compile / is not verified

- **The full test binary still aborts, on the KNOWN inherited defect** — not this lane's:
  `./ReversibilityContractTest` (all slots) passes `initTestCase`,
  `everyRegisteredCommandHasAContractRow`, `theTableHistogramIsTheDocumentedOne`,
  `theOnlyUnclassedMutatingCommandsAreTheRefusals`, `transactionRecordStatesItsBounds`,
  `coalescingIsDeclaredOnlyForCommandsWithALiveCheckpoint`, then dies with
  `Received signal 6 (SIGABRT)`, `what(): No writable member 'apiSurface'` in
  `irreversibleUndoFailsTypedAndDoesNotUndoAnOlderStep` (src/core/ScriptDawBindings.cpp:307). The
  registered ctest therefore reports `98 - ReversibilityContractTest (Subprocess aborted)`,
  `CTEST_EXIT=8` — the same abort the wave-9 second-pass train recorded at the tip this branch was cut
  from, so it is not a regression.
- **Not verified:** the wasmtime-off / session-off / stem-on runs are probe-level measurements of the
  TABLE (they compile the row files with those options moved); the *registry* side of those
  configurations (which ids exist) was not built or run — the published block's witness ids are the
  declared link between the two, and the ctest checks the witness in whatever build it runs in.
- **Not verified:** `tests/run-all-gates.sh` as a whole (gates 1, 2, 3, 5, 10, 11 are not in this
  lane's scope and the ctest row is red on the inherited abort anyway); the parent re-runs the suite on
  the merged tip. The individuals gate rows this lane CAN move are in §4.6 and none of them is newly
  red because of it.

## 6 · Hotspots

- `docs/RELEASE-NOTES-v0.3.0-alpha.md` — the A16 section is a merge point (a lane editing it will
  conflict textually); only the `A16-HISTOGRAM` block between the markers is load-bearing for the ctest.
- `tests/CMakeLists.txt` — one added `target_compile_definitions` block, for `ReversibilityContractTest`
  only; ledger reason appended to its line in the same commit.
- `src/core/ControlReversibilityTableLive.cpp` — a lane that re-adds a `not_mutating` reader row here
  will now FAIL the duplicate check rather than duplicating it silently (intended, and named in the
  retirement note).
- `tests/src/core/ReversibilityContractTest.cpp` — at **499 of the 500-line** file-length limit; the
  next lane that needs room here must split the A16 helpers out (or re-anchor, recorded).
- `tools/dawproject-proof.sh` (part 2) now exits non-zero on a duplicate declaration, so a lane that
  introduces one sees this script fail as well as the ctest.

## 7 · KB writeback

`ai_kos_create`, article type `note`, slug `zene-a16-histogram-published-figure-2026-09-16`: the one
number, the command, the five measured configurations, the retirement and the derivation.
The same block is appended to `…/lmms-fl-research/V0.3-EVIDENCE-MATRIX.md` for the workspace.

**One competing figure is left in the KB for the parent's pass:** `zene-control-surface-2026-09`'s
summary still says "The A16 table has 72 rows and the snapshot is missing 17" (its 2026-09-12 state
snapshot). Rewriting another article's summary is a KB-wide decision, not a lane's, so this lane names
it rather than editing it.

## 8 · The SINGLE next action

On the merged tip, run `bash tools/dawproject-proof.sh` (part 2) and, if any lane's merges moved the
table, re-take the `A16-HISTOGRAM` block in `docs/RELEASE-NOTES-v0.3.0-alpha.md` with the figures it
prints (its `MEASURED` line is the block's `measured:` line verbatim); the ctest fails by design until
that is done, naming both figures, so a merge that forgets it cannot ship a stale number. The rebuild
this lane's evidence came from is `cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DWANT_QT6=ON
-DUSE_WERROR=ON -DUSE_COMPILE_CACHE=ON -DWANT_CLAP=ON -DLMMS_CLAP_PATH=…/vendor/clap-195b42a0
-DWASMTIME_ROOT=…/zene-030/third_party/wasmtime -DWANT_VST3=OFF && cmake --build build --target
ReversibilityContractTest -j2` (~30 min at -j2 on this box), then `cd build/tests &&
./ReversibilityContractTest theTableHistogramIsTheDocumentedOne`.
