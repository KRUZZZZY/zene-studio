ALREADY-FIXED

The take-lane model's edits already hold the audio engine's change lock, and the render period holds that same lock while `SampleTrack::play()` iterates the composite. Nothing in this finding's mechanism is unprotected on this branch; the fix landed earlier (commits `6afd3b4038` and `82e5b0783f`, both ancestors of `HEAD`). This cluster adds a deterministic regression test that pins that contract.

## Decision

REAL in its time, ALREADY-FIXED on `bugs/qwen-new-p15-196`. The finding names `namedByComposite()` iterating `takeLanes().segments()` on the audio thread while an edit reallocates the vector.

- `src/tracks/SampleTrack.cpp:116-123` — `namedByComposite()` ranges over `track.takeLanes().segments()`; called from `SampleTrack::play()` at `:281`. `play()` also reads `segments().empty()` and `auditionLane()` at `:218-220`.
- `src/core/Song.cpp:687` — `track->play(...)` is called from `Song::processNextBuffer()`, which `AudioEngine::renderStageNoteSetup()` calls at `src/core/AudioEngine.cpp:1065`.
- `src/core/AudioEngine.cpp:1207-1212` — `renderNextPeriod()` takes `std::lock_guard{m_changeMutex}` (line 1210) and holds it for the whole period; `renderStageNoteSetup()` runs at `:1226` inside it. So every read above runs under the engine change lock.
- `src/core/TakeLane.cpp:55-61` — `changeLockForEdit()` returns `AudioEngine::requestChangesGuard()` (a no-op without an engine).
- `src/core/TakeLane.cpp:1708-1721` — `AudioEngine::requestChangeInModel()` locks `m_changeMutex`; a control thread waits for the render period, and the render thread short-circuits only when it is itself the caller (`s_renderingThread`).
- Every mutator of `TakeLaneModel` takes that guard as its first statement: `setAuditionLane` `src/core/TakeLane.cpp:175`, `recordTake` `:218`, `selectTake` `:239`, `removeTakeClip` `:252`, `setPlayAllTakes` `:283`, `addLane` `:289`, `removeLane` `:330`, `selectSegment` `:397`, `fillGaps` `:433`, `rebuild` `:467`, `clear` `:568`, and `loadSettings` `src/core/TakeLanePersistence.cpp:148`. `m_segments` is private; `sortSegments`/`mergeSegments` are private and only called from those locked functions, so no writer exists outside the lock.
- `include/TakeLane.h:209` — `playAllTakes()` is an atomic read for the render thread; `include/TakeLane.h:280` — the audition lane is documented and written under the change lock. The clip-level active-take mirror is atomic (`include/Clip.h:214-216`).

The fix commits are in this branch's history (not a claim about a future train):

- `git merge-base --is-ancestor 6afd3b4038 HEAD` → EXIT 0 — "fix(comp): the take-lane model's edits hold the change lock (round 5 review, row 5)", whose message names exactly this read ("the muted-take scheduling in SampleTrack::play").
- `git merge-base --is-ancestor 82e5b0783f HEAD` → EXIT 0 — "fix(comp): the audition lane is written under the change lock".

## Render-side readers, all excluded by the same lock

| reader | where | lock |
| --- | --- | --- |
| `namedByComposite`, `comped`, `auditionLane` | `src/tracks/SampleTrack.cpp:116,218-220,281` | render period (`renderNextPeriod` `m_changeMutex`) |
| `compPlays`/`takePlays` | `src/tracks/InstrumentTrack.cpp:901-921` | same (`track->play` from `Song::processNextBuffer`) |
| pre-fade / loop gate, `ClipPreFades.cpp:51`, `ClipAudioLoop.cpp:48` | called from `SampleTrack::play` | same |
| play handle span snapshots / slipped comp reads | `src/core/SamplePlayHandle.cpp:148,262,354`, `src/core/CompSlip.cpp` | render stages 0-1, joined inside the locked period |

Writers are the single choke point (`TakeLaneModel` mutators above); the GUI edits through `comp.*` commands (`src/gui/TakeLanesPanel.cpp:104-107`), never the vector.

## Regression test

`tests/src/tracks/TakeLaneCompPlaybackTest.cpp::compositeEditsWaitForTheEngineChangeLock` (line 222). It takes `AudioEngine::requestChangesGuard()` on the test thread (what `renderNextPeriod` holds), starts a writer thread that calls `TakeLaneModel::selectSegment`, and uses the existing `AudioEngine::modelChangeWaiters()` seam to see the writer parked in `requestChangeInModel()`. It then asserts the composite is unchanged while the period owns the model, releases, joins, and asserts the edit landed. No sleep decides the outcome; a 10 s deadline only turns a missing lock into a failure instead of a hang. Registered in `tests/upstream-modifications.txt`.

RED/GREEN, with the product line removed and restored (the removal is a local patch, not part of this commit):

