# Cluster 5430 — Replacing track versions edits clips outside the audio guard

Verdict: **ALREADY-FIXED** on this branch (`bugs/qwen-5430`).

The defect was real when Luna's patrol filed it (`requestChangesGuard()` was scoped to a block around
`track->deleteClips()` only, so `createClip()` and `restoreState()` ran with the audio model
unlocked). It was fixed on this branch by commit `1ef0672e9` ("fix(versions): extend audio guard
over clip replacement (cluster 5430)"), which is an ancestor of this branch's HEAD. The regression
test that came with that commit was **timing-based** (a 50 ms deadline plus 1 ms sleeps), so it is
not a valid regression proof under the definition of done. This round replaces it with a
deterministic test and adds the one test seam it needs.

## Evidence (file:line)

The guard now spans the whole replacement:

- `src/core/TrackVersions.cpp:84-93` — `auto guard = Engine::audioEngine()->requestChangesGuard();`
  is taken at function scope, **before** `track->deleteClips()` (`:87`) and released only when
  `replaceClips()` returns, so `track->createClip(TimePos(0))` (`:90`) and
  `clip->restoreState(element)` (`:91`) both run inside it.
- `include/AudioEngine.h:97-131` — `RequestChangesGuard` is RAII: the constructor calls
  `requestChangeInModel()` (`:103`) and the destructor calls `doneChangeInModel()` (`:124`).
- `include/AudioEngine.h:536-539` — `requestChangesGuard()` hands out that guard.
- `src/core/AudioEngine.cpp:1386-1396` — `requestChangeInModel()` locks `m_changeMutex`,
  `doneChangeInModel()` unlocks it (`include/AudioEngine.h:758`, a `std::recursive_mutex`).
- `src/core/AudioEngine.cpp:921` — `renderNextPeriod()` takes the same mutex
  (`std::lock_guard{m_changeMutex}`) for the whole render period, so while the guard is alive the
  audio thread cannot enter the model.
- The list the audio thread walks is edited under that guard: `src/core/Track.cpp:906`
  (`Track::addClip()` → `m_clips.push_back(clip)`), `src/core/Track.cpp:945-951`
  (`Track::deleteClips()`).

The old code (what the finding describes) is the parent of that commit; the reverted form is kept
verbatim in `runs/5430.r1/logs/fix.patch` and was used for the RED proof below.

## What this round changed

Only the regression test and the test seam it needs — no product behaviour changed:

- `include/AudioEngine.h:58` and `include/AudioEngine.h:563-566` — a `friend class ::ArrangeToolsTest;`
  under `#ifdef LMMS_TESTING`, the pattern this tree already uses for
  `::AudioBusPlayHandleQueueTest` (`include/AudioEngine.h:57, 562`). It lets the test read
  `AudioEngine::m_changeMutex` (`include/AudioEngine.h:758`) — the lock the audio thread takes.
  The declaration is compiled only in test builds (`target_compile_definitions(lmmsobjs PRIVATE
  LMMS_TESTING)`, `tests/CMakeLists.txt:6`) and changes no runtime behaviour.
- `tests/src/core/ArrangeToolsTest.cpp` — the timing-based probe (`ModelGuardProbe`, 50 ms
  deadline, 1 ms sleeps, a racing worker thread) is replaced by a deterministic one:
  - `tests/src/core/ArrangeToolsTest.cpp:103-146` — `GuardProbeSampleClip` / `GuardProbeSampleTrack`
    call a probe callback at the two points of the replacement (while the clip is created, while it
    is restored) and record what the probe saw.
  - `tests/src/core/ArrangeToolsTest.cpp:513-527` — `changeMutexIsHeld()`: a second thread tries to
    take the change mutex with `try_lock()` (non-blocking) and reports whether it got it. Only a
    free mutex can be taken, so the answer is the state of the mutex at that instant. No sleeps, no
    deadline, no wall-clock: the worker never blocks and the test waits for its answer.
  - `tests/src/core/ArrangeToolsTest.cpp:206-226` — the test: the audio device is stopped for the
    probe (the render thread takes the same mutex, `src/core/AudioEngine.cpp:921`, and would
    otherwise make the probe answer "held" by accident), a control assertion checks that the mutex
    is free when no guard is in sight, then the replacement must find it held both while the clip is
    created and while it is restored.

The seam is the only way to observe the guard from a test: `AudioEngine`'s change mutex is private
and `requestChangeInModel()` blocks instead of reporting, so a test cannot ask "is the model
locked?" without either a deadline (the old, timing-based probe) or access to the mutex.

## Callers and siblings (checked, consistent)

- The only production caller of `TrackVersions::replaceClips()` is
  `src/core/ControlCommandsTrackVersions.cpp:114` (`switchVersion()`), i.e. the
  `track.version_switch` control command. The GUI's version menu dispatches that same command
  (`src/gui/shell/ShellMenuEntries.cpp:76-90`), so every version switch — GUI, agent command or
  script — goes through the guarded replacement.
- The undo of a version switch restores the track through `SerializingObject::restoreState()`
  (`src/core/SerializingObject.cpp:69-77`) → `Track::loadSettings()` (`src/core/Track.cpp:891-895`)
  → `Track::loadTrack()`, which guards only its deletion (`src/core/Track.cpp:774-777`). That is the
  upstream pattern for a *load*: extending the guard over the whole walk would hold the audio
  model's change mutex for the length of a project load (every track), a different trade-off from
  the short version replacement, and it is not this cluster's contract. Left untouched on purpose;
  it is reported here as an observation, not changed.
- `TrackVersions::replaceClips()` parses the XML before it touches the track
  (`src/core/TrackVersions.cpp:81-83`), so a stored version that is not XML still leaves the track
  as it was — unchanged by this round.

## Commands and EXIT codes (logs in runs/5430.r1/logs)

Baseline before the change (the old timing-based test, already committed):

```
$ cmake --build build --target ArrangeToolsTest -j20 > logs/build-1.log 2>&1; echo EXIT=$?
EXIT=0
$ (cd build/tests && ctest -R '^ArrangeToolsTest$' --output-on-failure > logs/test-1.log 2>&1; echo EXIT=$?)
EXIT=0
```

RED — the committed fix reverted with `git apply -R logs/fix.patch`, rebuilt, the new test run:

```
$ git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5430.r1/logs/fix.patch; echo EXIT=$?
EXIT=0
$ cmake --build build --target ArrangeToolsTest -j20 > logs/build-3-red.log 2>&1; echo EXIT=$?
EXIT=0
$ (cd build/tests && ctest -R '^ArrangeToolsTest$' --output-on-failure > logs/test-3-red.log 2>&1; echo EXIT=$?)
EXIT=8
```

```
FAIL!  : ArrangeToolsTest::replacingTrackVersionKeepsModelGuardThroughClipRestore() 'track->createRanInsideGuard()' returned FALSE. (the track version replaced its clips outside the audio guard: the change mutex was free while the replacement clip was created)
```

Repeated ten times in the reverted state (`logs/build-7-red-repeat.log`): 10 runs, 10 failures,
same assertion — the regression is caught every time, not by luck.

GREEN — the fix re-applied with `git apply logs/fix.patch`, rebuilt, the same test run:

```
$ git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5430.r1/logs/fix.patch; echo EXIT=$?
EXIT=0
$ cmake --build build --target ArrangeToolsTest -j20 > logs/build-4-green.log 2>&1; echo EXIT=$?
EXIT=0
$ (cd build/tests && ctest -R '^ArrangeToolsTest$' --output-on-failure > logs/test-4-green.log 2>&1; echo EXIT=$?
EXIT=0
```

```
1/1 Test #264: ArrangeToolsTest .................   Passed    1.71 sec
100% tests passed, 0 tests failed out of 1
```

Ten more runs of the binary in the fixed state: 10/10 exit 0. The product binary was rebuilt as
well (`logs/build-6-zene.log`, EXIT=0).

The tests that share the change mutex with this one (`logs/build-9-neighbours.log`, EXIT=0):

```
$ (cd build/tests && ctest -R '^(MixerConcurrencyTest|AudioBusPlayHandleQueueTest|ArrangingCatchUpTest|ArrangeToolsTest)$' --output-on-failure > logs/test-7-neighbours-green.log 2>&1; echo EXIT=$?)
EXIT=0
```

```
1/4 Test #114: AudioBusPlayHandleQueueTest ......   Passed    1.43 sec
2/4 Test #122: MixerConcurrencyTest .............   Passed    5.50 sec
3/4 Test #264: ArrangeToolsTest .................   Passed    2.80 sec
4/4 Test #277: ArrangingCatchUpTest .............   Passed    2.92 sec
100% tests passed out of 4
```

(The first run of that family failed `ArrangingCatchUpTest` with
`The plugin "tripleoscillator" wasn't found` — this lane's build tree had no plugin binaries; after
`cmake --build build --target tripleoscillator` (`logs/build-10-tripleosc.log`, EXIT=0) the test
passes. Nothing to do with this change.)

## Gates

```
$ python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-5430 > logs/gate-1.log 2>&1; echo EXIT=$?
EXIT=0
```

```
feature-check zene-qwen-5430 (base 2da2ed356, 2 changed files)
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

## Bookkeeping

No new product files: `include/AudioEngine.h` and `tests/src/core/ArrangeToolsTest.cpp` are both
inherited files that already exist in the tree. `tests/src/core/ArrangeToolsTest.cpp` is a
fork-new test source and is already registered in `tests/fork-sources.txt:2230`, so it needs no new
entry. `include/AudioEngine.h` is an inherited (fork-modified) header, so this round's change is
declared in the divergence ledger with its reason: `tests/upstream-modifications.txt` (last line)
— "cluster 5430: the track-version regression has to see whether a model edit runs inside the
engine change mutex, so the test is granted access to it; the friend declaration sits inside
`#ifdef LMMS_TESTING` and changes no runtime behaviour, no declaration and no call site."
No control command was added or touched (the `track.version_switch` command surface is unchanged),
so the command snapshot and the README canary are untouched.

## Self-check

- **The regression test FAILS with your product change reverted and PASSES with it (exit codes).**
  Reverted (the committed fix `1ef0672e9` undone with `git apply -R logs/fix.patch`):
  `ctest -R '^ArrangeToolsTest$'` → EXIT=8, `FAIL! ... 'track->createRanInsideGuard()' returned
  FALSE`, 10/10 runs fail (`logs/test-3-red.log`, `logs/build-7-red-repeat.log`). Re-applied:
  `ctest -R '^ArrangeToolsTest$'` → EXIT=0, 1/1 passed, 10/10 runs of the binary exit 0
  (`logs/test-4-green.log`, `logs/test-5-green-repeat.log`).
- **The test is deterministic: no wall-clock timing, sleeps or races (name the seam or injected
  values it uses).** The seam is `friend class ::ArrangeToolsTest;` in `include/AudioEngine.h:566`
  (test builds only), which exposes `AudioEngine::m_changeMutex`. The probe
  (`tests/src/core/ArrangeToolsTest.cpp:513-527`) takes that mutex with `try_lock()` from a second
  thread: a non-blocking attempt, so the answer is the mutex state at that instant; the test waits
  for the worker's answer, never for a timer. There is no sleep, no deadline and no `std::chrono` in
  the test any more (the old 50 ms/1 ms probe is gone). The probe points are the two product calls
  the finding names (`createClip`, `restoreState`), reached through the track/clip subclasses the
  test already used. 10 consecutive runs give the same result in both states.
- **The fix is at the cause and complete: every caller and sibling of what you changed was checked
  and stays consistent.** The cause (the guard's scope in `TrackVersions::replaceClips()`) is
  already fixed at `src/core/TrackVersions.cpp:84-93`; the only production caller is
  `src/core/ControlCommandsTrackVersions.cpp:114` and the GUI dispatches the same command
  (`src/gui/shell/ShellMenuEntries.cpp:76-90`), so there is no second, unguarded path into a
  version switch. The sibling restore path (`Track::loadSettings()`, `src/core/Track.cpp:774-777`)
  keeps the upstream pattern on purpose (guarding a whole project load would hold the audio model's
  change mutex for the whole load) and is reported above, not changed.
- **No collateral damage: old projects and saved data still load as before, and any limit or check
  you added does not hit other callers that legitimately exceed it.** The only product change is a
  friend declaration inside `#ifdef LMMS_TESTING` (`include/AudioEngine.h:563-566`): it declares
  a name and grants access, it changes no code path, no serialisation and no file format. Project
  files, saved versions and the `TrackVersions` project section are untouched, so old projects load
  and save exactly as before. The test's `stopProcessing()` / `startProcessing()` pair is scoped to
  the probe slot (restored by a `qScopeGuard`) and touches only the test process.
- **If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the intended
  contract that shows it.** `src/core/TrackVersions.cpp:84-93` (guard at function scope around
  `deleteClips()`, `createClip()` and `restoreState()`), with the RAII contract in
  `include/AudioEngine.h:97-131` and `src/core/AudioEngine.cpp:1386-1396`, and the audio thread's
  side of the same lock at `src/core/AudioEngine.cpp:921`. The fix landed in commit `1ef0672e9`,
  an ancestor of this branch's HEAD; this round replaced only its timing-based regression test with
  a deterministic one and added the test seam for it.
