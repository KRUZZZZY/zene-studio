ALREADY-FIXED

# Cluster 4411: recording allocates and starts a thread from the render path

The finding describes the pre-fix clip record path: `SampleTrack::play()` constructed a
`SampleRecordHandle` inside the render callback, and that constructor built the accumulator
ring, the drain scratch vector, the shared take and started a `std::thread`. On this branch
that construction does not run on the render path any more: the handle is built on the main
thread and the render path only claims it. The render path was verified with the existing
allocation probe, and the pre-fix shape was re-introduced temporarily this session to show
that the probe catches exactly this defect (RED below).

The same defect was already closed under a sibling cluster on this branch; the registration
note for it names this cluster too:
`tests/upstream-modifications.txt:2158` — "`src/tracks/SampleTrack.cpp` docs/m0-blocker-fixes-2026-10-08
(cluster 3508/4411/3506): play() claims the clip's prepared take instead of constructing a
SampleRecordHandle on the audio thread". The earlier report `docs/qwen-fixes-2026-10/cluster-3508.md`
records the same ALREADY-FIXED verdict for the same code.

## Evidence (current tree, read this session)

- `src/tracks/SampleTrack.cpp:293` — the render path calls `st->claimRecordHandle()`; when it
  returns null the pass only counts a fallback (`:297 SampleRecordHandle::countUnpreparedTake()`,
  then `continue`). There is no `new SampleRecordHandle` anywhere on the audio path
  (`new SampleRecordHandle` appears once in the tree, `src/core/SampleClip.cpp:218`).
- `src/core/SampleClip.cpp:190-198` — `claimRecordHandle() noexcept` swaps two raw pointers and
  calls `markClaimed()` (`include/SampleRecordHandle.h:70`, a bool store). No allocation, no
  thread, no lock acquisition beyond the render lock the render thread already holds.
- `src/core/SampleClip.cpp:199-224` — `prepareRecordTake()` is the only constructor site
  (`:218 new SampleRecordHandle(this)`), reached from the arm path (`armRecordTake`, `:251`) and
  the queued `Song::playbackStateChanged` connection (`:74`, `:128`), plus the 10 ms main-thread
  pump (`src/core/SampleRecordHandle.cpp:225-242`, started at `:92`). Its comment at `:199-201`
  states that it always runs on the main thread, where a handle may allocate and start a thread.
- `src/core/SampleRecordHandle.cpp:111-136` — the handle constructor builds the accumulator and
  joins the track's bus; it is documented main-thread-only at `include/SampleRecordHandle.h:46-54`
  and `:55` ("Main thread only: allocates the accumulator and starts its drain thread").
- `include/SampleRecordAccumulator.h:178-187` — the accumulator constructor does the allocation
  (`m_ring`, `m_scratch`, `m_take`, `m_gaps`) and starts `m_drainThread`; that is where the
  finding's "allocates and starts a thread" lives, and it is now reached only from the main
  thread. The audio thread calls only `append()` (`:145-161`), a ring write.
- `src/core/SampleRecordHandle.cpp:140-160` — the destructor (runs on the audio thread when the
  engine removes the handle) only sets an atomic and pushes the take to a lock-free queue; it
  joins nothing and touches no model.

## Regression test (existing, run this session)

`tests/src/core/RecordingRealtimeTest.cpp:642 recorderStartIsPreparedBeforeTheCallbackPath`
(named "Cluster 3508 regression" at `:635-641`). It arms the clip on the main thread, asserts
`recordTakesReady() == 2`, reads `unpreparedTakes()` before the transport rolls, then runs the
`test::tlCountAllocations` probe over eight `processNextBuffer()` calls inside a
`RealtimeScope` and requires `tlAllocationCount == 0` plus an unchanged unprepared-take count.
Deterministic: no sleeps and no wall-clock timing; the engine is driven period by period on the
test thread, so the probe measures exactly the render callback.

RED proof of the test (temporary, product tree restored afterwards): `src/tracks/SampleTrack.cpp`
was edited to the pre-fix shape `handle = new SampleRecordHandle( st );` at the same place, then
reverted with `git checkout -- src/tracks/SampleTrack.cpp` and confirmed byte-identical to the
saved copy. The probe catches `new SampleRecordHandle`, the ring/scratch/take allocations and the
drain thread's state allocation.

## Commands and exit codes (all run this session, logs in the run's log folder)

