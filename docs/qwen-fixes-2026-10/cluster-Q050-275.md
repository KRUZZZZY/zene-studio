BLOCKED: every Q050-275 acceptance test passes, but `tests/run-all-gates.sh` exits 1. Gate 1 cannot run a full build as a worker (bunny-guard exit 97), gates 13-15 hit bunny-guard refusals, and gate 11's self-test 2 is not confirmed as environmental. Gate 7 (file-length) fails only on pre-existing regressions. See Gates below.

# Q050-275 report: named track versions as take-lane comp sources

Logs: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/Q050-275.r2/logs`

## Acceptance

| # | Criterion | Test | Command | Exit | Log |
|---|---|---|---|---|---|
| 1 | Named playlists are created, switched, saved, reloaded and undone through the control socket, including the comp section | `ControlTrackPlaylists` | `ctest -R ControlTrackPlaylists -V` (from `build/tests`) | 0 | `040-playlists-v.log` (rerun after helper split and test move) |
| 2 | A version used as a comp source becomes a take lane, survives switching, is one undo step, refuses bad input | `TrackVersionLanesTest::versionBecomesACompLane`, `laneCopySurvivesAVersionSwitch`, `versionToLaneIsOneUndoStep`, `versionToLaneRefusesBadInput`, `compLaneSurvivesRemovingItsVersion` (moved from `ArrangeToolsTest` in step 2) | `ctest -R TrackVersionLanes` | 0 | `033-tvl-run.log` (post-move run, exit 0) |
| 2b | The same slots, plus the rest of the comp set, before the move | `ArrangeTools`, `ExternalDeviceCard`, `ControlTrackPlaylists`, `ReversibilityContract`, `TakeLaneComp` | `ctest -R 'ArrangeTools\|ExternalDeviceCard\|ControlTrackPlaylists\|ReversibilityContract\|TakeLaneComp'` | 0 (6/6) | `022-ctest-green3.log` (run before the move) |
| 3 | Feature check (complexity ratchet) passes | `feature_check.py` | `feature_check.py <worktree>` | 0 | `017-featcheck.log` exit 1 (CCN 16 in `check_comp_source`); fixed; `018-featcheck2.log` exit 0 |
| 4 | Gates | `tests/run-all-gates.sh` | `quiet-run gates` | 1 | `025-gates3.log` (see Gates) |

## Design

- Track versions are the named playlists.
- `track.version_to_lane` copies a version's clips into a take lane at that moment. The copies are kept in every stored version, so a version switch keeps the comp consistent. If the track has no lane, it adds base lane 0 first.

## Commands added

- `track.version_to_lane` (socket command; typed errors: blank name -> `invalid_args`, unknown version -> `not_found`).
- `true_inverse` (reversibility class in `ControlReversibilityTableArranger.cpp`).
- Menu entry `Versions > Use as Comp Lane`, which calls the same command.

## Changes in this run

- `tests/control-track-playlists.py`: `check_comp_source` (CCN 16) split into six helpers with the same checks in the same order. No assertion was removed or weakened.
- `tests/src/core/TrackVersionLanesTest.cpp` (new, about 248 lines): the five version-lane slots and their helpers moved out of `tests/src/core/ArrangeToolsTest.cpp`. Assertions are unchanged. Registered in `tests/CMakeLists.txt`, `tests/fork-sources.txt` and `tests/all-sources.txt`.
- `tests/src/core/ArrangeToolsTest.cpp`: restored to base (`20fa43c90b`). It has no diff on this branch.
- `docs/050-features/Q050-275.md`: acceptance rows now name `TrackVersionLanesTest::`.
- `tests/run-all-gates.sh` and the baselines were not touched.

## Self-check

**(a) RED/GREEN.** Revert runs: `013-ctest-red.log` (exit 8, the five slots failed with "no command 'track.version_to_lane'") and `005-playlists-red.log` (exit 8, ControlTrackPlaylists failed on the lane, undo, comp and save/reload checks). For the moved file: `037-red2-run.log` (exit 8, five failing slots, with the base `ControlCommandsTrackVersions.cpp` in place) and `039-green2-run.log` (exit 0, after restoring HEAD). GREEN for the playlists: `040-playlists-v.log` (exit 0).

**(b) Criterion to test.** See the Acceptance table. Criterion 1 maps to `ControlTrackPlaylists` (exit 0). Criterion 2 maps to `TrackVersionLanesTest` (exit 0 after the move).

**(c) One undo step.** `versionToLaneIsOneUndoStep` asserts that one `control.undo` after `track.version_to_lane` leaves `laneCount()==0` and the Set equal to its state before. It passes in `033-tvl-run.log` (exit 0). `ReversibilityContract` passes in `022-ctest-green3.log` (`Test #230 Passed`, 6/6 exit 0). The A16 counts `rows=934 true_inverse=460` are quoted from `docs/RELEASE-NOTES-v0.3.0-alpha.md:1129`. They were not re-measured in this run.

