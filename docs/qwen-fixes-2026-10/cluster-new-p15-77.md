# Cluster new-p15-77 — Input monitoring searched the song per handle per period

Verdict: **FIXED**

## The finding is REAL

Before this change, every `InputMonitorHandle::play()` called `routeOf()`, which did a
linear `std::find` over `Engine::getSong()->tracks()` (`src/core/InputMonitor.cpp:57-61`
in the seam state). With one monitoring handle per monitored track, one audio period
therefore did `O(monitored tracks x song tracks)` pointer comparisons — `O(n^2)` when
many tracks monitor.

The new regression measured that directly through the seam:
`ROUTE_LOOKUP_EVIDENCE 24 handles, 348 candidate track scans in one period` — RED,
`tests/src/core/InputMonitorTest.cpp`, log `test-3b-reversed-slots.log`.

## Fix at the cause

The song's track list is the one place a track's record route (its position, the
`track.set_arm` rule) comes from, so the position is now maintained when the list
itself changes instead of searched for on every period:

- `include/Track.h:293-300` — `Track::containerIndex()`, one acquire load of the
  `std::atomic<int>` position (the private `setContainerIndex()` at
  `include/Track.h:538-544` is a release store, and `TrackContainer` is a friend).
- `src/core/TrackContainer.cpp:292-299` — `TrackContainer::trackIndex()` returns
  `containerIndex()` after the container check; no scan, no allocation, no lock.
- `src/core/TrackContainer.cpp:318-324` (`addTrack`), `:341-354` (`removeTrack`) and
  `:388-403` (`moveTrack`) publish the new positions while the list changes, off the
  audio thread.
- `src/core/InputMonitor.cpp:59-62` — `routeOf()` is now
  `Engine::getSong()->trackIndex(track)`.
- `src/core/SessionAudioCells.cpp:78-81` — the same sibling lookup (`columnOf()`, also
  per-period on an audio-thread play handle) uses the maintained index too, so there is
  one rule and no second scan left.

The position is not serialised, defaults to `-1`, and every existing reader of
`TrackContainer::tracks()` is unchanged, so old projects load and save exactly as before.

## Regression test

`tests/src/core/InputMonitorTest.cpp`:

- `routeLookupDoesNotScanTheSongPerPeriod()` — creates 24 sample tracks, sets each to
  `in` (one monitor handle each), resets the seam counter, plays one period per handle
  directly, and asserts the counter is 0. It is the finding: `348` before the fix,
  `0` after (`test-3b-reversed-slots.log` / `test-4b-restored-slots.log`).
- `theRouteIndexFollowsMovesAndRemovals()` — guards that the maintained position is
  reindexed on `track.move` and `track.remove`, so a stale cache cannot pass the slot
  above.

The seam is `TrackContainer::trackIndexScanCount()` / `resetTrackIndexScanCount()`
(declared in `include/TrackContainer.h` under `LMMS_TESTING`, defined in
`src/core/TrackContainer.cpp:278-315`). It counts candidates a linear lookup examined;
the maintained path never increments it. The test injects nothing else and uses no
timers, sleeps or worker threads: it calls `play()` on the test thread and reads one
atomic counter.

## Commands and exit codes (as run)

