# Cluster new-p16-146: frequency updates leave the dirty flag set

**Verdict: FIXED** - gate acceptance **BLOCKED** on the shared gate baseline (review round 1 returned REWORK for this reason; see "Review round 2" below). The fix and its regression are verified; the two direct `--check` gates exit 1 on the base commit too, and clearing them needs a baseline decision by the parent.

`NotePlayHandle::setFrequencyUpdate()` sets `m_frequencyNeedsUpdate`, and nothing ever cleared it. Once a note was marked, `play()` called `updateFrequency()` on every later period for the life of the note. `updateFrequency()` now spends the mark before it reads its inputs, so a note recomputes its frequency only when something marks it again.

## Evidence

- `src/core/NotePlayHandle.cpp:307` - `play()` tests the flag and calls `updateFrequency()` (the slide-glide clause is unchanged).
- `src/core/NotePlayHandle.cpp:620-624` - `updateFrequency()` clears `m_frequencyNeedsUpdate` before it reads `masterPitch`, `baseNote`, detuning, pitch, slide and MPE pitch. A mark set while the update runs survives to the next period.
- Before the change, the flag had no writer except `setFrequencyUpdate()` (`include/NotePlayHandle.h:336-339`) and no clear anywhere. The only reader is `NotePlayHandle.cpp:307`.
- Every caller of `updateFrequency()` now spends the mark: the constructor (`:117`), `play()` (`:309`), `processTimePos()` (`:715`) and the sub-note recursion (`:695`). The fix sits in the function, not in `play()`, so no caller is left out.
- Callers that mark (unchanged): `src/tracks/InstrumentTrack.cpp:440, 519, 803`, `src/tracks/InstrumentTrackNotes.cpp:92`, `include/ChordFollow.h:50`, `include/InstrumentTrack.h:135-139` (retune), `src/core/SessionTuning.cpp:315`.

### Regression test

`tests/src/core/MpePlaybackTest.cpp`, new slot `aFrequencyMarkIsSpentByItsUpdate()`:

1. `setFrequencyUpdate()` and one rendered block: the mark is applied.
2. `setMpePitchCents(1200)` with no mark, one more block: the frequency must stay the same (an unmarked input must not be picked up).
3. A new mark: the octave bend is applied on the next block (frequency doubles).

Step 2 fails before the fix: `261.626 Hz became 523.251 Hz`.

Registration: `tests/upstream-modifications.txt` (last line) for `src/core/NotePlayHandle.cpp`. `MpePlaybackTest.cpp` is an existing test file and was not re-registered.

