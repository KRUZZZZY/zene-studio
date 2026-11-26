FALSE-POSITIVE

Cluster `new-p9-201`: "Persistent input-monitor handle keeps an unguarded track pointer"
(`include/InputMonitor.h`:65; `src/core/InputMonitor.cpp`:95-106 as the patrol saw them;
today `include/InputMonitor.h`:70-88 and `src/core/InputMonitor.cpp`:101-113).

## What the finding claims, and what the code does

The patrol's premise is explicit: "This slice does not show track destruction removing or
quiescing that handle." The destruction path is there, it is the first thing `~SampleTrack`
does, and it runs under the engine's model-change guard:

- `src/tracks/SampleTrack.cpp:73-77` — `SampleTrack::~SampleTrack()` calls
  `Engine::audioEngine()->removePlayHandlesOfTypes(this, PlayHandle::Type::SamplePlayHandle
  | PlayHandle::Type::InputMonitorHandle | PlayHandle::Type::SessionCellHandle)`.
  The destructor body runs before the track's members (its `AudioBusHandle`, which the
  handle stores) are destroyed, so the bus is still valid when the handle unregisters from it.
- `src/core/AudioEngine.cpp:1902-1979` — `removePlayHandlesOfTypes` opens with
  `requestChangeInModel()` and closes with `doneChangeInModel()`, and sweeps **all three**
  places a handle can live: `m_playHandles` (1905-1918), the staging list `m_newPlayHandles`
  (1925-1954, the sweep the code comment says was added for exactly this "renders through its
  FREED track" class) and `m_capacityDeferredPlayHandles` (1960-1976). Each match is removed
  from its `AudioBusHandle` first and then disposed (`AudioEngine.cpp:453-469`).
- `src/core/AudioEngine.cpp:1983-2006` — `requestChangeInModel()` takes `m_changeMutex`; the
  render owner holds that lock "for the whole period" (`AudioEngine.cpp:1544`), so a delete
  cannot interleave with a period that is playing the handle.
- `src/core/InputMonitor.cpp:101-113` — `InputMonitorHandle::play()` is the only dereference of
  `m_track`; it reads `monitorMode()`, `isPlaying()` and `Engine::getSong()->trackIndex()`, all
  of which are valid for exactly as long as the destructor above lets the handle live.
- `src/core/Engine.cpp:114` — `Engine::destroy()` runs `s_song->clearProject()` (which deletes
  every track and therefore every monitor handle) before `deleteHelper(&s_audioEngine)`
  (`:121`), so the destructor's `Engine::audioEngine()` is live even at shutdown.
- The other half of the finding ("or undone") is equally covered: undoing `track.set_monitor`
  goes through the same setter into `SampleTrack::monitorModeChanged()`
  (`src/tracks/SampleTrack.cpp:84-104`), and undoing `track.remove` recreates the track with
  `Track::create` — the dead track's handle was already swept by its destructor.

`git log -S"InputMonitorHandle" -- src/tracks/SampleTrack.cpp` returns one commit,
`58ee8b03f7 wip: input monitoring (R2.1), not yet built`: the term was in the destructor when
the feature landed and was never added later as a fix, so this is a false premise, not a
branch that has since been repaired.

## Test matrix (before editing)

| path | what covers it | result this session |
|---|---|---|
| normal: a monitored track is deleted while its handle is registered | new slot `InputMonitorTest::deletingAMonitoredTrackRemovesItsHandle` (tests/src/core/InputMonitorTest.cpp:303-332) | PASS with the branch's code; FAIL when the destructor's `InputMonitorHandle` term is removed (red/green below) |
| failure: the mode goes Off (handle removed, not the track) | `InputMonitorTest::inPassesTheInputAndOffDoesNot` (undo included), `ArrangingCatchUpTest::switchingMonitoringOffRemovesAHandleQueuedInTheSamePeriod` (tests/src/core/ArrangingCatchUpTest.cpp:329-345) | PASS (`InputMonitorTest` EXIT=0 in test-7.log; the ArrangingCatchUp slot PASSED in test-4.log) |
| exhaustion: `addPlayHandle` refuses the monitor handle | `src/tracks/SampleTrack.cpp:95-103` deletes the handle and clears `m_monitorHandle`; `AudioEngine::addPlayHandle` returns false without taking ownership for this type (`src/core/AudioEngine.cpp:1778-1781`) | read, not exercised — no test injects a full staging list |
| reconfiguration: undo/redo of the mode and of the delete | same two slots as the failure row | PASS |
| audio thread | `play()` is allocation-free and lock-free (atomics, `trackIndex`, a `std::fill` over the caller's span); no product file changed in this report | unchanged |
| platform | the change is one test slot; it uses QtTest and the public `AudioEngine::requestChangesGuard()` / `playHandles()` seam, no Qt-6-only API | `feature_check.py` PASS |

## Red / green, with the exact commands and exit codes

The branch's product code is correct, so there is no product diff to reverse. The red run is
the inverse demonstration: delete the one term the finding says is missing from the destructor
(`src/tracks/SampleTrack.cpp:75-76`), rebuild, and the new slot fails; restore it, rebuild,
and it passes. All logs are in
`/home/kruzzzzy/.config/kads/zene/state/runs/new-p9-201.r1/logs/`.

RED (destructor term removed):

```
cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p9-201 \
  && cmake --build build --target InputMonitorTest -j4 > .../logs/build-4.log 2>&1
EXIT=0                                    (build-4.log)

cd build/tests && ctest -R InputMonitorTest --output-on-failure > .../logs/test-6.log 2>&1
EXIT=8                                    (test-6.log)
1/1 Test #297: InputMonitorTest .................***Failed
DELETION_EVIDENCE the monitor handle SURVIVED in the render list after track.remove
FAIL!  : InputMonitorTest::deletingAMonitoredTrackRemovesItsHandle() '!survived' returned
          FALSE. (the monitor handle outlived its track in the render list)
0% tests passed, 1 tests failed out of 1
```

GREEN (the branch's destructor, restored — `git status --porcelain` at that point listed only
`tests/src/core/InputMonitorTest.cpp`, see logs/status-after-restore.log):

```
cmake --build build --target InputMonitorTest -j4 > .../logs/build-5.log 2>&1
EXIT=0                                    (build-5.log)

cd build/tests && ctest -R InputMonitorTest --output-on-failure > .../logs/test-7.log 2>&1
EXIT=0                                    (test-7.log)
1/1 Test #297: InputMonitorTest .................   Passed    1.80 sec
100% tests passed out of 1
```

Corroborating runs made this session, same worktree:

```
cmake --build build --target InputMonitorTest -j4                 EXIT=0  (build-1.log)
ctest -R InputMonitorTest --output-on-failure                     EXIT=0  (test-1.log)
./InputMonitorTest zzProbeDeletingAMonitoredTrackRemovesItsHandle EXIT=0  (test-2.log)
  DELETION_EVIDENCE the monitor handle was removed from the render list after track.remove
cmake --build build --target InputMonitorTest ArrangingCatchUpTest -j4  EXIT=0  (build-3.log)
ctest -R 'InputMonitorTest|ArrangingCatchUpTest' --output-on-failure    EXIT=8  (test-4.log)
  InputMonitorTest ................... Passed
  ArrangingCatchUpTest ............... ***Failed   (two environment failures, see Other findings)
  PASS : ArrangingCatchUpTest::switchingMonitoringOffRemovesAHandleQueuedInTheSamePeriod()
ctest -R InputMonitorTest --output-on-failure -V                  EXIT=0  (test-5.log)
  DELETION_EVIDENCE the monitor handle was removed from the render list after track.remove
python3 .../qwen/feature_check.py <worktree>                      EXIT=0  (gates-1.log)
```

`feature_check.py` (gates-1.log):

```
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
EXIT=0
```

## What changed

`tests/src/core/InputMonitorTest.cpp` only: one slot,
`deletingAMonitoredTrackRemovesItsHandle`, which deletes a monitoring sample track through the
real `track.remove` command and then gives the engine one period
(`AudioEngine::renderNextPeriod()`) before asserting the handle is not in
`AudioEngine::playHandles()`. The synchronization seam is
`AudioEngine::requestChangesGuard()` (`include/AudioEngine.h:661`), so the device cannot begin
a period between the handle's registration and the delete: no sleep, no timing, no race. No
product file changed; no command, save format, undo step or audio-path code was touched.

The two pre-existing slots that remove a monitor handle (`inPassesTheInputAndOffDoesNot`,
`switchingMonitoringOffRemovesAHandleQueuedInTheSamePeriod`) and the new one all pass.

## Self-check

- **The regression test FAILS with your product change reverted and PASSES with it (exit
  codes).** There is no product change to revert (verdict FALSE-POSITIVE): the branch's code is
  what I tested. I proved the test can see the defect instead by removing the single term the
  finding says is absent: `build-4.log` EXIT=0, `test-6.log` EXIT=8 with
  `DELETION_EVIDENCE ... SURVIVED in the render list`; restored, `build-5.log` EXIT=0,
  `test-7.log` EXIT=0.
- **The test is deterministic.** It injects no timestamps and sleeps nowhere: the seam is
  `requestChangesGuard()` (the engine's own model-change lock, which the render owner holds for
  a whole period, `AudioEngine.cpp:1544`), so between `track.set_monitor` and `track.remove`
  the audio device cannot run a period, and the single `renderNextPeriod()` after the delete is
  the period under test.
- **The fix is at the cause and complete.** No fix was needed. The callers and siblings were
  walked: `~SampleTrack` sweeps `SamplePlayHandle`, `InputMonitorHandle` and `SessionCellHandle`
  together; `monitorModeChanged` removes the same type on Off; `AudioEngine::clearInternal`
  keeps the monitor handle on stop (`AudioEngine.cpp:1367-1398`), matching the track-owned
  `m_monitorHandle`; `removePlayHandlesOfTypes` covers the active, staging and capacity-deferred
  lists, so a handle queued in the same period is swept too.
- **No collateral damage.** No product file, command, save attribute or limit changed; the test
  file's existing slots are untouched and pass (`test-7.log`).
- **If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the intended
  contract that shows it.** `src/tracks/SampleTrack.cpp:73-77` (destructor sweep),
  `src/core/AudioEngine.cpp:1902-1979` (the guard and the three lists),
  `src/core/AudioEngine.cpp:1983-2006` and `:1544` (the guard excludes the render period),
  `src/core/Engine.cpp:114` vs `:121` (project cleared before the engine dies),
  `src/core/InputMonitor.cpp:101-113` (the only dereference), and
  `git log -S"InputMonitorHandle" -- src/tracks/SampleTrack.cpp` →
  `58ee8b03f7 wip: input monitoring (R2.1)` (the guard shipped with the feature).

## Completion check

```
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p9-201
EXIT=0                                    (logs/submit-1.log)

  PASS  commits on the branch
  PASS  working tree clean
  PASS  report docs/qwen-fixes-2026-10/cluster-new-p9-201.md
  PASS  tests added or changed - claim-only closure: independent QA must verify its cited code/contract
  PASS  gates (feature_check.py)
RESULT: PASS - submit
```

`feature_check.py` on the committed tree: all eight rows PASS, `RESULT: PASS`, EXIT=0
(logs/gates-2.log).

## Other findings

- `ArrangingCatchUpTest` fails in this build tree for environment reasons only:
  `newDocumentsCarryTheirTypesTemplate` cannot write under `~/.cache` ("the mastering template
  could not be written to /home/kruzzzzy/.cache/ArrangingCatchUpTest/..."), and
  `queuedInstrumentHandlesAreRemovedBeforeTheirTrackCanDie` finds no built-in plugins ("The
  plugin \"tripleoscillator\" wasn't found"; `LMMS_PLUGIN_DIR` must not be set here). Not fixed
  in this report; the monitor slot of that binary passes (test-4.log).
- `tests/src/tracks/SampleClipWindowTest.cpp:136-152` still documents `addPlayHandle` /
  `removePlayHandlesOfTypes` as missing staged handles; the sweep now walks `m_newPlayHandles`
  (`src/core/AudioEngine.cpp:1925-1954`), so that comment is stale.
