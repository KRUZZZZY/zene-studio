ALREADY-FIXED

# Cluster new-p10-349: tail trim past the source end

## Verdict

The defect is already fixed on branch `bugs/qwen-new-p10-349`. No source, include, plugin, or test change is needed. This report is the only change.

## Code evidence

- `src/core/SampleFrameEdits.cpp:233` clamps the requested frame to the source length before anything else uses it:
  `const f_cnt_t at = std::min(frame, static_cast<f_cnt_t>(clip->sample().sampleSize()));`
- `src/core/SampleFrameEdits.cpp:234` computes the tail tick from the clamped frame: `const tick_t tail = tailTick(*clip, at);`
- `src/core/SampleFrameEdits.cpp:235` guards the refusal (`at <= sourceIn` or `tail <= start`).
- `src/core/SampleFrameEdits.cpp:240` calls `applyTail(clip, at, tail)` with the clamped frame and tick.
- `tests/src/core/ArrangingCatchUpTest.cpp:236` is the regression slot `aTailTrimPastTheSourceEndsWhereTheSourceEnds()`. It trims a clip to the source end, then trims a second clip to a frame far past the end, and requires the second clip's `sourceOut` and `endPosition().getTicks()` to equal the first clip's.

## Cause

Before the fix, `trimTailToFrame` passed the raw requested frame into `tailTick`. A frame past the source end produced a tail tick past the end of the source, so the clip's end moved beyond where the source stops. The clamp at line 233 runs before `tailTick` at line 234, so the tick is always computed from a frame inside the source.

In-range frames are unchanged. For any `frame <= sampleSize()`, `min(frame, sampleSize())` returns `frame`, so behaviour for valid edits is identical to the pre-fix code.

## History

- `49e9bb16c0` (`fix(arrange): bound tail trim to the source before its tail tick`) introduced the clamp and the regression slot. It is an ancestor of `HEAD` (`git merge-base --is-ancestor 49e9bb16c0 HEAD` exit 0). Its subject names cluster `new-p12-214`, so this fix is shared with that cluster.
- `git blame` on `src/core/SampleFrameEdits.cpp:233-235` attributes those lines to `49e9bb16c0`. Lines 230-232 come from the earlier commit `009f591b27`. Blame log: `/home/kruzzzzy/.config/kads/zene/state/runs/new-p10-349.r1/logs/blame-01.log`.
- The ancestry log `ancestry-01.log` is empty (0 lines). Ancestry was checked by the command above, not by that log.
- The diff of the fix is in `logs/fix.patch`.

## Test evidence

The RED/GREEN sequence was re-run in this session. Every log is in the run log folder `/home/kruzzzzy/.config/kads/zene/state/runs/new-p10-349.r1/logs/`. In the commands below this folder is `$LOGS`, and `logs/` elsewhere in this report means the same folder. `LOGS=/home/kruzzzzy/.config/kads/zene/state/runs/new-p10-349.r1/logs`. The worktree is `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p10-349`. Each command ran bare, with its output redirected into the named log, and each log ends with an `EXIT=` line. Before the RED ctest, `mkdir -p $LOGS/cache-13-red $LOGS/cache-13-green` ran with no output and no recorded exit line. The ctest runs use `XDG_CACHE_HOME` set to the writable folders `cache-13-red` and `cache-13-green` inside `$LOGS`.

