Verdict: FIXED

new-p22-20: FIXED. `splitPasses` (comp.split_passes) ignored `addLane()`'s `-1`. Verdict REAL, reachable through a project file: `TakeLaneModel::loadSettings` reads the `nextlane` high-water mark from the file (src/core/TakeLane.cpp:687 in the fixed tree), and `addLane()` returns -1 once that mark reaches `INT_MAX` (src/core/TakeLane.cpp:358). The unchecked `-1` reached the loop at src/core/ControlCommandsCompGroup.cpp line 288 of the base tree (294 after the fix): the clip was already cut, the pass past the last free number was tagged lane -1 (`Clip::setLaneIndex` clamps that to the base lane, include/Clip.h:192), and the command still returned success with a composite it never set (`selectSegment` refuses lane -1 at src/core/TakeLane.cpp:444 in `canSelect`, and its result was ignored at line 296 of the base tree, 302 after the fix).

Fix: `TakeLaneModel::canAddLanes(count)` (include/TakeLane.h, src/core/TakeLane.cpp) and a preflight in `splitPasses` (src/core/ControlCommandsCompGroup.cpp:273-278) that refuses before the Track checkpoint and before any split when the track cannot hand out one lane number per pass.

## Evidence

| Item | Evidence |
| --- | --- |
| Finding location | `splitPasses` loop: src/core/ControlCommandsCompGroup.cpp:286-291 (base tree; 292-297 after the fix); `addLane` -1 path: src/core/TakeLane.cpp:358; `nextlane` load: src/core/TakeLane.cpp:687 (fixed tree) |
| Regression test | `tests/src/core/RecordToolsTest.cpp`: `splittingPassesRefusesWhenTheTrackHasTooFewLaneNumbers` (new) and `splittingPassesUsesTheLastLaneNumbersTheTrackHas` (new boundary test) |

