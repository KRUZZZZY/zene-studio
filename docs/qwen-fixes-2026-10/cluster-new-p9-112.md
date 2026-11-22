FIXED

# Cluster new-p9-112 — extreme tempo-map values are cast to `int` before validation

## Verdict and reachability

**FIXED** — the defect is real in the function, but the file-import path cannot reach it today.

- **Real (before the fix):** `addTempoPoints` rounded `point.value` and cast it to `int` with no finiteness
  or bounds check (`src/core/DawProjectSession.cpp:430` before the fix). A NaN, an infinity, or a finite
  value past `int`'s range makes that cast undefined behaviour. `applyDawProjectModel` takes a
  `DawProjectModel` from any in-process caller (`include/DawProjectInterchange.h:547`), so the cast was
  reachable from such a caller.
- **Not reachable from a file today:** every tempo automation point is checked at parse time by
  `tempoAccepted` (`src/core/DawProjectRead.cpp:77-82`, called at `:168` before the point is appended at
  `:169`). That check refuses NaN and anything outside `DawProjectMinTempo..DawProjectMaxTempo` (10..999,
  `include/DawProjectInterchange.h:137-138`). The import command reads with `readDawProject`
  (`src/core/ControlCommandsDawProject.cpp:520`) and only then calls `applyDawProjectModel` (`:539`). The
  undo redo at `:550` re-applies the same already-read model. The session-side model is built from
  `TempoMap` events, and `TempoMap::validEvent` refuses tempos outside 10..999 (`src/core/TempoMap.cpp:64-67`,
  `:97-101`). So the finding's trigger ("a file's tempo point reaches `addTempoPoints`") is already refused
  one step earlier.

## The fix (`src/core/DawProjectSession.cpp:430-445`)

The value is checked for finiteness and against the half-step interval whose values round into
`DawProjectMinTempo..DawProjectMaxTempo` **before** rounding or casting. This preserves the old
`floor(value + 0.5)` behavior: `9.5` still rounds to 10, `999.4` still rounds to 999, and `999.5` is
refused. The cast therefore only sees a finite rounded BPM in range. A refusal names the value, in the
same words the writer uses (`beat %1 carries %2 bpm, outside the engine's own %3..%4`).

Accepted set: unchanged. Before the fix, a value was accepted exactly when its rounded int landed in
10..999 (`TempoMap::addEvent` refused everything else). After the fix, the same values are accepted. Only
the refusal text changes, for values that were already refused. The import of any file is unchanged, since
the reader refuses these values first.

## Regression test

`tests/src/core/DawProjectInterchangeRoundTripTest.cpp:792` —
`tempoPointValuesOutsideTheEngineAreRefusedBeforeTheCast`. This is an existing file for this area (the
beat-refusal tests sit beside it); no new test file, so no registration change.

It is deterministic: fixed values, no clock, no sleep, no race. It asserts that `applyDawProjectModel`
refuses NaN, +inf, -inf, 1e300, -1e300, 5000, 9.4 and 999.5, that each refusal names the value, and
that the tempo map is left empty. It also asserts that 9.5 (which rounds to 10) and 999.4 (which rounds
to 999) are still applied, to show both ends of the accepted set are unchanged.

## Evidence (exact commands, EXIT codes; logs in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-112.r1/logs/`)