| Step | Exact command (worktree root unless `cd` is shown) | Exit | Log and evidence |
|---|---|---|---|
| Patch | `git diff 49e9bb16c0^ 49e9bb16c0 -- src/core/SampleFrameEdits.cpp > $LOGS/fix.patch; echo EXIT=$? > $LOGS/patch-13.log` | 0 | `patch-13.log`; the patch is `fix.patch` (22 lines) |
| Revert fix | `git apply -R $LOGS/fix.patch > $LOGS/apply-reverse-13.log 2>&1; echo EXIT=$? >> $LOGS/apply-reverse-13.log` | 0 | `apply-reverse-13.log` |
| Revert check | `git diff --stat -- src/core/SampleFrameEdits.cpp > $LOGS/revert-stat-13.log 2>&1; echo EXIT=$? >> $LOGS/revert-stat-13.log` | 0 | `revert-stat-13.log`: 1 file changed, 3 insertions(+), 4 deletions(-) |
| RED build | `cmake --build build --target zene ArrangingCatchUpTest > $LOGS/build-13-red.log 2>&1; echo EXIT=$? >> $LOGS/build-13-red.log` | 0 | `build-13-red.log` |
| RED ctest | `cd build/tests && XDG_CACHE_HOME=$LOGS/cache-13-red ctest -R '^ArrangingCatchUpTest$' --output-on-failure > $LOGS/test-13-red.log 2>&1; echo EXIT=$? >> $LOGS/test-13-red.log` | 8 | `test-13-red.log` lines 15-17: `aTailTrimPastTheSourceEndsWhereTheSourceEnds()` fails; `beyond->endPosition().getTicks()` is 324, expected 224. Summary: "0% tests passed, 1 tests failed out of 1". The earlier `logs/test-07-red.log` lines 16-17 show the same failure. |
| Restore fix | `git apply $LOGS/fix.patch > $LOGS/apply-restore-13.log 2>&1; echo EXIT=$? >> $LOGS/apply-restore-13.log` | 0 | `apply-restore-13.log` |
| GREEN build | `cmake --build build --target zene ArrangingCatchUpTest > $LOGS/build-13-green.log 2>&1; echo EXIT=$? >> $LOGS/build-13-green.log` | 0 | `build-13-green.log` |
| GREEN ctest | `cd build/tests && XDG_CACHE_HOME=$LOGS/cache-13-green ctest -R '^ArrangingCatchUpTest$' --output-on-failure > $LOGS/test-13-green.log 2>&1; echo EXIT=$? >> $LOGS/test-13-green.log` | 0 | `test-13-green.log`: "100% tests passed out of 1" |
| Tail slot | `cd build/tests && XDG_CACHE_HOME=$LOGS/cache-13-green ./ArrangingCatchUpTest aTailTrimPastTheSourceEndsWhereTheSourceEnds > $LOGS/test-13-green-tail.log 2>&1; echo EXIT=$? >> $LOGS/test-13-green-tail.log` | 0 | `test-13-green-tail.log`: "Totals: 3 passed, 0 failed, 0 skipped" |
| Product diff | `git diff --exit-code -- src/core/SampleFrameEdits.cpp > $LOGS/product-diff-13.log 2>&1; echo EXIT=$? >> $LOGS/product-diff-13.log` (after the restore) | 0 | `product-diff-13.log` |
| Status | `git status --porcelain > $LOGS/status-13.log 2>&1; echo EXIT=$? >> $LOGS/status-13.log` | 0 | `status-13.log`: no porcelain lines (clean) |

The earlier logs in `logs/` (`build-07-green-body.log`, `test-07-green-body.log`, `test-07-green-tail.log`) show the same results but have no `EXIT=` lines, so they are not cited for exit codes. Every exit code above comes from a command in the table.

## Canonical test command and the default-environment failure

- Default environment (`logs/test-01.log`, `ctest -R '^ArrangingCatchUpTest$' --output-on-failure`, no override): entry 377 fails with 2 failed, 11 passed, 1 skipped. The two failures are:
  - `newDocumentsCarryTheirTypesTemplate()`: `mastering.mpt` cannot be written to `/home/kruzzzzy/.cache/ArrangingCatchUpTest/document-templates/`, which is not writable here.
  - `queuedInstrumentHandlesAreRemovedBeforeTheirTrackCanDie()`: compared values are not the same.
- `logs/test-02.log`: 12 passed, 1 failed. The only failure is the cache one. The queued-instrument check and the tail slot pass (`PASS` at line 16). The queued-instrument failure is gone here; the cause (building the tripleoscillator artefact) was not re-checked in this session.
- The tail slot passes in both runs, so the default-environment failure comes from the unwritable cache path, not from the clip-trim code.
- Canonical command: ctest with `XDG_CACHE_HOME` pointed at a writable directory under the run's log folder. `logs/test-03-xdg-override.log` shows 13 passed, 0 failed, 1 skipped. `logs/test-04-ctest-xdg-override.log` shows 100% tests passed, 1 of 1. The GREEN ctest above (`004-p4b-green-ctest.log`) uses the same override and is also green.

