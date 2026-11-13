Verdict: FIXED

# Cluster new-p17-24

new-p17-24: FIXED
new-p17-27: FIXED

## Verdict

**FIXED** for both bugs in the batch. Both are REAL on the prior tree.

## Evidence — new-p17-24 (re-arm reuses previous take file)

- Root cause: `src/core/ControlRecordingSupport.cpp:302-303` returned the fixed
  `zene-take-route%1.wav`; `src/core/ControlCommandsRecordingRoutes.cpp:208`
  passed it through; `src/core/audio/TrackRecorder.cpp:166-174` opened it with
  `SFM_WRITE`, truncate-overwriting the earlier take. The sibling default
  `zene-take-trk%1.wav` at `src/core/ControlCommandsArrangementState.cpp:201-204`
  had the same defect for `track.set_arm`.
- Fix (cause, both defaults):
  - `include/ControlRecordingSupport.h` + `src/core/ControlRecordingSupport.cpp`:
    `reserveDefaultTake(directory, base)` reserves the next free name
    (`<base>.wav`, `<base>-1.wav`, … up to `TakeNameProbeBound = 1000`) by
    exclusive creation (`QIODevice::NewOnly`), skipping any candidate whose
    WAV or `.rec-journal` side file exists; `releaseDefaultTake(path)` removes
    only the reservation itself and the journal a failed arm wrote.
    `takePathArg` now reserves instead of returning the fixed name, and refuses
    deterministically on exhaustion.
  - `src/core/ControlCommandsArrangementState.cpp` (`armTrackArm`): the `trk`
    default now reserves the same way and releases its reservation when the arm
    fails.
  - `src/core/ControlCommandsRecordingRoutes.cpp` (`armTrack`): releases the
    reserved default path when `armTrack` returns false.
  - `src/core/audio/TrackRecorder.cpp` (`arm`, writer-start failure path):
    removes the WAV the failed arm opened, so a failed attempt leaves no
    take-file residue. Explicit-file behaviour is unchanged: a caller-named
    file is still opened with truncation as before.
- Allocation is on the control thread only (command handlers + `arm()`); the
  audio-thread entry `processInputInterleaved` is untouched. Verified by
  `RecordingRealtimeTest::routeRecorderCallbackAllocatesNothing` (0 allocations
  under `RealtimeScope`).
- Regression tests (`tests/src/core/ControlRecordingRoutesTest.cpp`):
  `secondDefaultArmDoesNotTruncateFirstTake`,
  `defaultArmSkipsExistingTakeAndJournal`,
  `defaultArmExhaustionRefusesAndLeavesNoResidue`,
  `defaultArmFailureLeavesRouteRetryableAndSentinels`,
  `trackSetArmDefaultSecondTakeIsDistinct`; plus
  `tests/src/core/MultiTrackRecorderTest.cpp::writerStartFailureLeavesRouteRetryable`
  (now also asserts the failed WAV is removed) and
  `tests/src/core/RecordingRealtimeTest.cpp::routeRecorderCallbackAllocatesNothing`.

## Evidence — new-p17-27 (disarm reports journal retirement from prior arm state)

- Root cause: `src/core/ControlCommandsRecordingRoutes.cpp:261` set
  `journal_retired` to `wasArmed` instead of the removal result.
  `src/core/RecordingJournal.cpp:278-280` (`recordingjournal::remove`) returns
  whether the journal is gone; `src/core/audio/TrackRecorder.cpp:318-323`
  ignored it and cleared the path regardless.
- Fix: `include/TrackRecorder.h` + `src/core/audio/TrackRecorder.cpp` —
  `TrackRecorder::disarm()` now returns the removal result and retains
  `m_journalPath` when removal fails (so the failure stays observable).
  `src/core/ControlCommandsRecordingRoutes.cpp` (`disarmTrack`) reports that
  result as `journal_retired`. `track.set_arm armed:false` (same `disarm()`)
  inherits the behaviour; it does not report the field.
- Regression tests (`tests/src/core/ControlRecordingRoutesTest.cpp`):
  `disarmJournalRetirementReflectsRemovalResult` (success case) and
  `disarmJournalRemovalFailureReportsFalse` (deterministic non-removable
  journal: the side file replaced by a directory of the same name, so
  `QFile::remove` fails on every platform; asserts `journal_retired=false`
  and that the journal path is retained).

## Sibling / caller consistency

