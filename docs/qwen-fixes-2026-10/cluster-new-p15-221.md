FIXED
new-p15-221: FIXED
new-p14-439: FIXED

## Decision and evidence

Both findings are REAL and share the same cause: zero represented both a valid clip ID and “no take group.” `Clip` now stores `-1` for no group, retains `0` as a valid group ID, and exposes `hasTakeGroup()` / `belongsToTakeGroup()` for membership checks. `takeGroupId()` still returns zero for an ordinary clip, preserving its previous getter result.

- **new-p15-221:** `src/core/TakeLane.cpp:285` (`recordTake`) and `:726` (`loadTakeGroup`) now reject only negative group IDs. Group membership checks use `Clip::belongsToTakeGroup`, so ordinary clips do not join group zero. `src/core/Clip.cpp:466,725-727` saves and restores an explicit `takegroup="0"`.
- **new-p14-439:** `include/Clip.h:198-210,393` uses `-1` internally for no group and accepts zero in `setTakeGroupId`. `src/core/SamplePlayHandle.cpp:121,386` and `src/tracks/SampleTrack.cpp:128` pass group zero through the existing playback gate. `src/core/ControlCommandsCompGroup.cpp:336-339` accepts a group-zero clip for the existing take-select command.

## Test matrix

| Path | Coverage | Result |
|---|---|---|
| Normal recording and persistence | `TakeLaneTest::zeroIdTakeGroupCanBeRecorded` records group zero and reloads the track; `zeroIdTakeGroupLoadsFromSavedState` loads a stored group zero. | Passed |
| Playback and ordinary clips | `TakeLaneCompPlaybackTest::aZeroIdTakeGroupIsDistinctFromAnOrdinaryClip` gates group zero when its lane is inactive, keeps an ordinary clip audible, and probes render allocations. | Passed; zero render allocations |
| Invalid IDs | The record regression verifies group `-1` is rejected. The group loader still rejects negative IDs and negative active lanes. | Passed |
| Existing IDs and project state | Existing positive-ID round-trip and untouched-clip serialization cases in `TakeLaneTest` pass. `TakeLaneCompTest` passes. | Passed |
| Reconfiguration and clearing | The group-zero track save/load round-trip passes. `RangeEditsTest` and `TrackViewDeferredLifetimeTest` pass with explicit group clearing on paste. | Passed |
| Exhaustion | The change adds no capacity, limit, or resource allocation path, so there is no new exhaustion case. | Not applicable |
| Realtime constraints | The render regression uses a fixed 512-frame buffer under `RealtimeScope` and `AllocationProbe`. The changed render paths perform scalar membership checks and use the existing atomic active-lane mirror. | Passed; no new allocation, free, lock, wait, I/O, log, or signal |
| Platform build | Built the configured Linux C++20 / Qt build, including `zene` and all affected test targets. The implementation uses integer checks and Qt DOM APIs available in Qt 5.15. Windows/MSVC and macOS builds were not available in this worktree. | Linux build passed |

## RED/GREEN proof and commands