## Commands and exit codes

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-146.r1/logs/`.

| # | Command (run from the worktree or `build/tests` as noted) | Log | EXIT | Result |
|---|---|---|---|---|
| 1 | `cmake --build build --target MpePlaybackTest -j4` | build-1.log | 2 | ccache could not write under `/run/user/1000`; no code compiled |
| 2 | `CCACHE_TEMPDIR=/tmp/zene-new-p16-146-ccache-tmp cmake --build build --target MpePlaybackTest -j4` (RED build) | build-2.log | 0 | built with the test, without the fix |
| 3 | `ctest -R '^MpePlaybackTest$' --output-on-failure` (from `build/tests`, **RED**) | test-red-1.log | 8 | 7 passed, 1 failed: `aFrequencyMarkIsSpentByItsUpdate` - `the spent mark recomputed the frequency: 261.626 Hz became 523.251 Hz` |
| 4 | `git diff -- src/core/NotePlayHandle.cpp > fix.patch` | - | 0 | fix saved to the logs folder |
| 5 | `git apply -R fix.patch` (step 4b, revert) | - | 0 | product file back to unfixed |
| 6 | `cmake --build build --target zene MpePlaybackTest -j4` (with `CCACHE_TEMPDIR`) | build-4.log | 0 | |
| 7 | `ctest -R '^MpePlaybackTest$' -V` (unfixed, step 4b) | test-4b-unfixed.log | 8 | same FAIL as row 3 |
| 8 | `git apply fix.patch` (re-apply) | - | 0 | |
| 9 | `cmake --build build --target zene MpePlaybackTest -j4` (with `CCACHE_TEMPDIR`) | build-5.log | 0 | |
| 10 | `ctest -R '^MpePlaybackTest$' -V` (**GREEN**, step 4b) | test-4b-fixed.log | 0 | `PASS : MpePlaybackTest::aFrequencyMarkIsSpentByItsUpdate()`; 100% passed |
| 11 | `cmake --build build --target zene MpePlaybackTest -j4` (after the final edit) | build-3.log | 0 | |
| 12 | `ctest -R '^MpePlaybackTest$'` (GREEN) | test-green-1.log | 0 | 1/1 passed |

### Related family (regression check of the behaviour change)

The fix changes when a note recomputes, so I ran the other tests that cover note pitch, MPE, slides, tuning and articulation.

| # | Command | Log | EXIT | Result |
|---|---|---|---|---|
| 13 | build of the family targets, first attempt (`MpeExpressionTest MpeInputPathTest MpeNoteStorageTest SessionTuningTest SlideNotesTest ClipTransposeTest NotePlaybackTest ExprSynthTest ArticulationPlaybackTest MpePlaybackTest`) | build-6.log | 2 | `No rule to make target 'ExprSynthTest'`; the CMake target is `ExprSynth` |
| 14 | `cmake --build build --target InstrumentLayerZonePublishTest AudioFileProcessorPlayheadTest ...` | build-7.log | 0 | |
| 15 | `cmake --build build --target ExprSynth` | build-8.log | 0 | |
| 16 | `cmake --build build --target ExprSynthAllocation` | build-9.log | 2 | no such target; the target is `ExprSynthAllocationTest` |
| 17 | `cmake --build build --target ExprSynthAllocationTest` | build-10.log | 0 | |
| 18 | `ctest -R '^(...family...)$'` (first run) | test-family-1.log | 8 | 2 failures from missing plugins, see below |
| 19 | `cmake --build build --target monstro audiofileprocessor` (with `env -u MAKEFLAGS`) | build-12.log | 0 | plugins built |
| 20 | `ctest -R '^(NotePlaybackTest\|AudioFileProcessorPlayheadTest)$' -V` | test-family-4.log | 0 | 23/23 and 3/3 pass |
| 21 | `ctest -R '^(SessionTuningTest\|MpeExpressionTest\|MpeInputPathTest\|MpeNoteStorageTest\|MpePlaybackTest\|ClipTransposeTest\|SlideNotesTest\|NotePlaybackTest\|ArticulationPlaybackTest\|ExprSynth\|ExprSynthAllocation\|InstrumentLayerZonePublishTest\|AudioFileProcessorPlayheadTest)$' --output-on-failure` | test-final-family.log | 0 | **13 tests executed, 13 passed, 100%** |

Rows 18-20 are environmental. The `monstro` and `audiofileprocessor` plugins were not built in the targeted tree (`Plugin not found`, as the QWARN lines in test-family-1.log show). Building them and rerunning passes (row 20). No product code is involved in that failure.

Affected-family test count for the final run (row 21): 13 executed, 13 passed. The regression lives in `MpePlaybackTest` (ctest family of 1, plus the new slot).

### Gates (step 5)

- `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (row 22): **EXIT=1**. The script creates its log directory beside the worktree (`…/zene-qwen-new-p16-146-feature-check`), which is outside the write jail (`PermissionError`, `gates-1.log`). I did not edit the script.
- The same gate functions, command lists and pass criteria were run with `/tmp/zene-new-p16-146-gates/run_gates.py` (it imports `feature_check.py` unchanged and only redirects the per-gate logs to `logs/gates-1/`): **EXIT=0, RESULT: PASS**. Rows: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures all PASS (exit 0); complexity and file-length PASS ("no new item in your files"). Summary in `gates-1-summary.log`.

