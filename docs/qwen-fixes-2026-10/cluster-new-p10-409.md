FIXED (narrowed: the finding's zero numerator is not reachable; the reachable bad state is a NaN numerator, which made the `int` conversion in `getNumerator()` undefined)

# Cluster new-p10-409 - Tap handling divides by a zero time-signature numerator

## Finding and verdict

Finding (Luna, BUG 18): `plugins/TapTempo/TapTempo.cpp:63` computes `% getNumerator()` and a zero numerator would divide by zero.

Checked against the code on this branch (base `37eaf07557`):

- **Zero is not reachable from finite values.** The numerator is an `IntModel` with range 1..32 (`src/core/MeterModel.cpp:35`). Every write path goes through `AutomatableModel::fittedValue`, which clamps to `[m_minValue, m_maxValue]` (`src/core/AutomatableModel.cpp:604-618`). Project load (`setInitValue` -> `setValue`, `AutomatableModel.cpp:1058-1072`) and the control/DAWproject setters all use that path.
- **Controller-driven values stay in range.** Linear: `min + range * currentValue` with `currentValue` clamped to [0,1] by `Controller::fittedValue` (`include/Controller.h:123-125`, `AutomatableModel.cpp:730`). Logarithmic: the member `logToLinearScale` maps into `[min,max]` (`AutomatableModel.cpp:530-533`, `include/lmms_math.h:301-313`), so it cannot produce 0 for min 1.
- **NaN is the reachable bad state.** `fittedValue` does not clamp NaN (`std::clamp` returns NaN), so a NaN can be stored. The controller path also returns an unfitted value (`AutomatableModel.h:216-233`, `AutomatableModel.cpp:722-741`), and `Controller::fittedValue` leaves NaN as NaN. `getNumerator()` then converted NaN to `int` through `castValue<int>` (`AutomatableModel.h:197-200`), which is undefined. On x86_64 the result is `INT_MIN`, which the modulo does not trap on, so this build does not crash. Other platforms may yield 0.

The fix is at the accessor every numerator divisor uses. `MeterModel::getNumerator()` now reads the value as a float and returns it only when it is at least 1; otherwise it returns 1 (`include/MeterModel.h`, new `getNumerator` and `setNumerator`). The 24 call sites of `getNumerator()` (including the `TapTempo.cpp:63` modulo, `TimeDisplayWidget.cpp:106,108`, `TimePos.cpp:46`, and `Song.cpp:971`) are covered by this one change.

Behaviour change: none for any valid numerator (1..32 finite values return the same integer). Only NaN and sub-1 values now read as 1.

Residual, not fixed here: the NaN is still stored in the model (`numeratorModel().value()`), and saving still writes it. Stopping NaN at `fittedValue` would change every model in the product, so it needs its own decision.

## Files committed

- `include/MeterModel.h` - accessor change (product fix; header, so every includer of `MeterModel.h` rebuilds).
- `tests/src/core/TapTempoTest.cpp` - regression test `aNonFiniteNumeratorIsReadAsOneBeatNotAsADivisor` plus `<limits>` and `MeterModel.h` includes.
- `docs/qwen-fixes-2026-10/cluster-new-p10-409.md` - this report.

No new files, so `tests/fork-sources.txt` and `tests/upstream-modifications.txt` need no entry (both changed files already exist in the tree).

## Regression test

`TapTempoTest::aNonFiniteNumeratorIsReadAsOneBeatNotAsADivisor` sets the song's numerator to NaN, reads `getNumerator()`, taps three times, reads `m_beat`, restores the numerator, and asserts `beatsPerBar >= 1` and `0 <= beat < beatsPerBar`. It is deterministic: `m_beat` changes only by the count, not by the clock, and the test does not sleep or race.

## Evidence (logs in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-409.r1/logs`)

| Step | Command (run from the worktree / `build/tests`) | Log | EXIT |
|---|---|---|---|
| RED build (unmodified product) | `cmake --build build --target TapTempoTest -j4` (with `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp-new-p10-409`) | build-red-2.log | 0 |
| RED test | `ctest -R '^TapTempoTest$' --output-on-failure` | test-red-1.log | **8** - `FAIL! ... 'beatsPerBar >= 1' returned FALSE` at `TapTempoTest.cpp:109`; Totals 5 passed, 1 failed |
| GREEN build | `cmake --build build --target TapTempoTest -j4` (after the fix) | build-green-2.log | 0 |
| GREEN build (no-op check) | same command | build-green-3.log | 0 |
| GREEN test | `ctest -R '^TapTempoTest$' --output-on-failure` | test-green-2.log | **0** - Test #386 Passed |
| 4b: save fix | `git diff -- include/MeterModel.h > fix.patch` | - | 0 |
| 4b: revert fix | `git apply -R fix.patch` | - | 0 |
| 4b: rebuild reverted | `cmake --build build --target TapTempoTest -j4` | build-revert-1.log | 0 |
| 4b: RED again | `ctest -R '^TapTempoTest$' --output-on-failure` | test-revert-red-1.log | **8** - same assertion at `TapTempoTest.cpp:109` |
| 4b: reapply fix | `git apply fix.patch` | - | 0 |
| 4b: rebuild reapplied | `cmake --build build --target TapTempoTest -j4` | build-reapply-1.log | 0 |
| 4b: GREEN again | `ctest -R '^TapTempoTest$' --output-on-failure` | test-reapply-green-1.log | **0** |

The test is proven to catch the bug: it fails with the fix reverted (exit 8) and passes with it restored (exit 0).

### Family test count

Command: `ctest -R '^(AudioPluginTest|AutomatableModelTest|TempoMapTest|TimelineTest|TapTempoTest)$' --output-on-failure` (run from `build/tests`) - log test-family-2.log - **EXIT 0, 5 of 5 tests executed and passed** (AudioPluginTest, AutomatableModelTest, TapTempoTest, TempoMapTest, TimelineTest). Zero tests were not run.

The first family run (test-family-1.log, EXIT 8) failed `AudioPluginTest` because plugin artifacts were not built in this targeted tree (`plugins/libeq.so`, the Gate module, `plugins/nam_harness`). These are environment build gaps, not the fix. I built them with `cmake --build build --target eq` (build-eq-plugin-1.log, EXIT 0) and `cmake --build build --target gate nam_harness -j4` (build-gate-nam-1.log, EXIT 0), and `AudioPluginTest` then passed (test-audioplugin-2.log, EXIT 0).

### Gates

- `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (as specified): **EXIT 1** - the script creates `<worktree>-feature-check`, a sibling directory outside the worktree, and the write-jail denies that (`PermissionError`, log gate-feature-check-1.log). I did not edit the shared gate.
- Gate run used for the verdict: an unmodified copy of `feature_check.py` (log `feature_check_logdir_redirect.py` in the LOG folder) with two changes only: the log directory is set to the LOG folder (`logs/feature-check`), and `ROOT` points at the original gate directory. Command: `python3 -I feature_check_logdir_redirect.py <worktree>` (log gate-feature-check-2.log) - **EXIT 0, RESULT: PASS** (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity: no new item, file-length: no new item).

The official gate command still needs the sibling directory to be writable to run unchanged.

### Build environment note

The shared ccache config (`/home/kruzzzzy/.cache/ccache-bunny/ccache.conf`) sets `temporary_dir` to a path I cannot write (`dsh/home/ccache-tmp-r6`). The first build failed with EXIT 2 (build-red-1.log, ccache permission error). Every build since then used `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp-new-p10-409`. The shared config was not changed.

A duplicate GREEN build was started and then stopped so only one build ran in the tree; the second build completed (build-green-2.log, EXIT 0) and a no-op rebuild confirmed the tree is consistent (build-green-3.log, EXIT 0).

## Self-check

Answered from the evidence above; the `submit_check.py` output is in the LOG folder once run.

- Verdict line present and first: FIXED (narrowed) - see the top of this file.
- Regression fails without the fix and passes with it: test-red-1.log / test-revert-red-1.log (exit 8) and test-green-2.log / test-reapply-green-1.log (exit 0).
- Siblings: all 24 `getNumerator()` call sites read through the one accessor; no other divisor site needed changing (checked by grep). `numeratorModel().value()` readers are not divisors and are unchanged.
- Old projects and saved data: a valid numerator (1..32) reads back as the same integer; saved values are written from the model value, which is unchanged.
- Realtime: `getNumerator()` is a const read of a float with no allocation, lock, or log; `Song::processMetronome` calls it on the audio thread and still does no more than before.
- Undo and commands: no control command added; `setNumerator` keeps the same `setValue((float) val)` body.
- Registration: no new files.
- Gates: see the Gates section; the official command could not write its log directory from inside the write-jail, so the PASS comes from the redirected copy described above.
- Unverified: the controller-driven NaN path (`Controller::fittedValue` passing NaN into `controllerValue`) is reasoned from the code and not exercised by a runtime test.
