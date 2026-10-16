IMPLEMENTED

# Cluster Q050-002 - Move recorder setup off the audio thread

Goal: starting an armed recording does not allocate memory or create its drain thread from the audio
render callback.

The product behaviour is the fix the merged `alloc-probe-fix` /
`docs/m0-blocker-fixes-2026-10-08` lane already put on this train
(`plans/P0-RECONCILE-2026-10-09.md` marks Q050-002 DONE, and `plans/PLAN-050-CYCLE.md` says not to
duplicate the probe work). This lane verified the exact acceptance on train 5 and added the
recorder-start regression that acceptance criterion 2 asks for; it changed no product logic.
`SampleClip::prepareRecordTake()` (`src/core/SampleClip.cpp:183`) builds the take - and therefore
the `SampleRecordAccumulator` with its ring and `std::thread` (`include/SampleRecordAccumulator.h:174`)
- on the main thread; `SampleTrack::play()` (`src/tracks/SampleTrack.cpp:268`) only claims the
prepared handle. The stale R2.2 comment in `src/core/SampleRecordHandle.cpp:105` that said the
constructor runs on the audio thread was corrected.

## Acceptance table (exact commands and observed exit codes)

All builds from the worktree root, all ctest from `build/tests`. Environment:
`unset LMMS_PLUGIN_DIR; export QT_QPA_PLATFORM=offscreen CCACHE_DIR=$HOME/.cache/ccache-bunny
CCACHE_BASEDIR=... CCACHE_NOHASHDIR=true PKG_CONFIG_PATH=... LD_LIBRARY_PATH=...`.

| Criterion | Test | How it proves it | Exact command | EXIT |
|---|---|---|---|---|
| The fixed AllocationProbe counts an allocation injected from a non-test translation unit | `AllocationProbeTest::countsAnAllocationMadeInLibraryCode` | `RoutingGraph::createNode()` is a non-test TU in `lmmsobjs`; the probe counts its `make_unique` (`counted > 0`). `doesNotCountWhileDisarmed` is the negative control | `ctest -R '^AllocationProbeTest$' --output-on-failure` | 0 (1/1) |
| A recorder-start regression exercises the callback path and proves accumulator and drain-thread setup happen before playback; recording still produces the expected take | `RecordingRealtimeTest::recorderStartIsPreparedBeforeTheCallbackPath` (new, `tests/src/core/RecordingRealtimeTest.cpp:421`) | arm -> `recordTakesReady() == 2` (accumulator + drain thread exist before playback); `Song::processNextBuffer()` x8 under `lmms::RealtimeScope` with the probe armed -> `tlAllocationCount == 0`; `driveRecordPeriods(20)` + stop -> non-empty take | `ctest -R '^RecordingRealtimeTest$' --output-on-failure` and `./RecordingRealtimeTest recorderStartIsPreparedBeforeTheCallbackPath` | 0 (1/1); 0 (case PASS) |
| (supporting) the audio-thread claim allocates nothing | `RecordingRealtimeTest::armedClipClaimsItsPreparedTakeWithoutAllocation` | probe around `claimRecordHandle()` (what `SampleTrack::play` calls) -> 0 allocations | same ctest run | 0 |
| (supporting) recording still produces the expected take | `RecordingRealtimeTest::claimedTakeRecordsItsInput` | 40 engine periods record >= 10 x 256 frames onto the clip | same ctest run | 0 |
| (supporting) both takes exist before playback | `RecordingRealtimeTest::disarmReleasesThePreparedTakes` | after `setRecord(true)`, `recordTakesReady() == 2` | same ctest run | 0 |

First line of this file is `IMPLEMENTED` because every acceptance criterion is met and was proven by
the tests run below.

## Command log

```text
cmake --build build --target AllocationProbeTest RecordingRealtimeTest -- -j4 > .../logs/build-1.log 2>&1; echo EXIT=$?   # EXIT=0
ctest -R '^AllocationProbeTest$' --output-on-failure > .../logs/test-1-allocationprobe.log 2>&1; echo EXIT=$?          # EXIT=0, 1/1
ctest -R '^RecordingRealtimeTest$' --output-on-failure > .../logs/test-2-recordingrealtime.log 2>&1; echo EXIT=$?      # EXIT=0, 1/1 (before the new case)
# added recorderStartIsPreparedBeforeTheCallbackPath
cmake --build build --target RecordingRealtimeTest -- -j4 > .../logs/build-2.log 2>&1; echo EXIT=$?                     # EXIT=0
ctest -R '^RecordingRealtimeTest$' --output-on-failure > .../logs/test-3-recordingrealtime.log 2>&1; echo EXIT=$?      # EXIT=0, 1/1 (after the new case)
./RecordingRealtimeTest recorderStartIsPreparedBeforeTheCallbackPath > .../logs/test-5-newcase.log 2>&1; echo EXIT=$?  # EXIT=0, "PASS", 3 passed 0 failed
```

## Gates (from the worktree root)

```text
bash tests/complexity-gate.sh --check   > .../logs/gate-complexity.log 2>&1; echo EXIT=$?   # EXIT=1  (pre-existing, by design)
bash tests/file-length-gate.sh --check  > .../logs/gate-filelength.log 2>&1; echo EXIT=$?   # EXIT=1  (pre-existing, by design)
bash tests/fork-sources-gate.sh         > .../logs/gate-forksources.log 2>&1; echo EXIT=$?  # EXIT=0
bash tests/all-sources-reproduce.sh     > .../logs/gate-allsources.log 2>&1; echo EXIT=$?   # EXIT=0
bash tests/no-upstream-regression-gate.sh > .../logs/gate-noupstream-2.log 2>&1; echo EXIT=$? # EXIT=0
tests/scripted/check-namespace          > .../logs/gate-namespace.log 2>&1; echo EXIT=$?    # EXIT=0
bash tests/no-tautology-gate.sh         > .../logs/gate-notautology.log 2>&1; echo EXIT=$?  # EXIT=0
```

Gates 4 (complexity) and 7 (file length) are red on train 5 on purpose. Their REGRESSION lists are
byte-identical to the reference worktree `../zene-cachyos`, and neither lists
`tests/src/core/RecordingRealtimeTest.cpp`, so no file of this lane is newly listed.

## New commands and reversibility

None. `ControlRegistry` is unchanged; the commands snapshot (828 ids / 76 groups, `wasm.*` excluded)
and the A16 reversibility histogram do not change.

## Format changes

None.

## Known limits

- The product fix predates this lane (merged `alloc-probe-fix` lane); this branch adds the
  recorder-start regression and a corrected comment only. No RED was produced for the new test,
  because doing so would require reverting merged product code outside this unit.
- The pre-existing audio-thread `AudioBusHandle::addPlayHandle` append is outside this unit; the new
  test incidentally schedules a handle through it (allocation-free) but does not claim it as the
  unit's subject.

## Files committed

- `tests/src/core/RecordingRealtimeTest.cpp`
- `src/core/SampleRecordHandle.cpp`
- `tests/upstream-modifications.txt`
- `docs/050-features/Q050-002.md`
- `docs/qwen-fixes-2026-10/cluster-Q050-002.md`