## Review round 2 (REWORK: direct gates red)

Review `new-p16-146.r1.md`: the fix and regression check out; `bash tests/complexity-gate.sh --check` and `bash tests/file-length-gate.sh --check` both exit 1, and the review asked for green direct gates before resubmitting. Logs in `runs/new-p16-146.r2/logs/`.

| Command (worktree root unless noted) | Log | EXIT | Result |
|---|---|---|---|
| `bash tests/complexity-gate.sh --check` | complexity-gate-2.log | 1 | 6 `REGRESSION` lines, 6 unique items |
| `bash tests/file-length-gate.sh --check` | file-length-gate-2.log | 1 | 15 `REGRESSION` lines, 14 unique files (ControlShutdownHookTest.cpp listed twice) |
| `python3 -I /tmp/zene-new-p16-146-gates/run_gates.py <worktree> <logs>/gates-3` (same gate functions as `feature_check.py`, logs redirected) | gates-3-summary.log | 0 | all 8 gates PASS; complexity and file-length PASS ("no new item in your files") |

**Why the direct gates fail, and why it is not this change.** The direct gates compare against the committed `tests/file-length-baseline.tsv` (7 lines on this branch, the same file as on the base commit `2da2ed356`), and that baseline does not list the current violations. None of the violating items is in a file this branch touches (`src/core/NotePlayHandle.cpp`, `tests/src/core/MpePlaybackTest.cpp`, `tests/upstream-modifications.txt`, this report). Evidence:

- The cached base listing `gate-baselines/2da2ed356e1bde5fb67f46ed5157d186f0adb95c.json` (computed by the shared tooling on the base code) already contains the same items: 6 complexity and 14 file-length keys. Comparing the keys from `complexity-gate-2.log` and `file-length-gate-2.log` against it gives **0 keys not in base** and **0 violations in the changed files**.
- The complexity run also prints `improved:` lines for baseline entries that are no longer over the limit (for example `registerFollowGetState`), which shows the committed baseline is stale on the base commit.

So the direct gates are already red on the base commit with these exact items. I did not re-anchor the baselines (`--reanchor` is forbidden for this fix), and I did not edit the unrelated violating files, as the review asked. **Decision needed from the parent:** either re-anchor the complexity and file-length baselines on the base as its own recorded change, or have the train clear the 20 listed files. Until one of those happens, the direct `--check` commands cannot exit 0 from this branch.

Tests rerun for round 2 (after `CCACHE_TEMPDIR=/tmp/zene-new-p16-146-ccache-tmp cmake --build build --target zene MpePlaybackTest -j4`, EXIT=0, `build-r2-1.log`):

- `ctest -R '^MpePlaybackTest$' -V` from `build/tests`: EXIT=0, `PASS aFrequencyMarkIsSpentByItsUpdate()`, 1/1 (`test-r2-mpe.log`).
- Family of 13 from `build/tests`: EXIT=0, 13/13 passed (`test-r2-family.log`).

Round 2 product code is unchanged from round 1; the commit adds only this report section.

## Behaviour consequence (reviewer decision)

`masterPitch` is read inside `updateFrequency()` (`src/core/NotePlayHandle.cpp:623`), and its change does not mark notes: the `dataChanged` connection in `src/core/Song.cpp:194-195` is commented out. Before this fix, a note that had been marked once recomputed every period, so it also followed master-pitch automation from then on. After the fix it follows master-pitch changes only when it is marked again, like a note that was never marked. This is by reading the code; I did not add a test for it. The consistent behaviour (master-pitch changes marking the handles) would be a separate change, and I did not make it here.

## Self-check

`submit_check.py new-p16-146` (log `submit-check-1.log`, EXIT=1):