### Commands and exit codes

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p22-20.r1/logs`

| Step | Command (run from the worktree unless noted) | EXIT | Log |
| --- | --- | --- | --- |
| Build, first try | `cmake --build build --target RecordToolsTest -j4 > build-red-1.log 2>&1` | 2 | build-red-1.log (ccache could not write its temp dir, see Notes; not a test result) |
| Build, RED (ccache temp dir overridden) | `CCACHE_TEMPDIR=$HOME/.cache/zene-tmp/ccache-tmp-new-p22-20 cmake --build build --target RecordToolsTest -j4 > build-red-2.log 2>&1` | 0 | build-red-2.log |
| Test, RED (product code unfixed) | `cd build/tests && ctest -R RecordToolsTest --output-on-failure > test-red-1.log 2>&1` | 8 | test-red-1.log: `FAIL! splittingPassesRefusesWhenTheTrackHasTooFewLaneNumbers`, `Actual (split.errorKind): 0`, `Expected (ControlErrorKind::Refused): 5`, `RecordToolsTest.cpp(311)` |
| Build, GREEN | `CCACHE_TEMPDIR=... cmake --build build --target zene RecordToolsTest -j4 > build-green-1.log 2>&1` | 0 | build-green-1.log |
| Test, GREEN | `cd build/tests && ctest -R RecordToolsTest -V > test-green-2.log 2>&1` | 0 | test-green-2.log: `Totals: 26 passed, 0 failed, 1 skipped` |
| Mutation: save product fix | `git diff -- include/TakeLane.h src/core/TakeLane.cpp src/core/ControlCommandsCompGroup.cpp > fix.patch` | 0 | fix.patch (50 lines) |
| Mutation: revert fix | `git apply -R fix.patch` | 0 | (none) |
| Mutation: build | `CCACHE_TEMPDIR=... cmake --build build --target zene RecordToolsTest -j4 > build-mutant-1.log 2>&1` | 0 | build-mutant-1.log |
| Mutation: test must FAIL | `cd build/tests && ctest -R RecordToolsTest -V > test-mutant-1.log 2>&1` | 8 | test-mutant-1.log: same `FAIL!` at `RecordToolsTest.cpp(311)`, `Actual 0`, `Expected 5`; `25 passed, 1 failed, 1 skipped` |
| Mutation: re-apply fix | `git apply fix.patch` | 0 | (none) |
| Mutation: build | `CCACHE_TEMPDIR=... cmake --build build --target zene RecordToolsTest -j4 > build-green-2.log 2>&1` | 0 | build-green-2.log |
| Mutation: test must PASS | `cd build/tests && ctest -R RecordToolsTest -V > test-green-3.log 2>&1` | 0 | test-green-3.log: `26 passed, 0 failed, 1 skipped` |
| Header changed: other includers rebuilt | `CCACHE_TEMPDIR=... cmake --build build --target TakeLaneCompTest TakeLaneTest TakeLaneCompPlaybackTest TrackViewDeferredLifetimeTest ScenesUpconvertTest RecordingRealtimeTest RecordingPassTest RangeEditsTest AudioResamplerRatioTest LanesUpconvertTest ClipConsolidateTest -j4 > build-family-1.log 2>&1` | 0 | build-family-1.log |
| Family run 1 | `cd build/tests && ctest -R "TakeLane\|TrackViewDeferredLifetimeTest\|ScenesUpconvertTest\|RecordingRealtimeTest\|RecordingPassTest\|RangeEditsTest\|AudioResamplerRatioTest\|LanesUpconvertTest\|ClipConsolidateTest\|RecordToolsTest" --output-on-failure > test-family-1.log 2>&1` | 8 | test-family-1.log: 12 of 13 passed; `TakeLanesPanelTest` "Not Run" (its binary was not in the list above) |
| Build missing target | `CCACHE_TEMPDIR=... cmake --build build --target TakeLanesPanelTest AudioResamplerRatioTest -j4 > build-family-2.log 2>&1` | 0 | build-family-2.log |
| Family run 2 | `cd build/tests && ctest -R TakeLanesPanelTest --output-on-failure > test-family-2.log 2>&1` | 0 | test-family-2.log: `100% tests passed out of 1` |

Affected family (`ctest -R` over the 13 tests that match the lane and comp family, including `RecordToolsTest`): 13 of 13 pass when `TakeLanesPanelTest` is counted from its own run (run 1 passed the other 12, run 2 passed the 13th). `RecordToolsTest` itself: 26 passed, 0 failed, 1 skipped; the skip is `armedRoutesOnOneFilesystemUnderTwoMountPointsShareItsFreeSpace` ("this host cannot give one filesystem two mount points"), an environment limit unrelated to this change, and it was skipped before the fix too (test-red-1.log).

### Gates

`feature_check.py` itself could not run: it writes its logs to `<worktree>-feature-check/` next to the worktree, outside the write-jail (`gates-1.log`: `PermissionError`, EXIT 1). I did not change the script. I ran each of its gate commands from the worktree with logs in the LOG folder:

| Gate | Command | EXIT | Log |
| --- | --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 | gate-fork-sources.log |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 | gate-all-sources.log |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 | gate-no-upstream.log |
| check-namespace | `tests/scripted/check-namespace` | 0 | gate-namespace.log |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 | gate-no-tautology.log |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 | gate-doc-figures.log |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 | gate-complexity.log |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 | gate-file-length.log |

The two ratchets are red on the train. Every REGRESSION line in them is in a file this branch did not touch (WasmSandbox, XyPadPairTest, DawProjectReadTracks, ControlCommandsMixerStrip, ControlCommandsMixerTools, ControlCommandsSessionRecordLand, ControlCommandsShortcuts, and the file-length list). The only lines that name a changed file are two file-length items:

- `src/core/TakeLane.cpp` (580 code lines). Code lines at the base `7641e9493d`: 576 (counted with the gate's rule, `count_code.py`, which matches the gate's current 580 and 556 figures). Already over 500 at the base, so the item is not new. `feature_check`'s `key()` strips the counts, so it matches the base listing.
- `tests/src/core/RecordToolsTest.cpp` (556 code lines). At the base: 512. Already over 500, so not new.

No complexity item names `splitPasses` or any other changed function.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.** Yes. Reverted with `git apply -R fix.patch` (EXIT 0); the test fails with `Actual 0 / Expected 5` at `RecordToolsTest.cpp:311` (`test-mutant-1.log`, EXIT 8). Re-applied with `git apply fix.patch` (EXIT 0); the test passes (`test-green-3.log`, EXIT 0).
- **The test is deterministic.** It has no clock, sleep or thread. The lane high-water mark is set through the production loader, `TakeLaneModel::loadSettings` with a `nextlane` attribute of `INT_MAX - 2` (the same path a saved project takes). The clip, loop and pass counts are fixed ticks. The boundary test uses the same fixture with two passes, which exactly fits.
- **The fix is at the cause and complete (within this finding).** `canAddLanes` is consulted only by `splitPasses`. The check runs before the Track checkpoint and before any split, so a refusal changes nothing and records no undo step. Its arithmetic is `count <= INT_MAX - m_nextLaneIndex`, which is exactly the condition under which `addLane()` never returns -1 for `count` calls (checked by the boundary test). `passes` is capped at 32 by the existing check just above, so the `int` cast is safe. After the preflight the loop's `addLane()` cannot return -1: nothing else changes lane numbers between the preflight and the loop.
  - **Siblings NOT changed (same unchecked `-1`, reported as follow-ups; they need a decision the finding does not give):**
    - `comp.lane_add` (src/core/ControlCommandsComp.cpp:136) returns lane `-1` as a success result.
    - `SampleClip::addRecordedLane` (src/core/SampleClip.cpp:396) and `SampleClip::addHistoryLane` (src/core/SampleClip.cpp:413) build a lane clip with `-1`. These run on the recording-stop path, where the right refusal is not obvious.
  - The new method `canAddLanes` has no other caller, so it cannot change any other behaviour.
- **No collateral damage.** Saved projects load as before: `loadSettings`, `saveSettings` and `nextlane` are unchanged. The refusal can only fire when the high-water mark is within `passes` of `INT_MAX`, which a normal project never reaches; the check is not a limit on lanes in general. Where `splitClipAt` returns fewer pieces than `passes`, the preflight is conservative (it may refuse a split that would have used fewer lanes), and that only matters at the same exhaustion point. Undo is unchanged: the success path still takes the same checkpoint and returns the same record.
- **Sibling commands in the command files changed** (`src/core/ControlCommandsCompGroup.cpp` defines comp.move_edge, comp.split_passes, comp.select_group, comp.take_select and comp.play_all_takes; the others on the checklist are in other files). Only `comp.split_passes` was changed. Checked with `rg -n 'addLane\(' src include`: the only callers of `addLane` are `comp.lane_add` (src/core/ControlCommandsComp.cpp:136), `SampleClip::addRecordedLane` and `addHistoryLane` (src/core/SampleClip.cpp:396, 413), and `splitPasses`. So comp.assign, comp.audition, comp.cycle, comp.get_state, comp.lane_list, comp.lane_remove, comp.move_edge, comp.play_all_takes, comp.rebuild, comp.select, comp.select_group and comp.take_select do not create lanes and are not affected by the fix. comp.lane_add is the one sibling with the same unchecked `-1` (see the bullet above); it is not changed here.
- **Realtime, undo and commands.** No audio-thread code is touched. `canAddLanes` reads one int on the main thread, as `addLane` does. No command was added, so `ControlCommandsSnapshot` is unchanged.

## Notes

- **ccache temp dir.** The build's ccache config (`/home/kruzzzzy/.cache/ccache-bunny/ccache.conf`) points `temporary_dir` at a path outside the write-jail, so the first build failed with `Permission denied` (`build-red-1.log`). I set `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp/ccache-tmp-new-p22-20` for the builds. The compiler and code are unchanged by it.
- **feature_check.py.** Could not run here (log directory outside the write-jail). The six simple gates and the two ratchets were run directly, as above. The fleet owner should run `feature_check.py` from an environment that can write the sibling log directory before merge.