- Every caller of `TrackRecorder::disarm()` was checked: `MultiTrackRecorder`
  (`disarmAll`, `reconcileInputChannelCapacity`, destructor),
  `TrackRecorder::setInputChannelCapacity`, `setInputChannel`, command
  handlers, and test cleanup structs. All ignore the return value except the
  disarm command, which is the documented reporting surface.

## Commands and exit codes (logs in .../new-p17-24.r2/logs/)

| Step | Command | Log | EXIT |
|---|---|---|---|
| RED (tests before fix) | `cmake --build build --target ControlRecordingRoutesTest MultiTrackRecorderTest RecordingRealtimeTest`; `ctest -R ControlRecordingRoutesTest\|MultiTrackRecorderTest\|RecordingRealtimeTest` | build-1-red.log, test-1-red.log | 2 (build, first attempt), then 8 (6 route slots + 1 multitrack slot FAIL) |
| GREEN | `cmake --build build --target ControlRecordingRoutesTest MultiTrackRecorderTest RecordingRealtimeTest`; `ctest -R …` | build-7-final.log, test-3-green.log | 0 (3/3) |
| Family final | `ctest -R <29 recording-family tests incl. ControlCommandsSnapshot>` | test-19-final-family.log | 0 (29/29) |
| Gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | gates-1.log | 0 (RESULT: PASS, 8/8) |

## RED/GREEN proof (step 4b)

`git diff -- <6 product files> > logs/fix.patch` (256 lines), `git apply -R`,
rebuild (**EXIT=0**, build-18-revert.log):
`./build/tests/ControlRecordingRoutesTest` **EXIT=6** (4 passed, 6 failed:
secondDefaultArm…, defaultArmSkips…, defaultArmExhaustion…, defaultArmFailure…,
disarmJournalRemovalFailure…, trackSetArmDefaultSecondTake… — test-15-revert-direct.log);
`./build/tests/MultiTrackRecorderTest` **EXIT=1** (`writerStartFailure…`,
test-16-revert-direct2.log). `git apply logs/fix.patch`, rebuild **EXIT=0**
(build-19-reapply.log): ControlRecordingRoutesTest **EXIT=0** (10 passed,
test-17-reapply-direct.log); MultiTrackRecorderTest **EXIT=0** (10 passed,
test-18-reapply-direct2.log). Recorded in redo-proof.log.

## Files committed

- `include/ControlRecordingSupport.h`
- `include/TrackRecorder.h`
- `src/core/ControlRecordingSupport.cpp`
- `src/core/audio/TrackRecorder.cpp`
- `src/core/ControlCommandsRecordingRoutes.cpp`
- `src/core/ControlCommandsArrangementState.cpp`
- `tests/src/core/ControlRecordingRoutesTest.cpp`
- `tests/src/core/MultiTrackRecorderTest.cpp`
- `tests/src/core/RecordingRealtimeTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p17-24.md`

## Self-check

### The regression test FAILS with your product change reverted and PASSES with it (exit codes)

Proved by step-19 RED/GREEN (redo-proof.log): reverted rebuild EXIT=0
(build-18-revert.log), then `ControlRecordingRoutesTest` EXIT=6 with 4
passed / 6 failed (`secondDefaultArmDoesNotTruncateFirstTake`,
`defaultArmSkipsExistingTakeAndJournal`,
`defaultArmExhaustionRefusesAndLeavesNoResidue`,
`defaultArmFailureLeavesRouteRetryableAndSentinels`,
`disarmJournalRemovalFailureReportsFalse`,
`trackSetArmDefaultSecondTakeIsDistinct` — test-15-revert-direct.log) and
`MultiTrackRecorderTest` EXIT=1 (`writerStartFailureLeavesRouteRetryable` —
test-16-revert-direct2.log). Reapplied rebuild EXIT=0 (build-19-reapply.log):
`ControlRecordingRoutesTest` EXIT=0 (10 passed, test-17-reapply-direct.log),
`MultiTrackRecorderTest` EXIT=0 (10 passed, test-18-reapply-direct2.log).
The pre-fix run gave the same failures (test-1-red.log, ctest EXIT=8), and
the fixed run is green (test-3-green.log, EXIT=0; 29-test family EXIT=0,
test-19-final-family.log).

### The test is deterministic: no wall-clock timing, sleeps or races (name the seam or injected values it uses)

