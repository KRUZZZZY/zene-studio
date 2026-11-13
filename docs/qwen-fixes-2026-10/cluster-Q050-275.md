BLOCKED: `tests/run-all-gates.sh` exits 1. Gate 1's full `cmake --build build -j4` is refused by bunny-guard for workers (full builds are the parent's job), the file-length ratchet fails, and gate self-tests fail. The Q050-275 tests and the feature check pass (see below).

# Q050-275 report: named track versions as take-lane comp sources

Logs: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/Q050-275.r2/logs`

## Acceptance

| # | Criterion | Test | Command | Exit | Log |
|---|---|---|---|---|---|
| 1 | Named playlists are created, switched, saved, reloaded and undone through the control socket, including the comp section | `ControlTrackPlaylists` | `ctest -R ControlTrackPlaylists` (from `build/tests`) | 0 | `007-playlists-green2.log` (GREEN); rerun after helper split `021-playlists-green3.log` (exit 0) |
| 2 | A version used as a comp source becomes a take lane, survives switching, is one undo step, refuses bad input | `ArrangeToolsTest::versionBecomesACompLane`, `laneCopySurvivesAVersionSwitch`, `versionToLaneIsOneUndoStep`, `versionToLaneRefusesBadInput`, `compLaneSurvivesRemovingItsVersion`; `ExternalDeviceCardTest::versionsMenuUsesAVersionAsACompLane`; the comp section of `ControlTrackPlaylists` | `ctest -R 'ArrangeTools\|ExternalDeviceCard\|ControlTrackPlaylists\|ReversibilityContract\|TakeLaneComp'` | 0 (6/6) | `015-ctest-green.log` (GREEN); rerun `022-ctest-green3.log` (exit 0, 6/6) |
| 3 | Feature check (complexity ratchet) passes | `feature_check.py` | `feature_check.py <worktree>` | 0 | `017-featcheck.log` exit 1 (CCN 16 in `check_comp_source`); fixed; `018-featcheck2.log` exit 0 |
| 4 | Gates | `tests/run-all-gates.sh` | `quiet-run gates` | 1 | `023-gates2.log`, `025-gates3.log` (see Notes) |

## Design

- Track versions are the named playlists.
- `track.version_to_lane` copies a version's clips into a take lane at that moment. The copies are kept in every stored version, so a version switch keeps the comp consistent. If the track has no lane, it adds base lane 0 first.

## Commands added

- `track.version_to_lane` (socket command; typed errors: blank name -> `invalid_args`, unknown version -> `not_found`).
- `true_inverse` (reversibility class in `ControlReversibilityTableArranger.cpp`).
- Menu entry `Versions > Use as Comp Lane`, which calls the same command.

## Changes in this run

- `tests/control-track-playlists.py`: `check_comp_source` (CCN 16) split into six helpers with the same checks in the same order. No assertion was removed or weakened.
- `tests/run-all-gates.sh` and the baselines were not touched.

## Self-check

**(a) RED/GREEN.** The revert runs were `013-ctest-red.log` (ArrangeToolsTest failed: `versionBecomesACompLane`, `laneCopySurvivesAVersionSwitch`, `versionToLaneIsOneUndoStep`, `versionToLaneRefusesBadInput`, each "no command 'track.version_to_lane'"; ctest "1/3 ... ***Failed") and `005-playlists-red.log` (ControlTrackPlaylists failed: "version_to_lane adds lane A ...", "control.undo removes lane A ...", "comp.select over 0..96 ...", "through a switch to A ...", "after save and reload the comp's lane A ..."). The quiet-run exit codes for those two runs are not in the logs I read; ctest reported failure. The GREEN runs are `007` and `015` ("100% tests passed"), and this run's rerun is `021` and `022`, both exit 0.

**(b) Criterion to test.** See the Acceptance table. Criterion 1 maps to `ControlTrackPlaylists` (exit 0). Criterion 2 maps to the ArrangeTools, ExternalDeviceCard, ControlTrackPlaylists, ReversibilityContract and TakeLaneComp set (6/6, exit 0).

**(c) One undo step.** `versionToLaneIsOneUndoStep` is in the ArrangeTools set that passed (`022`, 6/6). The socket check in `check_comp_source` does `control.undo` (lane removed) then `control.redo` (lane back at the same index); that test passes in `021`. The `true_inverse` class is in `ControlReversibilityTableArranger.cpp`. ReversibilityContract passed in `022`. I did not find "rows=934 true_inverse=460" in the r2 logs, so the count is **not verified in this run**.

**(d) Realtime.** From the design, not re-read in this run: all new code runs on the interface thread, and clip creation runs under `AudioEngine::requestChangesGuard`, as the existing `replaceClips` does. No audio-path code is changed. `TakeLaneCompPlaybackTest` is in the `022` set (6/6, exit 0). Local TSan does not run on this box, so this rests on reasoning.

**(e) Projects.** The socket check for "a project without versions writes no `<track-versions>`" was not in the lines I read, so it is **not verified in this run**. The comp round trip (save, reload, lane A and segment unchanged, versions `B` and `["A","B"]`) is asserted in `check_comp_source` and passes in `021`.

**(f) Portability.** `git diff 20fa43c90b..HEAD -- src include | grep -n -E '\b(small|near|far|interface|min|max)\b|filesystem'` gives one hit: diff line 159, a context line (not added by this branch) with the string literal `"interface state"` in `arrangement.scroll`. No `std::filesystem` is added and no new Windows macro names appear.

**WARN items.**
- README canary: `doc-figures-gate` was not checked as a separate step in this run. The docfig logs (`009`–`011`) predate it.
- Manual: updated in commit `6aae31d0a7` (per its subject).

## Notes on the gate run

- **Gate 1 (ctest):** `cmake --build build -j4` is refused by bunny-guard for workers, so it fails before any test runs. This is an environment limit, not a branch fault.
- **Gate 5 (mutation):** PASS after building the named target `RoutingGraphTest` (`024-buildrgt.log`, exit 0).
- **File-length ratchet:** FAIL "baseline not written". The listed "new" files (e.g. `ProjectContainerSaveTest.cpp`) are not on this branch. `ArrangeToolsTest.cpp` was 659 lines at base and is 834 here, so this branch grew an existing over-500 file by 175 lines. `ControlRegistryGroups.h` is 997 at base and unchanged.
- **Gate self-tests:** FAIL in the file-size and verification-debt self-tests (e.g. "a tracked over-cap file is refused exit 0 (expected 1)"). I did not check these against the base commit.
