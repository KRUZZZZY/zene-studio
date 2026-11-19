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

The RED/GREEN sequence was run again in this session. Its output is in `/home/kruzzzzy/.config/kads/zene/state/runs/new-p10-349.r1/duet/14-worker/tmp/` (files `001`-`005`, `p4b-*`). All commands run in the worktree, and the cache is `XDG_CACHE_HOME=logs/cache-p4b`.

| Step | Command | Exit | Evidence |
|---|---|---|---|
| Patch | generation of `logs/fix.patch` | not recorded in any log, not re-run | the patch itself is `logs/fix.patch` (22 lines) |
| Revert fix | `git apply -R logs/fix.patch` | 0 | |
| RED build | `cmake --build build --target ArrangingCatchUpTest` | 0 | `001-p4b-red-build.log` |
| RED ctest | `ctest -R '^ArrangingCatchUpTest$' --output-on-failure` in `build/tests` | 8 | `002-p4b-red-ctest.log`: `aTailTrimPastTheSourceEndsWhereTheSourceEnds()` fails, `beyond->endPosition().getTicks()` is 324, expected 224. Same in `logs/test-07-red.log` lines 16-17. |
| Restore fix | `git apply logs/fix.patch` | 0 | |
| Product diff | `git diff --quiet HEAD -- src tests` after restore | 0 | |
| GREEN build | same build command | 0 | `003-p4b-green-build.log` |
| GREEN ctest | same ctest command | 0 | `004-p4b-green-ctest.log`: 100% tests passed, 1 of 1 |
| Tail slot | `./ArrangingCatchUpTest aTailTrimPastTheSourceEndsWhereTheSourceEnds` in `build/tests` | 0 | `005-p4b-green-tail.log`: 3 passed, 0 failed |
| Final product diff | `git diff --quiet HEAD -- src tests` | 0 | |
| Clean status | `git status --porcelain` | empty | |

The earlier logs in `logs/` (`build-07-green-body.log`, `test-07-green-body.log`, `test-07-green-tail.log`) show the same results without exit-code lines. The exit codes above come from the re-run.

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

3. **Cause and completeness.** The clamp at line 233 runs before `tailTick` (line 234) and `applyTail` (line 240). `checkTrim` (`src/core/ControlCommandsClipFrames.cpp:91-100`) compares the raw `frames.tail <= frames.head` before clamping. The control path's `frameFor` (`ControlCommandsClipFrames.cpp:58-61`) calls `arrangement::frameAtTimeline`, which clamps to `[0, sampleSize()]` (`src/core/SampleFrameEdits.cpp:183`). So both frames are already clamped there, and `trimTailToFrame` clamps again at line 233. Because `min` is monotone, a raw `tail <= head` implies the clamped `tail <= head`. The raw check can only refuse earlier, with a more specific message. This is read-code reasoning; no test isolates this case. Sibling caller: `splitAtFrame` (`src/core/SampleFrameEdits.cpp:204`) passes its unclamped `frame` to `tailTick`. It is guarded by `canSplitAtFrame`, whose bound was not read here, so the fix's completeness for that caller is not verified.

4. **Collateral damage.** None. The change is docs-only, so the product diff is empty (`git diff --quiet HEAD -- src tests` exit 0). The clamp is the identity for in-range frames: for `frame <= sampleSize()`, `min(frame, sampleSize())` is `frame`.

5. **ALREADY-FIXED evidence.** `src/core/SampleFrameEdits.cpp:233` clamps the frame before `tailTick` (see Code evidence). The regression slot is `tests/src/core/ArrangingCatchUpTest.cpp:236`.