No sleeps, wall-clock reads or races anywhere in the new slots: takes are
driven by explicit `processInputInterleaved` calls with constant values
(`ControlRecordingRoutesTest::stage(8, 0.5f)` then `-0.25f`); the writer-start
failure uses the existing `TrackRecorder::failNextWriterThreadStartForTesting()`
seam; exhaustion occupies the full `TakeNameProbeBound` (1000) candidate
space by name; the non-removable journal is a directory with the journal's
exact name (`QDir().mkdir(journal)`), which fails `QFile::remove` on Linux,
macOS and Windows alike. Path checks use `QFileInfo` and byte `contents()`
comparisons, with no platform separator assumptions. The realtime slot arms
on the control thread and probes `processInputInterleaved` under
`RealtimeScope` with `AllocationProbe`, asserting 0 allocations.

### The fix is at the cause and complete: every caller and sibling of what you changed was checked and stays consistent

new-p17-24 cause: the fixed default name in `takePathArg`
(`ControlRecordingSupport.cpp:302-303`) and `armTrackArm`
(`ControlCommandsArrangementState.cpp:201-204`). Both producers now reserve
via `reserveDefaultTake`; both release the reservation on arm failure.
Every caller of `TrackRecorder::arm` was audited: `MultiTrackRecorder::armTrack`
(pure delegation, no path logic — unchanged by design), command handlers
(checked, release added), tests (explicit files). Every caller of
`TrackRecorder::disarm()` was audited: `MultiTrackRecorder::disarmAll`,
`MultiTrackRecorder::reconcileInputChannelCapacity`,
`TrackRecorder::setInputChannelCapacity`, the destructor, `track.set_arm`
`armed:false` (`disarmTrackArm`), `record.disarm_track`, `record.disarm_all`
and test `StopRecorderOnExit` structs — only the two new reporting surfaces
consume the return value. No new control commands were added.

### No collateral damage: old projects and saved data still load as before, and any limit or check you added does not hit other callers that legitimately exceed it

The allocator only *skips* existing candidates (WAV or journal present) and
refuses after a bounded 1000-try search — existing takes, journals and
project data are read exactly as before, and no established caller
legitimately needs more than 1000 default names in one recovery directory
(the retry after deleting occupation succeeds, as the exhaustion test
shows). Explicit-file arms are byte-for-byte unchanged. The 29-test
recording family, the `zene` binary rebuild and all gates pass
(test-19-final-family.log, build-12-zene.log, gates-1.log).

### If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the intended contract that shows it

Not applicable — verdict is FIXED.

### Sibling commands in the changed command files (arrangement.* list)

The submit check listed the `arrangement.*` ids registered in the touched
file `src/core/ControlCommandsArrangementState.cpp` (also `track.add` et al.
live nearby, but the listed question is the `arrangement.*` sibling set).
The only change in that file is inside `armTrackArm`, the handler of
`track.set_arm` (and, through it, `track.set_arm_many`, which calls the same
`armTrackArm` semantics per its registration text): `armTrackArm` is the
sole default-path producer for that command family, so `set_arm` and
`set_arm_many` both reserve and release consistently. The other listed
siblings — arrangement.fit_tracks, arrangement.get_state, arrangement.modes,
arrangement.modifier_tools, arrangement.navigator, arrangement.next_transient,
arrangement.nudge, arrangement.ruler_lanes, arrangement.scroll,
arrangement.select_clip, arrangement.set_tool, arrangement.zoom,
arrangement.zoom_back, arrangement.zoom_forward, arrangement.zoom_toggle —
share no code path with `armTrackArm` or `reserveDefaultTake` and are
unaffected by the change (each is its own handler in the same TU; the edit
touches only `armTrackArm`'s take-path computation and its failure return).
`ControlCommandsSnapshot` is unchanged (test-9-snapshot.log, EXIT=0).


## Notes on the diagnostic checkpoint's test list

- Covered above: re-arm distinctness (with WAV read-back), sentinel skip,
  bounded exhaustion, writer-failure retryability (route + multitrack),
  disarm-retirement failure/success, `track.set_arm` default distinctness,
  realtime zero-allocation, concurrent-sharing-one-directory distinctness is
  by construction (exclusive `NewOnly` creation makes the reservation atomic
  across allocator instances; no test slot was added for it).
- Cross-platform (Linux/macOS/Windows, spaces and non-ASCII directory
  segments): this worktree is Linux-only; CI on other platforms must run the
  family. The allocator and the non-removable-journal fixture use no
  POSIX-only primitives (`QIODevice::NewOnly`, `QFile::remove` on a directory).