## Self-check

1. **RED/GREEN exit codes.** RED ctest exits 8 with the tail slot failing at 324 vs 224. GREEN build exits 0, GREEN ctest exits 0 (1 of 1), and the tail slot exits 0 (3 passed). The table above has the commands and logs.

2. **Determinism.** `tests/src/core/ArrangingCatchUpTest.cpp` has no wall-clock, sleep, or timer calls (a grep for `sleep`, `qWait`, `QTime`, `QDateTime`, `QElapsedTimer`, and `rand` found nothing). The slot uses `freshClip()` and direct frame values (`size` and `size + clipFramesPerTick() * 100`). The cache is injected with `XDG_CACHE_HOME`, and the tail slot passes in both the default and override runs.

3. **Cause and completeness.** The clamp at line 233 runs before `tailTick` (line 234) and `applyTail` (line 240). `checkTrim` (`src/core/ControlCommandsClipFrames.cpp:91-100`) compares `frames.tail <= frames.head` after `trimFrames` has already called `frameFor`. The control path's `frameFor` (`ControlCommandsClipFrames.cpp:58-61`) calls `arrangement::frameAtTimeline`, which clamps both head and tail to `[0, sampleSize()]` (`src/core/SampleFrameEdits.cpp:183`) before `checkTrim` sees them. `trimTailToFrame` clamps again at line 233. The comparison and the final clamp therefore observe the same bounded values. This is read-code reasoning; no test isolates this case. Sibling caller `splitAtFrame` (`src/core/SampleFrameEdits.cpp:198-213`) passes its raw `frame` to `tailTick` at line 204 with no clamp. The raw frame is bounded by the split guard, as follows:
   - `splitAtFrame` calls `canSplitAtFrame` at line 200 (defined at line 154), which calls `splitFits` at line 156.
   - `splitFits` (lines 110-127) rejects `frame >= window.sourceOut` at line 113.
   - `replaceSample` stores `SampleWindow::clamped(window.sourceIn, window.sourceOut, frames)` with `frames = sampleSize()` (`src/core/SampleClip.cpp:479`). An empty result falls back to `SampleWindow::full(frames)` (line 480).
   - `SampleWindow::clamped` caps `sourceOut` at `bufferFrames` (`include/SampleWindow.h:64-69`; the comment at lines 58-63 says the window is clamped into [0, bufferFrames]).
   - So an accepted `frame` is below `sourceOut`, and `sourceOut` is at most `sampleSize()`. The raw `frame` at line 204 is therefore below `sampleSize()`, and `splitAtFrame` needs no clamp of its own.
   - `snapEdgeToZeroCrossing` (line 254) reaches `trimTailToFrame` at line 261 through `frameAtTimeline` (line 259). `frameAtTimeline` clamps to `[0, sampleSize()]` at `SampleFrameEdits.cpp:183`, so that path is already clamped before the fix's clamp at line 233.
   - This chain was read from the code. No test isolates the `splitAtFrame` bound.

4. **Collateral damage.** None. The change is docs-only. `git diff --quiet HEAD -- src tests` exited 0 before the commit (`src-tests-diff-15.log`, `EXIT=0`) and again after it (`src-tests-diff-15-post.log`). The docs-only commit is shown by `show-15.log` (`git show --stat --oneline HEAD`). These logs are in `runs/new-p10-349.r1/logs`. The clamp is the identity for in-range frames: for `frame <= sampleSize()`, `min(frame, sampleSize())` is `frame`.

5. **ALREADY-FIXED evidence.** `src/core/SampleFrameEdits.cpp:233` clamps the frame before `tailTick` (see Code evidence). The regression slot is `tests/src/core/ArrangingCatchUpTest.cpp:236`.
