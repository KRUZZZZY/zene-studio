FIXED

# Cluster new-p14-176: step-menu index multiplication overflows int before conversion

Branch `bugs/qwen-new-p14-176` (base `050/m0-train-8`, 2da2ed356). Verdict: the finding is REAL on this tree and is fixed.

## Evidence

- **The defect.** `src/gui/clips/MidiClipView.cpp:303` (before: `(at.x() - BORDER_WIDTH) * m_clip->m_steps`) multiplied two `int`s, then widened the result to `qint64`. The widening happens after the overflow. `at.x()` is the cursor's x in the clip (`:300`), and `m_steps` is an `int`.
- **The count is reachable.** The loader accepts any step count up to `maxSteps = INT_MAX / (2 * ticksPerBar)` (`src/tracks/MidiClip.cpp:548-549`), which is 5,592,405 at 192 ticks per bar. At 5,000,000 steps and a cursor 1,000 px in, the product is 998 × 5,000,000 = 4.99e9, above `INT_MAX`.
- **Observed before the fix.** With the fix reverted, the step-menu test cannot find the `Step N Chance` submenu for the step under the cursor, so the menu offers no edit for the intended step (`tests/src/gui/MidiClipStepMenuTest.cpp:167`, RED below).
- **Fix.** `src/gui/clips/MidiClipView.cpp:303` converts the offset to `qint64` before the multiplication. The division, the `std::max(1, …)` guard and the clamp at `:304` are unchanged, so the click-to-step mapping for every count that did not overflow is the same.
- **Sibling fixed too.** The beat-clip paint loop computed each step's x as `it * w / steps` in `int` (`src/gui/clips/MidiClipView.cpp:759`). `it` runs up to the step count and `w` is the view width, so the product can overflow the same way. It now widens `it` to `qint64`. This path only runs when the clip's pixels-per-bar is at least 96 or the clip is fixed (`:737-738`, `displayPattern`). Painting millions of steps in a test is too heavy, so **this change has no regression test of its own**; the behaviour change is limited to inputs where the old product overflowed.
- **Siblings checked and left alone.** `MidiClipView.cpp:529-530` and `:599-600` compute the same index in `float`, which has no int-overflow issue. `TimePos::stepPosition` (`include/TimePos.h:140`, `step * ticksPerBar() / stepsPerBar()`) stays inside `int32` at the loader's limit: 5,592,405 × 192 = 1,073,741,760 < 2,147,483,647. `addStepEditsMenu` (`src/gui/ClipEditsMenu.cpp:200`) takes an `int` step that is already clamped to `[0, m_steps - 1]`, so it is unchanged.
- **Collateral.** No loader or limit changes. Old projects load as before. No new control command. No audio-thread code: the menu and the paint run on the GUI thread. Undo still goes through the existing `note.probability_set` command.

## Regression test

- New file `tests/src/gui/MidiClipStepMenuTest.cpp`, test `aStepMenuFarAcrossAHugeBeatClipEditsTheStepUnderTheCursor`. It loads a beat clip with 5,000,000 steps through `MidiClip::loadSettings`, checks the count with `saveSettings`, realigns the song editor, and places the cursor at `view->mapToGlobal(QPoint(1000, 5))`. It opens the real context menu, triggers `50 %` under the `Step N Chance` submenu for the step under the cursor, and checks that note's probability is 0.5.
- Why a new file: I first put the test into `tests/src/gui/MidiClipLinkedEditTest.cpp`. That file's code-line count went from 498 to 563, so the file-length ratchet turned it into a new over-500 file. I moved the test into its own file and restored `MidiClipLinkedEditTest.cpp` to `HEAD`.
- Determinism: no sleeps or timing. The step is computed from the view width and the cursor position, which are the test's inputs; the test checks that the view width did not change between computing the step and opening the menu (`QCOMPARE(view->width(), width)`). The only timers are the existing 1,500 ms popup-close fallback, which only runs if the menu is never opened, and a 0 ms singleShot that runs inside the popup's own event loop.
- Registration: `tests/CMakeLists.txt` (test list and `set_tests_properties`), `tests/fork-sources.txt` and `tests/all-sources.txt` (new file), `tests/upstream-modifications.txt` (inherited `src/gui/clips/MidiClipView.cpp` with a reason).

## Commands and exit codes

Build environment note: the default ccache temporary directory `/run/user/1000/ccache-tmp` is not writable from this sandbox. Every build below sets `CCACHE_TEMPDIR=/tmp/ccache-tmp-p14`. The first build attempt failed on that alone (`logs/build-1-red.log`, EXIT=2).