| Step | Command (run in worktree / `build/tests`) | EXIT | Log |
| --- | --- | --- | --- |
| Build attempt 1 (sandbox) | `cmake --build build --target DawProjectInterchangeRoundTripTest -j4` | 2 | `build-1-red.log` |
| Build (RED, cause: ccache temp dir not writable from the sandbox) | `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp/ccache-new-p9-112 cmake --build build --target DawProjectInterchangeRoundTripTest -j4` | 0 | `build-2-red.log` |
| **RED** (test, fix not yet written) | `ctest -R '^DawProjectInterchangeRoundTripTest$' --output-on-failure` | **8** | `test-1-red.log` |
| Build (GREEN) | `cmake --build build --target zene DawProjectInterchangeRoundTripTest -j4` | 0 | `build-3-green.log` |
| **GREEN** (fix applied) | `ctest -R '^DawProjectInterchangeRoundTripTest$' --output-on-failure` | **0** | `test-2-green.log` |
| 4b: save fix | `git diff -- src/core/DawProjectSession.cpp > .../logs/fix.patch` | 0 | — |
| 4b: revert fix | `git apply -R .../logs/fix.patch` | 0 | — |
| 4b: build without fix | `cmake --build build --target zene DawProjectInterchangeRoundTripTest -j4` | 0 | `build-4-revert.log` |
| **4b RED** (without fix) | `ctest -R '^DawProjectInterchangeRoundTripTest$' --output-on-failure` | **8** | `test-3-proof-without-fix.log` |
| 4b: restore fix | `git apply .../logs/fix.patch` | 0 | — |
| Build with fix restored | `cmake --build build --target zene DawProjectInterchangeRoundTripTest -j4` | 0 | `build-5-restored.log` |
| **4b GREEN** (fix restored) | `ctest -V -R '^DawProjectInterchangeRoundTripTest$'` | **0** | `test-4-restored-fix.log` |
| Family build | `cmake --build build --target DawProjectDeflateImportTest -j4` | 0 | `build-6-family.log` |
| Family run | `ctest -R 'DawProject' --output-on-failure` | **0** (2/2 passed: #347, #348) | `test-5-family.log` |

Assertions seen:

- **RED** (`test-1-red.log`, and identically `test-3-proof-without-fix.log`):
  `FAIL! : ...tempoPointValuesOutsideTheEngineAreRefusedBeforeTheCast() 'error.contains(QString::number(value, 'g', 17))' returned FALSE. (the file's tempo automation does not fit the engine's map (at most 128 events, tempos 10..999))`
  at `DawProjectInterchangeRoundTripTest.cpp:814`. `Totals: 59 passed, 1 failed`. The NaN case is the first
  to fail. The old refusal does not name the value. The UB itself is not observable without a sanitizer
  (the old refusal and the new one both refuse), so the RED is the refusal text, and the UB is shown by
  reading the cast.
- **GREEN** (`test-4-restored-fix.log`): `347: Totals: 60 passed, 0 failed, 0 skipped`.
- **Family** (`test-5-family.log`): `100% tests passed out of 2`.

## Gates

**`feature_check.py` could not run from this sandbox.** It writes its logs to
`<worktree>/../<name>-feature-check`, which is `projects/zene-studio-program/zene-qwen-new-p9-112-feature-check`,
outside the write-jail. The run stopped with `PermissionError` at `feature_check.py:71` (`logs.mkdir`),
exit 1 (`gate-1.log`). I did not edit the shared script. Instead I ran each gate command the script runs,
from the worktree, with the same arguments and logs in the LOG FOLDER:

| Gate | Command | EXIT | Log |
| --- | --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 | `gate-fork-sources.log` |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 | `gate-all-sources-reproduce.log` |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 | `gate-no-upstream-regression.log` |
| check-namespace | `tests/scripted/check-namespace` | 0 | `gate-check-namespace.log` |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 | `gate-no-tautology.log` |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 | `gate-doc-figures.log` |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 | `gate-complexity.log` |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 | `gate-file-length.log` |

For the two ratchets the script fails only on `REGRESSION:` items in files the branch changed. Neither
changed file has one. `grep` over all `gate-*.log` for `DawProjectSession` and `DawProjectInterchangeRoundTrip`
matches only a complexity table row for the existing `applyDawProjectModel` (CCN 32, grandfathered in
`tests/complexity-baseline.tsv:6`). The other `REGRESSION:` lines are in files this change does not touch
(for example `plugins/Vst3Effect/Vst3Host.cpp`, `src/core/TakeLane.cpp`). Under the script's rule, both
rows are therefore PASS. The exit code of 1 comes from those untouched files.

## Independent QA verification

After the lane's run, QA rebuilt `zene` and `DawProjectInterchangeRoundTripTest` with
`cmake --build build --target zene DawProjectInterchangeRoundTripTest -j4` (EXIT 0), then ran
`ctest -R '^DawProjectInterchangeRoundTripTest$' --output-on-failure` from `build/tests` (EXIT 0,
1/1). `ctest -R 'DawProject' --output-on-failure` also passed 2/2 (EXIT 0).

QA reversed only the product hunk with
`git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-112.r1/logs/qa-product-reversal.patch`
(EXIT 0), rebuilt both targets (EXIT 0), and reran the focused test (EXIT 8). The regression failed at
`DawProjectInterchangeRoundTripTest.cpp:815`: the old generic map-capacity error did not name the NaN
value. `git checkout -- src/core/DawProjectSession.cpp` restored the staged product change (EXIT 0); QA
rebuilt both targets (EXIT 0) and reran the focused test (EXIT 0, 1/1).

QA then reran `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py
/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-112` (EXIT 0):
all six direct gates passed, and complexity and file-length reported no new item in changed files.

## Self-check

1. **Each new test FAILS with the product change reverted and PASSES restored.** Yes: RED exit 8 and GREEN
   exit 0, and the 4b proof repeats both (exit 8 without the fix, exit 0 with it). Exact commands are in the
   table above.
2. **Every acceptance criterion is proven by a test I ran.** The finding names one criterion: validate
   finiteness and tempo bounds before the cast. The test above proves it. It runs the refusal (exit 0 with the fix).
3. **Each edit is one undo step; command reversibility.** No command was added or changed. The import's undo
   is unchanged (`ControlCommandsDawProject.cpp:550` re-applies the same model). The commands snapshot is not affected.
4. **Nothing added runs on the audio thread.** `applyDawProjectModel` is the file/session import, not an audio
   path (no `process`/`play`/`render` call). The new code adds a `QStringLiteral` only on the refusal branch.
5. **Old projects still load and render as before.** Files are unaffected: the reader refuses these values
   before `addTempoPoints` (`DawProjectRead.cpp:168`). The accepted set in-process is unchanged (see above).
6. **Portability.** `std::isfinite` and `std::floor` come from `<cmath>`, already included
   (`DawProjectSession.cpp:63`). No new Qt API.
7. **Callers and siblings.** Only `applyDawProjectModel` calls `addTempoPoints`. Its sibling
   `addMeterPoints` (`DawProjectSession.cpp:461-477`) has no double→int cast on a point value (numerator and
   denominator are `int`), and its tick comes from `dawProjectTicksFromBeats`, which already refuses
   non-representable beats. Not changed. One related point I did NOT change:
   `song->setTempo(static_cast<bpm_t>(model.tempo))` (`DawProjectSession.cpp:504`) takes the global
   `double tempo` with no bounds check in `applyDawProjectModel`. That is a double→float cast, not a
   double→int cast, so it is not this UB. The reader guards it (`DawProjectRead.cpp:246`). It is a separate
   gap, outside this finding's scope.
8. **Registration.** No new test file (the test sits in an existing registered file), and no new product file.
   `tests/fork-sources.txt` and `tests/upstream-modifications.txt` need no change. `DawProjectSession.cpp` is
   already fork-authored.
9. **Determinism.** The test uses fixed literal inputs only (NaN, ±inf, ±1e300, 5000, 9.4, 999.5, 9.5, 999.4). It has no
   clock, sleep, thread or race, and no injected seam is needed, because `applyDawProjectModel` takes the
   model and the song as arguments.
10. **Verdict.** FIXED, so the ALREADY-FIXED / FALSE-POSITIVE evidence question does not apply. The reachability
   facts behind the verdict are cited above (`DawProjectRead.cpp:77-82,168`; `ControlCommandsDawProject.cpp:520,539`;
   `TempoMap.cpp:64-67,97-101`).
11. **`submit_check.py`.** Its only FAIL is the `gates (feature_check.py)` row. That script stops with a
   `PermissionError` before any gate runs (it writes logs to `projects/zene-studio-program/zene-qwen-new-p9-112-feature-check`,
   outside the write-jail). Run directly, the six simple gates exit 0. The two ratchets exit 1, but only on `REGRESSION:` lines for
   files this change does not touch (see the Gates section). This is not a gate pass from the script itself: the
   script needs to run from a place that can write its logs.