**(d) Realtime.** From the design, not re-read in this run: all new code runs on the interface thread, and clip creation runs under `AudioEngine::requestChangesGuard`, as the existing `replaceClips` does. No audio-path code is changed. `TakeLaneCompPlaybackTest` is in the `022` set (6/6, exit 0). Local TSan does not run on this box, so this rests on reasoning.

**(e) Projects.** A project with no versions writes no `<track-versions>` element: `040-playlists-v.log` line 841 reports `ok`, and `ctest -R ControlTrackPlaylists -V` exits 0. The comp round trip (save, reload, lane A and segment unchanged, versions `B` and `["A","B"]`) is asserted in `check_comp_source` and passes in `040`.

**(f) Portability.** `git diff 20fa43c90b..HEAD -- src include | grep -n -E '\b(small|near|far|interface|min|max)\b|filesystem'` gives one hit: a context line (not added by this branch) with the string literal `"interface state"` in `arrangement.scroll`. No `std::filesystem` is added and no new Windows macro names appear.

**(g) Doc figures.** `doc-figures-gate` exits 0 in `041-docfig2.log`.

**WARN items.**
- Manual: updated in commit `6aae31d0a7` (per its subject).

## Gates

Summary from `025-gates3.log` (lines 9392-9420), with the run's own verdicts:

| Gate | Verdict | Cause |
|---|---|---|
| 1 ctest | FAIL | Environmental. `cmake --build build -j4` is refused for workers by bunny-guard (exit 97), so no test runs in the gate. |
| 4 complexity | PASS | - |
| 5 mutation | PASS | After building the named target `RoutingGraphTest` (`024-buildrgt.log`, exit 0). |
| 7 file-length | FAIL | Pre-existing. `file-length-gate.sh --check` (fork scope) reports 26 regressions (`034-fl2.log`, exit 1). Only `ArrangeToolsTest.cpp` is a branch file. It is identical to base (531 code lines) and absent from the baseline at base, so it is not a new regression. The other 25 are not branch files. The new `TrackVersionLanesTest.cpp` is under the 500-line cap. The tools scope passes (`035-fl2t.log`, exit 0). |
| 11 evidence | FAIL | Self-test 2 ("a tracked over-cap file is refused", exit 0, expected 1) and the git-index self-test (does not name `build/tracked-dump.bin`). The fixture's `git add` is not verified as the cause, so this is **not confirmed as environmental**. |
| 13 scripted-checks | FAIL | Bunny-guard refusals (`git config`, `git submodule add` exit 97; `025-gates3.log` line 9288) and "cannot create the scratch worktree". |
| 14 release-fitness-selftest | FAIL | Same bunny-guard refusals (`git submodule add`, exit 97). |
| 15 verification-debt-selftest | FAIL | Fixture `git -C ... config` and `add -A` refused by bunny-guard. Defects 2 and 3 expect outputs the fixtures could not produce. The files named in defect 3 (`src/core/LatencyCompensation.cpp`, `include/LatencyCompensation.h`) have no diff on this branch. |

**To unblock:** the parent runs the full build (gate 1) outside the worker jail, and confirms gate 11 self-test 2 on the base commit or from an unrestricted run. If gate 11 is environmental, the status can change to IMPLEMENTED with the same evidence.