Commands below ran from the worktree root unless a working directory is stated. Build and test output was redirected to the numbered files in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-221.r1/logs`.

| Bug | RED command and exit | GREEN command and exit | CTest family count |
|---|---|---|---|
| new-p15-221 | From `build/tests`: `ctest -R '^TakeLaneTest$' --output-on-failure` — EXIT 8; `zeroIdTakeGroupCanBeRecorded` returned false and `zeroIdTakeGroupLoadsFromSavedState` did not find group zero. Logs `002-red-takelane.log`, `009-red-revert-takelane.log`. | From `build/tests`: same command — EXIT 0. Logs `006-green-takelane.log`, `012-green-restore-takelane.log`. | 1 registered CTest test; passed 1/1. |
| new-p14-439 | From `build/tests`: `ctest -R '^TakeLaneCompPlaybackTest$' --output-on-failure` — EXIT 8; `aZeroIdTakeGroupIsDistinctFromAnOrdinaryClip` heard the inactive group-zero take. Logs `003-red-playback.log`, `010-red-revert-playback.log`. | From `build/tests`: same command — EXIT 0. Logs `007-green-playback.log`, `013-green-restore-playback.log`. | 1 registered CTest test; passed 1/1. |

The production patch removal/restoration proof used these exact commands and exits:

- `git diff -- include/Clip.h include/SamplePlayHandle.h src/core/Clip.cpp src/core/ControlCommandsCompGroup.cpp src/core/ControlCommandsRange.cpp src/core/ControlCompSupport.cpp src/core/SamplePlayHandle.cpp src/core/TakeLane.cpp src/core/Track.cpp src/gui/tracks/TrackContentWidget.cpp src/tracks/SampleTrack.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-221.r1/logs/fix.patch` — EXIT 0.
- `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-221.r1/logs/fix.patch` — EXIT 0.
- `cmake --build build --target zene TakeLaneTest TakeLaneCompPlaybackTest --parallel 4` with the production patch removed — EXIT 0; log `008-red-revert-build.log`.
- Both RED ctest commands above then exited 8 with the targeted regression assertions failing.
- `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-221.r1/logs/fix.patch` — EXIT 0.
- `cmake --build build --target zene TakeLaneTest TakeLaneCompPlaybackTest --parallel 4` with the patch restored — EXIT 0; log `011-green-restore-build.log`.
- Both GREEN ctest commands above then exited 0.

Additional affected-family and paste-path verification:

- `cmake --build build --target zene TakeLaneCompTest --parallel 4` — EXIT 0; `ctest -R '^TakeLaneCompTest$' --output-on-failure` from `build/tests` — EXIT 0, 1/1. Logs `017-build-comp-test.log`, `018-ctest-comp-test.log`.
- `cmake --build build --target zene RangeEditsTest TrackViewDeferredLifetimeTest --parallel 4` — EXIT 0; `ctest -R '^(RangeEditsTest|TrackViewDeferredLifetimeTest)$' --output-on-failure` from `build/tests` — EXIT 0, 2/2. Logs `019-build-paste-tests.log`, `020-ctest-paste-tests.log`.
- Initial regression build, `cmake --build build --target zene TakeLaneTest TakeLaneCompPlaybackTest`, exited 0 before running the RED tests (`001-red-build.log`) and again for the first GREEN run (`005-green-build-targets.log`).
- `cmake --build build` exited 97 because the workspace bunny guard requires named build targets; the exact response is in `004-green-build-all.log`. The named-target builds above passed.

## Gates

The first `feature_check.py` run exited 1 (`014-feature-check.log`): `fork-sources` and `all-sources-reproduce` both found that `tests/all-sources.txt` contained 1,148 vendored sfizz paths excluded by its own header recipe. I regenerated only that manifest's entry list using the documented recipe:

`{ git ls-files '*.c' '*.cc' '*.cpp' '*.cxx' '*.h' '*.hpp' | grep -vE '^(src/3rdparty/|tests/reference/|plugins/NeuralAmp/(rtneural|nam|tests)/|plugins/RnnoiseDenoiser/rnnoise/|plugins/SfzPlayer/sfizz/)' ; git ls-files tests/control-detect-commands.py tests/control_detect_fixtures.py ; } | LC_ALL=C sort -u > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-221.r1/logs/015-all-sources-generated.txt` — EXIT 0. No gate code or limits changed.

The subsequent `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p15-221` exited 0 (`016-feature-check-after-manifest.log`):

| Gate | Result |
|---|---|
| fork-sources | PASS |
| all-sources-reproduce | PASS |
| no-upstream-regression | PASS |
| check-namespace | PASS |
| no-tautology | PASS |
| doc-figures | PASS |
| complexity | PASS — no new item in changed files |
| file-length | PASS — no new item in changed files |

The same command was run again after this report was first written; it exited 0 with the same table (`021-feature-check-final.log`).

## Completion check

The first `submit_check.py` run exited 1 (`023-submit-check.log`) because it requires a bare overall verdict word on line one. The report now begins with `FIXED`, followed by the required per-bug lines. `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p15-221 new-p15-221` then exited 0 (`025-submit-check-after-report-fix.log`): `RESULT: PASS - submit`.

## Self-check

- **RED/GREEN:** `TakeLaneTest` and `TakeLaneCompPlaybackTest` each failed with the production patch removed (CTest EXIT 8) and passed after restoration (CTest EXIT 0). The reversed patch is saved as `fix.patch`; builds and ctest logs are listed above.
- **Determinism:** Tests set group IDs, lanes, and active-lane mirrors directly and load a fixed DOM element. Playback uses a fixed-size stack buffer. There are no sleeps, wall-clock timestamps, or races.
- **Cause and callers:** The shared cause was zero used as the no-group marker. I checked all `takeGroupId()` call sites, clip save/load, take matching, playback, removal, state export, paste clearing, and the negative-ID paths. `comp.take_select` now checks explicit membership. Its sibling commands `comp.assign`, `comp.audition`, `comp.cycle`, `comp.get_state`, `comp.lane_add`, `comp.lane_list`, `comp.lane_remove`, `comp.move_edge`, `comp.play_all_takes`, `comp.rebuild`, `comp.select`, `comp.select_group`, and `comp.split_passes` do not use a clip group ID as a sentinel. The changed range-command file also registers `arrangement.modes`; it manages arrangement mode settings independently of clip group membership. The asked siblings `arrangement.fit_tracks`, `arrangement.get_state`, `arrangement.modifier_tools`, `arrangement.navigator`, `arrangement.next_transient`, `arrangement.nudge`, `arrangement.ruler_lanes`, `arrangement.scroll`, `arrangement.select_clip`, `arrangement.set_tool`, `arrangement.zoom`, `arrangement.zoom_back`, `arrangement.zoom_forward`, and `arrangement.zoom_toggle` do not read or write clip take-group IDs. No command was added; `comp.take_select` retains its existing single `Track::addJournalCheckPoint` path.
- **Saved data and collateral behavior:** An absent or invalid `takegroup` attribute restores the ordinary `-1` state, positive group IDs retain their existing values, and the public getter still returns zero for ordinary clips. Ordinary clips still omit the `takegroup` attribute. Explicit group zero now round-trips. No limits were added.
- **Verdict evidence:** Both findings are REAL; code evidence is listed above. There is no ALREADY-FIXED or FALSE-POSITIVE claim.

## Files in the fix commit

`include/Clip.h`, `include/SamplePlayHandle.h`, `src/core/Clip.cpp`, `src/core/ControlCommandsCompGroup.cpp`, `src/core/ControlCommandsRange.cpp`, `src/core/ControlCompSupport.cpp`, `src/core/SamplePlayHandle.cpp`, `src/core/TakeLane.cpp`, `src/core/Track.cpp`, `src/gui/tracks/TrackContentWidget.cpp`, `src/tracks/SampleTrack.cpp`, `tests/src/core/TakeLaneTest.cpp`, `tests/src/tracks/TakeLaneCompPlaybackTest.cpp`, `tests/all-sources.txt`, `tests/upstream-modifications.txt`, and `docs/qwen-fixes-2026-10/cluster-new-p15-221.md`.