| # | Command | Exit |
|---|---|---|
| 1 | `cmake --build build --target RecordingRealtimeTest -j4 > logs/build-1.log 2>&1` | 0 |
| 2 | `cd build/tests && ctest -R RecordingRealtimeTest --output-on-failure > logs/test-1.log 2>&1` | 0 (1/1 passed) |
| 3 | temporary edit: `handle = new SampleRecordHandle( st );` in `SampleTrack::play` | — |
| 4 | `cmake --build build --target RecordingRealtimeTest -j4 > logs/build-2-red.log 2>&1` | 0 |
| 5 | `cd build/tests && ctest -R RecordingRealtimeTest --output-on-failure > logs/test-2-red.log 2>&1` | **8** |
| 6 | `git checkout -- src/tracks/SampleTrack.cpp` (restore) | 0, file identical to the saved copy |
| 7 | `cmake --build build --target RecordingRealtimeTest zene -j4 > logs/build-3-green.log 2>&1` | 0 |
| 8 | `cd build/tests && ctest -R RecordingRealtimeTest --output-on-failure > logs/test-3-green.log 2>&1` | 0 (1/1 passed) |
| 9 | `cmake --build build --target RecordingLatencyTest TakeLaneCompPlaybackTest -j4 > logs/build-4-family.log 2>&1` | 0 |
| 10 | `cd build/tests && ctest -R 'RecordingRealtimeTest|RecordingLatencyTest|TakeLaneCompPlaybackTest' --output-on-failure > logs/test-4-family.log 2>&1` | 0 (3/3 passed) |
| 11 | `python3 .../qwen/feature_check.py <worktree> > logs/gates-1.log 2>&1` | 0 (RESULT: PASS) |

RED output (`logs/test-2-red.log:21-25`) with the pre-fix shape restored:

```
FAIL!  : RecordingRealtimeTest::recorderStartIsPreparedBeforeTheCallbackPath() Compared values are not the same
   Actual   (asNumber(counted)): 11
   Expected (asNumber(0))      : 0
FAIL!  : RecordingRealtimeTest::claimedTakeRecordsItsInput() '!clip->isRecord()' returned FALSE. ()
```

GREEN output (`logs/test-3-green.log`): `1/1 Test #237: RecordingRealtimeTest ... Passed 11.63 sec`,
`100% tests passed out of 1`.

## Self-check

1. **Regression test FAILS with the product change reverted and PASSES with it.** Rows 5 and 8
   above: EXIT 8 (11 allocations on the render path, expected 0) versus EXIT 0. The reverted code
   is the finding's pre-fix shape, applied temporarily and restored (row 6); no product file in
   this run's diff.
2. **Deterministic.** No sleeps, no wall-clock timing, no second thread in the assertion: the test
   arms the clip and then drives `Song::processNextBuffer()` a fixed eight times on the test
   thread inside a `RealtimeScope`, with the `test::tlCountAllocations` probe as the seam. The
   `QTRY_VERIFY_WITH_TIMEOUT` calls after the probe only wait for the main-thread reaper, which is
   not what the assertion measures.
3. **Fix at the cause / complete.** The constructor site is a single function
   (`SampleClip.cpp:218`), reachable only from main-thread paths (`armRecordTake` at `:251`, the
   queued `playbackStateChanged` connections at `:74`/`:128`, and the 10 ms `pumpTakes` timer).
   The sibling audio-thread seams were checked and are allocation-free: `claimRecordHandle`
   (`:190-198`), the handle destructor (`SampleRecordHandle.cpp:140-160`), `writeBuffer` →
   `SampleRecordAccumulator::append` (`include/SampleRecordAccumulator.h:145-161`), and the
   fallback when no take was prepared (`SampleTrack.cpp:294-299`). The second producer of takes,
   `SessionAudioRecorder::arm`, constructs on a control-thread call, not the render path.
4. **No collateral damage.** No product file changed in this run, so old projects, saved data and
   existing behaviours are untouched; no limit or check was added. The wider recording family
   (rows 9-10) still passes: `RecordingRealtimeTest`, `RecordingLatencyTest`,
   `TakeLaneCompPlaybackTest` → 3/3.
5. **ALREADY-FIXED evidence.** `src/tracks/SampleTrack.cpp:293` (render path claims, never
   constructs), `src/core/SampleClip.cpp:190-198` and `:199-224` (the claim seam and the single
   main-thread construction site), `include/SampleRecordAccumulator.h:178-187` (where the
   allocations and the `std::thread` live), plus the registration note at
   `tests/upstream-modifications.txt:2158` naming cluster 4411.
6. **Sibling commands.** This run's diff against the merge base is the report only (`git diff
   --name-only` on the branch tip minus the previous commits = `docs/qwen-fixes-2026-10/cluster-4411.md`);
   no product file, command file, registry table or commands snapshot is touched, so `clip_record.*`,
   `take.*` and every record command behave exactly as before. The siblings the check lists (`app.about`,
   `app.ask`, `app.help`, `app.online_help`, `app.task_cancel`, `app.tasks`, `app.tour`, `app.tours`,
   `app.update_check_get`, `app.update_check_run`, `app.update_check_set`, `app.version`, `app.whats_new`,
   `arrangement.fit_tracks`, `arrangement.get_state`) come from earlier commits already on
   `bugs/qwen-4411`, not from this run: none of them is affected by, or needs a change for, cluster 4411.
   No control command was added (the ControlCommandsSnapshot canary is unchanged).

## Other findings

- The render path still allocates one `SamplePlayHandle` per normally scheduled clip
  (`src/tracks/SampleTrack.cpp:305`, the non-record branch). That is a different defect (play
  handle construction, not the record take) and is out of scope here; the muted-clip variant is
  already measured by `RecordingRealtimeTest::mutedSampleClipSchedulingDoesNotAllocate`.
- Local TSan does not run on this box, so the race reasoning above is design-level: the prepared
  and spare handle slots are written under `AudioEngine::requestChangesGuard()` and read by the
  render pass that holds the same lock.