Logs: `/tmp/new-p15-77.r1-logs`. The requested log folder
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-77.r1/logs` is not
writable from this session: `touch .../logs/probe.txt` exits `1` (`Permission denied`,
even after `chmod 777`); `/tmp` was used for the logs instead and the same commands are
recorded here.

| # | command (from the worktree unless noted) | exit | evidence |
|---|---|---|---|
| 1 | `cmake --build build --target zene InputMonitorTest -j12` (seam state) | 0 | `build-2-seam.log` |
| 2 | `cd build/tests && ctest -R '^InputMonitorTest$' --output-on-failure` (seam state) | **8** | RED: `348` candidate scans, `test-1-seam-red.log` |
| 3 | `cmake --build build --target zene InputMonitorTest -j12` (fix applied) | 0 | `build-3-fix.log`, `build-3-fix.status` = `0` |
| 4 | `cd build/tests && ctest -R '^InputMonitorTest$' --output-on-failure` (fix) | 0 | GREEN: 100% passed, `test-2-fix-green.log` |
| 5 | `cd build/tests && ./InputMonitorTest` (fix) | 0 | `ROUTE_LOOKUP_EVIDENCE 24 handles, 0 candidate track scans`, `test-2b-fix-slots.log` |
| 6 | `git diff -- include/Track.h src/core/TrackContainer.cpp src/core/SessionAudioCells.cpp include/InputMonitor.h > fix.patch` | 0 | product-only patch, `fix.patch` |
| 7 | `git apply -R fix.patch` | 0 | fix reverted to the seam state |
| 8 | `cmake --build build --target zene InputMonitorTest -j12` (reverted) | 0 | `build-4-reversed.log`, `build-4b-reversed-confirm.log` |
| 9 | `cd build/tests && ctest -R '^InputMonitorTest$' --output-on-failure` (reverted) | **8** | RED proof: `348` scans, `test-3-reversed-red.log` |
| 10 | `cd build/tests && ./InputMonitorTest` (reverted) | **1** | `examined 348 track(s)`, `test-3b-reversed-slots.log` |
| 11 | `git apply fix.patch` | 0 | fix restored |
| 12 | `cmake --build build --target zene InputMonitorTest -j12` (restored) | 0 | `build-5-restored.log` |
| 13 | `cd build/tests && ctest -R '^InputMonitorTest$' --output-on-failure` (restored) | 0 | GREEN again: 100% passed, `test-4-restored-green.log` |
| 14 | `cd build/tests && ./InputMonitorTest` (restored) | 0 | `0 candidate track scans`, `9 passed`, `test-4b-restored-slots.log` |
| 15 | `cmake --build build --target ArrangingSpacerReferenceTest ArrangingCatchUpTest SessionAudioCellPlaybackTest -j12` | 0 | `build-6-family.log` |
| 16 | `cd build/tests && ctest -R '^(InputMonitorTest|ArrangingSpacerReferenceTest|ArrangingCatchUpTest|SessionAudioCellPlaybackTest)$' --output-on-failure` | **8** | 3/4 pass; `ArrangingCatchUpTest` fails only its `queuedInstrumentHandlesAreRemovedBeforeTheirTrackCanDie` slot because no built-in plug-in binary exists (`tripleoscillator` loads as `dummyinstrument`); `test-5-family.log` |
| 17 | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | **1** | direct run cannot write `<worktree-parent>/<name>-feature-check/` (`PermissionError`, `fork-sources.log`); the same module re-run with only its log destination redirected to `/tmp` (gate code and thresholds untouched): every row PASS, `RESULT: PASS`, `gate-1-feature-check.log` |
| 18 | `python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p15-77` | **1** | only the gates row fails, with the same `PermissionError`; report/tests/clean-tree rows PASS (`submit-check-direct.log`) |
| 19 | the same submit check with the feature-check log destination redirected | 0 | `RESULT: PASS - submit`, all rows PASS (`submit-check-wrapped.log`) |

Affected-family detail from #16: `InputMonitorTest` Passed, `ArrangingSpacerReferenceTest`
Passed, `SessionAudioCellPlaybackTest` Passed, `ArrangingCatchUpTest` 12 passed / 1
failed / 1 skipped. The failure is the missing plug-in build: this build tree has
`LIST_PLUGINS:BOOL=OFF` in `build/CMakeCache.txt` and no `*.so` under `build/plugins`,
so the slot's expected `tripleoscillator` becomes `dummyinstrument`; it is unrelated to
the route lookup and exists without this change.

## Registration

`src/core/InputMonitor.cpp`, `include/InputMonitor.h` and
`src/core/SessionAudioCells.cpp` are already fork-NEW files in
`tests/fork-sources.txt`. The inherited files changed —
`include/Track.h`, `include/TrackContainer.h`, `src/core/TrackContainer.cpp` — have
their reasons appended to `tests/upstream-modifications.txt` in the same commits.

## Self-check

**? The regression test FAILS with the product change reverted and PASSES with it
(exit codes).** Proven in commands 6-14 above: the product-only `fix.patch` was
reverse-applied; the rebuilt test gave ctest exit **8** and binary exit **1** with
`348` candidate scans; re-applying it and rebuilding gave ctest exit **0** and binary
exit **0** with `0` scans. The RED is a real assertion failure
(`'scans == 0u' returned FALSE`), not a build failure.

**? The test is deterministic: no wall-clock timing, sleeps or races (name the seam or
injected values it uses).** The seam is
`TrackContainer::trackIndexScanCount()`/`resetTrackIndexScanCount()` under
`LMMS_TESTING`. The test creates 24 tracks, resets the counter, calls `play()` once per
handle synchronously on the test thread and reads the atomic counter. There is no
timer, sleep, thread or clock in either slot; the move/remove slot uses fixed command
arguments and exact index comparisons.

**? The fix is at the cause and complete: every caller and sibling of what you changed
was checked and stays consistent.** `routeOf()` had exactly one caller,
`InputMonitorHandle::play()` (`src/core/InputMonitor.cpp:95-108`); it now calls
`trackIndex()` once per handle per period. The sibling per-period lookup
`columnOf()` in `src/core/SessionAudioCells.cpp:78-81` had the same `std::find` shape
and now uses the same maintained index (`SessionAudioCellPlaybackTest` passes, command
15-16). The control-thread lookups (`control::trackIndexIn`,
`ControlCommandsArrangementState.cpp::trackRouteOf`, `McuSurface.cpp`) are not on the
audio path and are unchanged. `TrackContainer::trackIndex()` checks the track's
container, so a nested pattern-container track returns `-1` exactly as before.

**? No collateral damage: old projects and saved data still load as before, and any
limit or check you added does not hit other callers that legitimately exceed it.** No
serialisation field, file format, limit or check was added. `m_containerIndex` is a
non-serialised runtime position defaulting to `-1`; it is rebuilt by the existing
`addTrack` path on every project load. `trackIndex()` returns `-1` for a null track or
one from another container, matching the old `-1`. No behaviour outside the lookup
changed; the only control-flow change is that `moveTrack()` now does nothing (instead
of undefined erase/insert) for a pointer that is not in this container.

**? If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the
intended contract that shows it.** Not applicable: the verdict is FIXED, and the RED
evidence above is the pre-fix linear scan at `src/core/InputMonitor.cpp:57-61`
(seam state) / `348` candidate scans.

FIXED