- PASS commits on the branch; PASS working tree clean; PASS report present; PASS tests added or changed (`MpePlaybackTest.cpp`).
- **FAIL gates (feature_check.py)** - an environment fault, not a gate result. `feature_check.py` creates its log directory at `<worktree>-feature-check` (`feature_check.py:71`), outside the write jail, and fails with `PermissionError` (`logs/gates-1.log`). I did not edit the shared script. The same gate functions, command lists and pass rule, run through `/tmp/zene-new-p16-146-gates/run_gates.py` with logs in the LOG FOLDER, give **EXIT=0, RESULT: PASS** on the committed tree (`logs/gates-2-summary.log`, 4 changed files; complexity and file-length PASS). The shared gate needs to be run from a location the jail allows, or its log directory made configurable, to close this item. Separately, the direct `--check` gates exit 1 on the base commit too (see "Review round 2"); that is the open gate blocker.
- ? Regression fails reverted and passes with the change: **yes**. RED with the fix reverted: `test-red-1.log` EXIT=8 (`261.626 Hz became 523.251 Hz`); the step 4b revert run: `test-4b-unfixed.log` EXIT=8; with the fix re-applied: `test-4b-fixed.log` EXIT=0 (`PASS aFrequencyMarkIsSpentByItsUpdate`). Commands and EXIT codes are in the table above.
- ? Deterministic: **yes**. No wall-clock time, sleeps or threads. The test uses a standalone `NotePlayHandle` on a fixed key and a fixed block size (`m_frames`) rendered through the same `renderBlock()` helper that `MpePlaybackTest` already uses. The one input it changes is an MPE pitch bend of exactly 1200 cents (ratio 2), set directly with `setMpePitchCents()`. The assertions compare floats against the value the handle itself produced, with tolerances `1e-3f` and `1e-2f`.
- ? Fix at the cause and complete: **yes**. The clear is in `updateFrequency()` itself, so all callers are covered: constructor (`src/core/NotePlayHandle.cpp:117`), `play()` (`:309`), `processTimePos()` (`:715`), sub-note recursion (`:695`). The clear runs before the inputs are read, so a mark that lands during an update survives to the next period (plain bool, same as the existing design; no lock added). Marks that set the flag are unchanged: `InstrumentTrack.cpp:440, 519, 803`, `InstrumentTrackNotes.cpp:92`, `ChordFollow.h:50`, `InstrumentTrack.h:135-139`, `SessionTuning.cpp:315`. Their tests pass (`SessionTuningTest` 1/13, `ClipTransposeTest`, `MpeExpressionTest`, `MpeInputPathTest`, `MpeNoteStorageTest`, `SlideNotesTest`, `NotePlaybackTest` 23/23, `ArticulationPlaybackTest`, `ExprSynth`, `ExprSynthAllocation`, `InstrumentLayerZonePublishTest`, `AudioFileProcessorPlayheadTest` 3/3: 13/13 in `test-final-family.log`).
- ? No collateral damage: **mostly, with one behaviour consequence to review.** Save and load are untouched, and no limit or check was added. The consequence: `masterPitch` is read inside `updateFrequency()` (`NotePlayHandle.cpp:623`) and its change does not mark notes (`Song.cpp:194-195`, the connection is commented out). A note that had been marked before this fix kept recomputing and followed master-pitch changes; after it, it follows them only when marked again. This is by reading only, not tested. A decision is needed on whether master-pitch changes should mark handles; I did not make that change.
- Realtime: the change adds one bool store to a path that already runs; no allocation, lock, log or allocating Qt call is added. Undo: no command is added or changed. Verdict is a real defect, so no ALREADY-FIXED or FALSE-POSITIVE proof is needed.
- Environment notes: the first targeted build failed under ccache (`/run/user/1000` not writable), so `CCACHE_TEMPDIR=/tmp/zene-new-p16-146-ccache-tmp` was used. The `monstro` and `audiofileprocessor` plugins were not built in this targeted tree and failed their tests with `Plugin not found`; building them made both pass (rows 18-20). `ExprSynthTest` and `ExprSynthAllocation` are not CMake target names here (`ExprSynth` and `ExprSynthAllocationTest`).
