Verdict: FALSE-POSITIVE

# Cluster new-p20-16 — Sub-scene launch reads a slot before checking its column

The finding is correct about statement order and wrong about consequence. `launchCells` does call `model.slot(track, scene)` at [ControlCommandsSessionLaunch.cpp:317](src/core/ControlCommandsSessionLaunch.cpp:317) before the range check at [ControlCommandsSessionLaunch.cpp:318](src/core/ControlCommandsSessionLaunch.cpp:318). But the accessor is bounds-checked by contract, so an out-of-grid column returns a shared empty slot and the existing check then skips it. No read is out of bounds, nothing is launched, and no behaviour differs from the "fixed" order.

## Evidence

1. **The accessor guards the read.** The const overload used by `launchCells` (`model` is `const SessionModel&`) is at [SessionModel.cpp:253](src/core/SessionModel.cpp:253). Its comment says out-of-range reads "return a shared empty slot instead of crashing; the grid is only valid for [0, trackCount) x [0, sceneCount)". The guard is at [SessionModel.cpp:258](src/core/SessionModel.cpp:258), and the shared `static const ClipSlot empty` is at [SessionModel.cpp:257](src/core/SessionModel.cpp:257). A track beyond `trackCount`, or a negative track, yields `empty`, and `empty.isEmpty()` is true.

2. **The skip is already in place.** [ControlCommandsSessionLaunch.cpp:318](src/core/ControlCommandsSessionLaunch.cpp:318) is `if (track < 0 || track >= launchable || slot.isEmpty()) { continue; }`. Both the out-of-grid case and the out-of-song-tracks case are excluded before `launchOne` or `launchRequestOf` run. The reference is never written through, because the const overload returns a `const ClipSlot&` and the loop reads only `isEmpty()`.

3. **Sub-scene columns are bounded again on the way out.** `launchSubScene` takes `launchable = launchableColumnCount(...)` at [ControlCommandsSessionSubScenes.cpp:164](src/core/ControlCommandsSessionSubScenes.cpp:164), and `skippedColumns` at [ControlCommandsSessionSubScenes.cpp:141](src/core/ControlCommandsSessionSubScenes.cpp:141) uses the same `model.slot` read with the same `track >= launchable` test. Those reads are also bounds-safe, and a column past the grid is reported as skipped.

4. **The out-of-grid case is not a launch-time error by design.** `launchSubScene` refuses only a sub-scene whose `scene` is past the grid ([ControlCommandsSessionSubScenes.cpp:159](src/core/ControlCommandsSessionSubScenes.cpp:159)). Columns outside the grid or past the song's tracks are reported in `skipped` and the rest of the sub-scene launches. That matches the comment on `launchableColumns` at [ControlCommandsSessionLaunch.cpp:105-107](src/core/ControlCommandsSessionLaunch.cpp:105): a column with no song track "can never take a track over".

5. **Existing coverage.** `SessionSubSceneTest::aLaunchStartsOnlyItsColumnsWithTheScenesQuantisation` at [tests/src/core/SessionSubSceneTest.cpp:189-194](tests/src/core/SessionSubSceneTest.cpp:189) covers the skip-and-report path for an empty cell and the refusal of a missing sub-scene. It does not construct a column beyond the grid. So the out-of-grid case is covered by the code contract above, not by a test.

Why no regression test: a test for this claim would pass before and after any change, because the behaviour is already correct. Per the procedure, a not-real verdict skips the RED/GREEN step. I did not reorder the check or add a guard. A reorder would be cosmetic and would move no behaviour, so it is not a bug fix. I also did not build the test binaries, since the verdict rests on the code above.

## Commands run

| Step | Command | EXIT |
| --- | --- | --- |
| Locate accessors and callers | `rg -n` over `src/core` for `model.slot`, `launchableColumns`, `launchCells` (run in the worktree) | 0 |
| Read the accessor | `sed -n 245,285p src/core/SessionModel.cpp` | 0 |
| Read the sub-scene path | `sed -n 100,125p`, `215,240p` of `ControlCommandsSessionLaunch.cpp`; `sed -n 130,200p` of `ControlCommandsSessionSubScenes.cpp` | 0 |
| Find existing coverage | `rg -n "subscene_launch|launchSubScene" tests --glob '*.cpp'` | 0 |
| Gates, first try (shared script) | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 1: `PermissionError` creating `<worktree-parent>/zene-qwen-new-p20-16-feature-check`, outside the write-jail. Nothing was checked; output was on stdout only. |
| Gates, via wrapper with logs in LOG FOLDER | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p20-16.r1/feature_check_wrap.py <worktree> > .../logs/001-feature-check-pre-report.log 2>&1` | 0; 8/8 PASS (log `logs/001-feature-check-pre-report.log`, per-gate logs `logs/gates/`) |

The wrapper runs the same `SIMPLE`/`RATCHET` lists and the same `run`/`base_listings`/`key`/`item_file` functions as `feature_check.py` `main()`. The only difference is the log folder. I did not change the shared script.

## Self-check

- **Regression test fails with the change reverted and passes with it.** Not applicable. The verdict is FALSE-POSITIVE, so there is no product change and no test was added. Nothing to revert.
- **Test is deterministic.** Not applicable, no test was added.
- **Fix is at the cause and complete.** Not applicable, no product code changed. Callers and siblings of the read were checked: `launchCells` callers at [ControlCommandsSessionLaunch.cpp:182](src/core/ControlCommandsSessionLaunch.cpp:182) and [ControlCommandsSessionSubScenes.cpp:168](src/core/ControlCommandsSessionSubScenes.cpp:168); the sibling read in `skippedColumns` at [ControlCommandsSessionSubScenes.cpp:146](src/core/ControlCommandsSessionSubScenes.cpp:146); and the scene-launch read at [ControlCommandsSessionLaunch.cpp:317](src/core/ControlCommandsSessionLaunch.cpp:317). All go through the same bounds-checked `SessionModel::slot`.
- **No collateral damage.** Not applicable, no product change; saved data and limits are untouched.
- **FALSE-POSITIVE evidence.** [SessionModel.cpp:253-263](src/core/SessionModel.cpp:253) (bounds guard and shared empty slot) and [ControlCommandsSessionLaunch.cpp:318](src/core/ControlCommandsSessionLaunch.cpp:318) (`track >= launchable` skip), as cited above.
- **`submit_check.py` result: FAIL on one item, the gates.** Its PASS items are: commits on the branch, working tree clean, report present, and the test-presence note. `submit_check.py` runs the shared `feature_check.py`, which exits 1 with `PermissionError` while creating `zene-qwen-new-p20-16-feature-check` beside the worktree (log `logs/004-feature-check-direct-shared-script.log`, and `logs/003-submit-check.log`). That path is outside this lane's write-jail, so I did not create it. The gate logic itself passes: the wrapper, which runs the same gate functions and writes logs into the LOG FOLDER, returns 8/8 PASS (`logs/002-feature-check-post-commit.log`, EXIT 0). This FAIL is an environment blocker, not a gate finding. It needs the shared script's log folder moved into the LOG FOLDER, or a run outside the jail.