- `python3` edit removes `const auto changeLock = changeLockForEdit();` from `TakeLaneModel::selectSegment` → `git diff -- src/core/TakeLane.cpp > /home/kruzzzzy/.config/kads/zene/state/runs/new-p15-196.r1/logs/fix.patch` — EXIT 0.
- `cmake --build build --target TakeLaneCompPlaybackTest -j4 > .../logs/build-2-red.log 2>&1` — EXIT 0.
- `./TakeLaneCompPlaybackTest compositeEditsWaitForTheEngineChangeLock > .../logs/test-2-red.log 2>&1` (cwd `build/tests`, lock line removed) — **EXIT 1**; `FAIL!  : ... 'reachedLock' returned FALSE. (a take-lane edit never asked for the audio engine's change lock)`; `Totals: 2 passed, 1 failed`.
- `git apply -R .../logs/fix.patch` — EXIT 0 (lock restored; `git diff -- src/ include/` empty).
- `cmake --build build --target TakeLaneCompPlaybackTest -j4 > .../logs/build-3-green.log 2>&1` — EXIT 0.
- `./TakeLaneCompPlaybackTest > .../logs/test-3-green-full.log 2>&1` (cwd `build/tests`) — **EXIT 0**; `Totals: 16 passed, 0 failed, 0 skipped, 0 blacklisted`.
- `ctest -R '^(TakeLaneCompPlaybackTest|TakeLaneCompTest|TakeLaneTest|TakeLanesPanelTest)$' --output-on-failure > .../logs/test-5-family.log 2>&1` (cwd `build/tests`) — **EXIT 0**; 4/4 passed. The first family run (`test-4-family.log`, EXIT 8) reported three targets "Not Run" because their binaries were not built; `build-4-family.log` (EXIT 0) built them and the rerun passed.

Every log path above is under `/home/kruzzzzy/.config/kads/zene/state/runs/new-p15-196.r1/logs/`.

## Gates

`python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p15-196 > .../logs/gate-1.log 2>&1` — **EXIT 0**:

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
```

## Self-check

- **Regression test fails with the product change reverted and passes with it:** red `test-2-red.log` EXIT 1 (`reachedLock` FALSE, lock line removed), green `test-3-green-full.log` EXIT 0 (16/16). The reverted line is `src/core/TakeLane.cpp:397`; the patch and both commands are listed above. This test pins the existing fix; there is no new product change in this cluster.
- **The test is deterministic:** no sleep and no timing decision. The writer is ordered by the engine's change lock (taken first on the test thread), and the observation is `AudioEngine::modelChangeWaiters()`, the seam that counts a thread parked in `requestChangeInModel()` before it blocks. The test file's `initTestCase` stops device processing, so no audio thread competes. The 10 s deadline is only a failure bound for a missing lock.
- **Cause and completeness:** the cause is exclusion of the render-thread read; the code reference above shows the read path (`Song::processNextBuffer` → `track->play` → `namedByComposite`) inside `renderNextPeriod`'s `m_changeMutex`, and all 12 `TakeLaneModel` mutators taking the same guard. Readers audited: `SampleTrack::play`, `InstrumentTrack::play`, `ClipPreFades`, `ClipAudioLoop`, `SamplePlayHandle`, `CompSlip` — all inside the locked period. `m_segments` has no writer outside the mutators (private member).
- **No collateral damage:** this cluster changes no product file and no saved-data path. `saveSettings`/`loadSettings` shapes are untouched (`src/core/TakeLanePersistence.cpp` unchanged), the four comp test targets pass, and no new limit was added.
- **ALREADY-FIXED evidence:** `src/core/TakeLane.cpp:55-61` (the guard), `:397` and the other mutators (writers), `src/core/AudioEngine.cpp:1210` (render period holds `m_changeMutex`), `src/tracks/SampleTrack.cpp:116-123,281` (the read the finding names), and ancestor commits `6afd3b4038`, `82e5b0783f`.
- **Sibling commands:** no `ControlCommands*`, `ControlRegistry*` or `ControlReversibilityTable*` file is changed by this cluster; the ask's sibling list comes from the branch's inherited merge history (`base..HEAD`), not from this patch. No command was added, changed or reclassified, so every `comp.*` sibling (`lane_add`, `lane_remove`, `lane_list`, `assign`) is unaffected: they call the same locked `TakeLaneModel` mutators as before.

## Other findings

- A different field, outside this finding's mechanism and not changed here: `Clip::laneIndex()` returns a plain `int` (`include/Clip.h:195`) that the render thread reads in `SampleTrack.cpp:119,250`, while `comp.assign` writes it through `Clip::setLaneIndex` (`src/core/ControlCommandsComp.cpp:333`, and `assignTakeLane` at `:366`) without taking the engine change lock. That is a separate clip-tag publication question (the active-take mirror next to it is atomic) and deserves its own cluster; it is not the segment-vector reallocation this finding describes.
