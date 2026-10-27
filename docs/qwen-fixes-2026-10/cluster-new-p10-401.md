**Verdict: FIXED**

# Cluster new-p10-401: changing step length can divide by zero

Changing the step length while a step is in progress divided the current step length by the old step length. A zero old length (a step started with zero length) made that a division by zero, which crashed the process with `SIGFPE` (`FPE_INTDIV`) when the test reproduces it. The fix skips the rescale when the old step length is zero; with a zero old length the in-progress step is already zero length, so there is nothing to scale.

## File evidence

- [StepRecorder.cpp](../../src/core/StepRecorder.cpp#L173): `setStepsLength()` is the only division by `m_stepsLength` in the file (`stepForwards()` and `stepBackwards()` only add and subtract it).
- [StepRecorder.cpp](../../src/core/StepRecorder.cpp#L178): the rescale now runs only when `m_stepsLength != 0`. Negative lengths keep the old behaviour.
- [StepRecorder.cpp](../../src/core/StepRecorder.cpp#L66): `start()` calls `setStepsLength(stepLength)` first, so the fix covers the start path too.
- [PianoRoll.cpp](../../src/gui/editors/PianoRoll.cpp#L5109): the note-length change calls `setStepsLength(newNoteLen())`; `newNoteLen()` returns `m_lenOfNewNotes`, which is copied from `note->length()` (PianoRoll.cpp#L3183, #L3279), so a zero-length note makes the zero length reachable.
- [PianoRollBenchmarkTest.cpp](../../tests/src/gui/PianoRollBenchmarkTest.cpp#L196): `changingLengthOfZeroLengthStepIsSafe` is the regression for the finding's path (zero length, note pressed, length changed). It also changes the length back to zero.
- [PianoRollBenchmarkTest.cpp](../../tests/src/gui/PianoRollBenchmarkTest.cpp#L214): `rescalingInProgressStepKeepsStepCount` is the positive-length control: two 192-tick steps rescaled to 96 ticks stay two steps (192 ticks). It passes before and after the fix.
- [tests/upstream-modifications.txt](../../tests/upstream-modifications.txt): registers the two changed inherited files with this cluster's reason. The test file was already in CMake (`tests/CMakeLists.txt`, PianoRollBenchmarkTest); no new file was added.

Harness note: `start()` cannot run in this headless test binary. Its hint goes through `StepRecorderWidget::showHint()` → `TextFloat`, whose constructor calls `getGUI()->mainWindow()`, and `getGUI()` is null here. My first RED attempt crashed there (SIGSEGV in `TextFloat::TextFloat`, `test-1-red.log`), which is not the bug. The regression therefore calls `setStepsLength()` directly, which is the first thing `start()` does (StepRecorder.cpp#L66). The sequence otherwise matches the finding.

## Why the zero case is safe

With `m_stepsLength == 0`, every `stepForwards()` added zero, and `start()` and `prepareNewStep()` reset `m_curStepLength` to zero. A non-zero current length only comes from a non-zero step length, so `m_curStepLength` is zero whenever `m_stepsLength` is zero. The rescale result would therefore have been zero anyway; the fix only avoids dividing by zero to reach it.

## RED/GREEN evidence

The affected CTest family is `PianoRollBenchmarkTest`: **1 CTest test, 8 QTest slots** (`initTestCase`, `destructionReleasesPendingStepNotes`, `changingLengthOfZeroLengthStepIsSafe`, `rescalingInProgressStepKeepsStepCount`, `invalidKeysAreSafe`, `invalidKeysAreNotPressed`, `frameTimeAtFourSizes`, `cleanupTestCase`). Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-401.r1/logs/`.

| Check | Exact command | Exit |
|---|---|---:|
| RED, original code (test built) | `cmake --build build --target PianoRollBenchmarkTest -j4 > .../build-2-red.log 2>&1; echo EXIT=$?` (with `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp/ccache-tmp`) | 0 |
| RED, original code, ctest | `cd build/tests && ctest -R '^PianoRollBenchmarkTest$' --output-on-failure > .../test-2-red.log 2>&1; echo EXIT=$?` | **8** |

The RED assertion is the crash, not a QTest assertion: `Received signal 8 (SIGFPE), code FPE_INTDIV`, with the top frame `lmms::StepRecorder::setStepsLength(lmms::TimePos const&)` (test-2-red.log, lines 9 and 227). `0% tests passed, 1 tests failed out of 1`.

| Check | Exact command | Exit |
|---|---|---:|
| Fixed build | `cmake --build build --target PianoRollBenchmarkTest -j4 > .../build-4-green.log 2>&1; echo EXIT=$?` | 0 |
| GREEN, ctest | `cd build/tests && ctest -R '^PianoRollBenchmarkTest$' --output-on-failure > .../test-3-green.log 2>&1; echo EXIT=$?` | 0 |
| GREEN, verbose per-slot | `cd build/tests && ctest -V -R '^PianoRollBenchmarkTest$' > .../test-4-green-verbose.log 2>&1; echo EXIT=$?` | 0 |

GREEN result (test-4-green-verbose.log, lines 22–49): `PASS` for all 8 slots, `Totals: 8 passed, 0 failed, 0 skipped`, `100% tests passed out of 1`.

### Step 4b: reverse-patch proof

| Step | Exact command | Exit |
|---|---|---:|
| Save product diff | `git diff -- src/core/StepRecorder.cpp > .../logs/fix.patch` | 0 |
| Reverse the fix | `git apply -R .../logs/fix.patch` | 0 |
| Rebuild reverted | `cmake --build build --target PianoRollBenchmarkTest -j4 > .../build-5-4b-reverted.log 2>&1; echo EXIT=$?` | 0 |
| Run reverted (must FAIL) | `cd build/tests && ctest -R '^PianoRollBenchmarkTest$' --output-on-failure > .../test-5-4b-reverted.log 2>&1; echo EXIT=$?` | **8** |
| Control on reverted build | `cd build/tests && ./PianoRollBenchmarkTest rescalingInProgressStepKeepsStepCount > .../test-6-control-reverted.log 2>&1; echo EXIT=$?` | 0 |
| Re-apply fix | `git apply .../logs/fix.patch` | 0 |
| Rebuild fixed | `cmake --build build --target PianoRollBenchmarkTest -j4 > .../build-6-4b-reapplied.log 2>&1; echo EXIT=$?` | 0 |
| Run fixed (must PASS) | `cd build/tests && ctest -V -R '^PianoRollBenchmarkTest$' > .../test-7-4b-reapplied.log 2>&1; echo EXIT=$?` | 0 |

The reverted build fails with the same `SIGFPE` in `setStepsLength` (test-5-4b-reverted.log, line 9 and frame 227). The positive-length control passes on the reverted build (`Totals: 3 passed`, test-6-control-reverted.log); it does not reach the crash because it runs alone. With the fix re-applied, the family passes 8/8.

The `zene` app target also builds with the fix: `cmake --build build --target zene -j4 > .../build-7-zene.log 2>&1; echo EXIT=$?` → 0.

## Build and environment notes

- The first build attempt failed with `ccache: error: failed to create temporary file ... Permission denied` under `zene-bunny-fix/dsh/home/ccache-tmp-r6` (`build-1-red.log`, EXIT=2). No code was involved. Every later build set `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp/ccache-tmp` and succeeded.
- Only `PianoRollBenchmarkTest` (and `zene`) were built. `StepRecorderWidgetTest` and `NoteEditorTest` were not rebuilt or run; neither calls the rescale path changed here.

## Gates

`feature_check.py` could not run in this session: it creates its log folder at `projects/zene-studio-program/zene-qwen-new-p10-401-feature-check`, which is outside the worktree and not writable from it. The traceback is `PermissionError: [Errno 13] ... zene-qwen-new-p10-401-feature-check` (`gate-feature-1.log`, EXIT=1). The gate scripts were not edited. Instead I ran each command that `feature_check.py` lists, in the worktree, with the same results it would produce:

| Gate | Exact command | Exit |
|---|---|---:|
| fork-sources | `bash tests/fork-sources-gate.sh > .../gate-fork-sources.log 2>&1` | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh > .../gate-all-sources-reproduce.log 2>&1` | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh > .../gate-no-upstream-regression.log 2>&1` | 0 |
| check-namespace | `tests/scripted/check-namespace > .../gate-check-namespace.log 2>&1` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh > .../gate-no-tautology.log 2>&1` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py > .../gate-doc-figures.log 2>&1` | 0 |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check > .../gate-complexity.log 2>&1` | 1 |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check > .../gate-file-length.log 2>&1` | 1 |

The two ratchets are red on the train on purpose. The gate logs list 6 complexity and 23 file-length `REGRESSION:` lines, and none of them names `src/core/StepRecorder.cpp`, `tests/src/gui/PianoRollBenchmarkTest.cpp`, or `tests/upstream-modifications.txt`. Under the wrapper's rule (fail only on new items in files the branch changed), both ratchets therefore have no item from this change. I did not run the base comparison the wrapper does, because it writes into the shared repo and the bunny-fix cache.

## Files in the commit

- `src/core/StepRecorder.cpp`
- `tests/src/gui/PianoRollBenchmarkTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-401.md`

## Self-check

1. **Regression fails without the fix and passes with it.** Yes. RED: `test-2-red.log`, ctest EXIT=8, SIGFPE in `StepRecorder::setStepsLength`. GREEN: `test-3-green.log` and `test-4-green-verbose.log`, EXIT=0, 8/8 slots. Reverse-patch proof: `test-5-4b-reverted.log` EXIT=8; `test-7-4b-reapplied.log` EXIT=0.
2. **Deterministic.** The new tests use no timers, sleeps or threads. They call `setStepsLength()`, `notePressed()` and `stepForwards()` directly on a `PianoRoll` and check plain tick values. `start()` is avoided because its hint needs the GUI; see the harness note above. The existing `frameTimeAtFourSizes` slot measures frame times, but it is pre-existing and not part of this change.
3. **Fix at the cause; callers and siblings checked.** The only division by the step length is `setStepsLength()` (StepRecorder.cpp#L181). Callers: `start()` (#L66) and `PianoRoll::noteLengthChanged()` (PianoRoll.cpp#L5109, which calls `setStepsLength(newNoteLen())`); both go through the same guard. Siblings: `stepForwards()`/`stepBackwards()` add or subtract without dividing. `StepRecorderWidget::setStepsLength()` (cluster new-p10-399) is a separate widget with its own guard.
4. **No collateral damage.** The rescale is unchanged for every non-zero old length, including negative lengths (`!= 0` only skips zero). The control test `rescalingInProgressStepKeepsStepCount` passes on both the original and the fixed code. No save format, command, or data changed.
5. **Realtime, undo, commands.** `setStepsLength()` runs on the GUI thread (piano roll and start path), not the audio thread. The change adds a comparison and nothing else: no allocation, lock, logging or signal. Undo and the journal are not touched. No control command was added, so the command snapshot and canary are unchanged.
6. **Registration and gates.** Both changed inherited files are registered in `tests/upstream-modifications.txt` with the cluster reason. The test file is already in CMake. Gate results are in the table above; `feature_check.py` itself could not run here (see Gates).
7. **ALREADY-FIXED / FALSE-POSITIVE evidence.** Not applicable: the verdict is FIXED.