| Step | Command (run from `build/`, or from the worktree) | EXIT | Log (LOG FOLDER) |
| --- | --- | --- | --- |
| Test written, product unchanged, first RED attempt (test then in `MidiClipLinkedEditTest`) | `ctest -R '^MidiClipLinkedEditTest$' --output-on-failure` | 8 | `test-2-red.log` / `test-3-red.log` / `test-11-prefix-RED.log` |
| RED, original product code, step test in new file | `git apply -R fix.patch`; `cmake --build build --target zene MidiClipStepMenuTest -j4`; `ctest -V -R '^MidiClipStepMenuTest$'` (in `build/tests`) | build 0, test **8** | `build-16-revert-stepmenu.log`, `test-14-RED-stepmenu.log` (`FAIL! … 'triggerSubmenuItemOfContextMenu(…"Step %1 Chance"…)' returned FALSE`, `MidiClipStepMenuTest.cpp:167`) |
| Fix re-applied | `git apply fix.patch` | 0 | — |
| GREEN, new test | `cmake --build build --target zene MidiClipStepMenuTest MidiClipLinkedEditTest -j4`; `ctest -V -R '^MidiClipStepMenuTest$'` | build 0, test **0** (3 passed) | `build-17-green-final.log`, `test-13-green-stepmenu.log` |
| Affected family, `MidiClip*` | `cmake --build build --target MidiClipControllersLoadTest -j4`; `ctest -V -R '^MidiClip' --output-on-failure` | build 0, test **0** (3/3 passed: MidiClipControllersLoadTest, MidiClipLinkedEditTest, MidiClipStepMenuTest) | `build-18-family.log`, `test-16-GREEN-family.log` |
| Patch used for 4b | `git diff -- src/gui/clips/MidiClipView.cpp > logs/fix.patch` | 0 | `fix.patch` (the product diff; `cmp` matched the diff again before the revert) |
| Ratchet gates (direct, the same scripts `feature_check.py` runs) | see Self-check below | see below | `gate-4-*.log`, `gate-5-*.log` |
| `feature_check.py` (logs redirected, see below) | `python3 -I <copy with logs to LOG FOLDER> <worktree>` | 0, **RESULT: PASS** (8 rows) | `feature-check-1.log` |
| `submit_check.py` (after the fix commit `3a4eecafc`) | `python3 submit_check.py <worktree> new-p14-176` | 1: commits, clean tree, report, tests and registration PASS; **gates FAIL only because the unmodified `feature_check.py` cannot create its log directory** (`PermissionError`, `logs/feature-check-unmodified.log`) | `submit-check-2.log` |
| Unmodified `feature_check.py` | `python3 feature_check.py <worktree>` | 1 (same `PermissionError`) | `feature-check-unmodified.log` |

Gate outcomes at the final tree:

- `tests/fork-sources-gate.sh` EXIT=0; `tests/all-sources-reproduce.sh` EXIT=0; `tests/no-upstream-regression-gate.sh` EXIT=0; `tests/scripted/check-namespace` EXIT=0; `tests/no-tautology-gate.sh` EXIT=0; `tests/doc-figures-gate.py` EXIT=0 (`gate-4-*.log`, `gate-5-*.log`).
- `tests/complexity-gate.sh --check` EXIT=1 and `tests/file-length-gate.sh --check` EXIT=1. Both fail on the train's pre-existing items. The logs have no entry for `MidiClipView.cpp`, `MidiClipStepMenuTest.cpp` or `MidiClipLinkedEditTest.cpp` (`grep -i MidiClip gate-4-complexity.log gate-4-file-length.log`). `feature_check.py` ranks those two rows against the cached base listing, and both PASS.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.** Yes. RED: `git apply -R logs/fix.patch`, build EXIT 0, `ctest -V -R '^MidiClipStepMenuTest$'` EXIT 8, failure at `tests/src/gui/MidiClipStepMenuTest.cpp:167`. GREEN: `git apply logs/fix.patch`, build EXIT 0, `ctest -V -R '^MidiClipStepMenuTest$'` EXIT 0, 3 passed.
- **The test is deterministic.** Yes. No sleeps, no wall-clock values. The inputs are fixed: 5,000,000 steps, cursor x = 1,000, y = 5. The expected step is computed from the view width, and the test checks that the width stays the same before the menu opens (`QCOMPARE(view->width(), width)`). The popup helper runs its trigger inside the popup's event loop (0 ms singleShot); the 1,500 ms singleShot only closes a popup that never opened, so it cannot race a passing run.
- **The fix is at the cause and complete.** Yes. The menu's product is widened at the one place it is computed (`MidiClipView.cpp:303`). The clamp, the `std::max(1, …)` guard and the `float` siblings (`:529`, `:599`) are checked above. The paint sibling (`:759`) is fixed too and has no test (see Evidence).
- **No collateral damage.** The loader limit is unchanged and only applies to loading; the menu and paint change only results that overflowed int before. No new limit or check, so no caller is affected.
- **Verdict is FIXED, not ALREADY-FIXED or FALSE-POSITIVE.** The RED run above shows the defect on this tree.

Submit-check note. The first `submit_check.py` run, before the fix was committed, also flagged the working tree and missing report; both are resolved by the commit and this report. The remaining FAIL is the gate row, which cannot pass in this sandbox because the write-jail blocks the log directory the script creates next to the worktree. The copy run above is the evidence for that row.

Gate-running notes. `tests/` gate scripts were run directly from the worktree. `feature_check.py` and `submit_check.py` write their logs to `<worktree>/../…-feature-check`, which the write-jail refuses (`PermissionError` at `mkdir`, from `feature_check.py` line 71, before any gate ran). I ran a copy of `feature_check.py` (`/tmp/p14-gate/feature_check_logdir.py`) that writes its logs to the LOG FOLDER and reads the existing `gate-baselines` cache. The row logic is unchanged. The unmodified script's own RESULT is therefore unverified here.
