FIXED

new-p15-253: FIXED (round 5)
new-p12-313: FIXED (round 5)

# Cluster new-p15-253 / new-p12-313: an audio bus registered on the audio path

The finding (new-p15-253) is REAL and was already fixed on this branch by commit
`16c7a655a` (round 2): the `AudioBusHandle` constructor no longer registers the bus with
the engine and with the audio device inline; a bus that is created on the audio path is
published to a fixed, lock-free queue and registered by the render owner at the start of
the next period, under the model lock it holds there. That removed the deadlock (a render
worker blocking on the model lock the render owner holds while it waits for that worker),
the allocation (the bus list is only grown by the control thread) and the device call.

Round 5 (this commit) closes the LIFETIME hole that the round-2 fix left open in that
deferred registration - the "safe lifetime handling" the finding asks for. The pending
queue held raw `AudioBusHandle*`; a bus that was removed or destroyed before the render
owner consumed that entry could be freed, and the render owner then registered the freed
pointer into the bus list the render stage walks. The same bus was registered even
though its owner had retired it. Both are fixed here, at the cause.

## What round 2 already fixed (kept as it is)

Commit `16c7a655a` (round 2 of this cluster) fixed the finding itself and is unchanged by
this commit:

- `src/core/AudioEngine.cpp:1168` `addAudioBusHandle`: on the audio path the bus is
  published to a fixed, lock-free `MpscQueue` (`m_pendingAudioBusHandles`) and the
  function returns - no model lock, no container growth. The control thread keeps the
  path it always had (the model lock, the list growth, the play-handle capacity
  top-up).
- `src/core/AudioEngine.cpp:1287` `promotePendingAudioBusHandles`: the render owner
  registers the published buses at the start of the period (`renderNextPeriod`,
  `src/core/AudioEngine.cpp:952`), under the model lock it holds there; the bus list is
  only appended to, never grown (the control thread keeps room in it).
- `src/core/AudioBusHandle.cpp:130-146` `setExtOutputEnabled`: the device port setup
  (which allocates) moved to the control thread.
- `src/core/SamplePlayHandle.cpp:437-446`: the standalone sample bus (the only
  heap-allocated bus in the tree) is destroyed through `AudioEngine::destroyAudioBusHandle`.
- Regression tests `busCreatedOnTheRealtimePathIsRegisteredByTheRenderOwner()` and
  `engineBusRegistrationOnTheRealtimePathIsDeferred()`
  (`tests/src/core/AudioBusHandleTest.cpp:278` and `:304`), both still in place and
  still passing.

That round's own RED/GREEN evidence is in the commit message of `16c7a655a` and in the
logs of the run `new-p15-253.r2`; the fix has not been touched by this round.

## The defect (round 5)

- `src/core/AudioEngine.cpp:1168` `addAudioBusHandle` (round-2 code): on the audio path
  the bus was pushed into `m_pendingAudioBusHandles` and NOTHING recorded that the queue
  now names that bus. The engine had no way to know that the bus must not be freed before
  the render owner consumed the entry.
- `src/core/AudioEngine.cpp:1247` `destroyAudioBusHandle` (round-2 code): the control
  thread did `delete busHandle` at once. A bus that was published to the pending queue and
  destroyed before the next period was therefore freed while the queue still named it -
  the render owner popped the freed pointer and pushed it into `m_audioBusHandles`
  (`promotePendingAudioBusHandles`), and the render stage walked it.
- `src/core/AudioEngine.cpp:1287` `promotePendingAudioBusHandles` (round-2 code): the
  render owner registered every published bus, without asking whether its owner had
  retired it in the meantime. A bus whose owner asked for it to go away was registered
  anyway (and, if it had been freed, registered as a dangling pointer).
- `src/core/AudioEngine.cpp:1219` `removeAudioBusHandle` (round-2 code): a bus that was
  retired while its registration was pending stayed in the pending queue and was
  registered by the render owner afterwards.

## The fix (round 5)

- `include/AudioBusHandle.h:80-90,239`: the bus carries the state of its registration
  with the engine - `AudioBusHandle::RegistrationState` (`Registered` / `Pending` /
  `Retired` / `Handled`) in one `std::atomic`. Only the engine writes it.
- `src/core/AudioEngine.cpp:1168` `addAudioBusHandle`: the bus is marked `Pending`
  BEFORE it is published (from that moment on the render owner may reach it through the
  queue). If the fixed queue refuses the entry, the bus was never published: it is marked
  `Handled` and stays in no list.
- `src/core/AudioEngine.cpp:1219` `removeAudioBusHandle` and `:1247`
  `destroyAudioBusHandle`: retiring a bus that is still `Pending` moves it to `Retired`
  (one compare-exchange), so the render owner will not register it.
- `src/core/AudioEngine.cpp:1287` `promotePendingAudioBusHandles`: the render owner
  registers a published bus only if it is still `Pending` (compare-exchange
  `Pending -> Registered`); a bus that was retired in the meantime is skipped and marked
  `Handled` (the entry is consumed, the bus is in no list). A bus that is in the list
  already is not added twice.
- `src/core/AudioEngine.cpp:1332` `releaseAudioBusHandle`, `:1357` `holdAudioBusHandle`,
  `:1373` `drainHeldAudioBusHandles`: the engine frees a bus only when the render path
  can no longer reach it. A bus that is still `Pending`/`Retired` is KEPT ALIVE in a
  fixed, lock-free hand-over queue (`m_heldAudioBusHandles`,
  `include/AudioEngine.h:755`) until the render owner consumed the pending entry; the
  control thread frees it in its next tick (the queue is drained in the engine's
  control-thread timer, `src/core/AudioEngine.cpp:383`).
- `src/core/AudioEngine.cpp:1247` `destroyAudioBusHandle`: the render owner no longer
  frees a bus it destroys (`delete` on the audio path) - it hands the bus to the control
  thread like a render worker does.
- `include/AudioEngine.h:248-251,755,765`: the hand-over queue, the held-bus counter
  (`heldAudioBusHandleFrees()`, a diagnostic like the counters the round-2 fix added) and
  the declarations of the three new helpers.

## Every caller and sibling (checked)

- `addAudioBusHandle` has exactly one caller: the `AudioBusHandle` constructor
  (`src/core/AudioBusHandle.cpp:101`). The engine's own pool bus
  (`src/core/AudioEngine.cpp:148`) and the track buses (`include/InstrumentTrack.h`,
  `include/SampleTrack.h`) are constructed on the control thread and keep the old path
  (registered inline, under the model lock).
- `removeAudioBusHandle` is called from the `AudioBusHandle` destructor
  (`src/core/AudioBusHandle.cpp:124`) and from `AudioEngine::destroyAudioBusHandle`
  (through that destructor, on the control thread). The engine's readers of the bus list
  (`src/core/AudioEngine.cpp:859` render stage, `src/core/Mixer.cpp:2119`/`:2184` latency
  graph) read it under the model lock, as before; the erase still happens under that
  lock, and the free happens after it was released, so no reader can see a freed bus.
- `destroyAudioBusHandle` has exactly one caller: `SamplePlayHandle::~SamplePlayHandle`
  (`src/core/SamplePlayHandle.cpp:445`), the only heap-allocated bus in the tree. It is
  now freed by the control thread in every case (the control thread, a render worker and
  the render owner), never on the audio path.
- The state is written in exactly four places (`addAudioBusHandle`,
  `removeAudioBusHandle`, `destroyAudioBusHandle`, `promotePendingAudioBusHandles`) and
  read in three (`releaseAudioBusHandle`, `drainHeldAudioBusHandles`, and the
  compare-exchange in the promotion). Every write is a store or a compare-exchange on one
  atomic, so the two racing decisions (the owner retires, the render owner promotes)
  cannot both win.

## Regression tests (added this round)

`tests/src/core/AudioBusHandleTest.cpp` (the file the brief names for this area; +64
lines). Both tests use only the API that exists without this change
(`Engine::audioEngine()->addAudioBusHandle/removeAudioBusHandle/destroyAudioBusHandle`,
`audioBusHandles()`, `renderNextPeriod()`, `RealtimeScope`), so they compile against the
unfixed code and fail on the behaviour, not on a missing symbol. No wall-clock timing,
no sleep, no race: the audio path is entered through the `RealtimeScope` the production
code itself uses, and the registration is observed through the engine's bus list.

- `busRetiredWhileItsRegistrationIsPendingIsNotRegistered()` (line 341) - a bus is
  published on the audio path and retired by its owner before the render owner consumed
  the entry. The render owner must not register it: the bus list must not grow.
- `busDestroyedWhileItsRegistrationIsPendingIsNotRegistered()` (line 374) - a bus is
  published on the audio path and handed to the engine (`destroyAudioBusHandle`) before
  the render owner consumed the entry. The bus must not be registered either, and the
  engine keeps it alive until the entry was consumed, so the render path never walks a
  freed bus.

## Commands run (all logs in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-253.r5/logs`)

| step | command (from the worktree / `build/tests`) | EXIT | log |
| --- | --- | --- | --- |
| build | `cmake --build build --target zene AudioBusHandleTest -j8` | 0 | build-1.log, build-2.log |
| GREEN (first, after the fix) | `ctest -R '^AudioBusHandleTest$' --output-on-failure` | **0** | test-1-green.log: 1/1 passed |
| patch | `git diff -- include/AudioBusHandle.h include/AudioEngine.h src/core/AudioEngine.cpp > logs/fix.patch` | 0 | fix.patch (348 lines) |
| revert the product hunks | `git apply -R logs/fix.patch` | **0** | - |
| build (reverted) | `cmake --build build --target zene AudioBusHandleTest -j8` | 0 | build-3-red.log |
| **RED** (reverted) | `ctest -R '^AudioBusHandleTest$' --output-on-failure` | **8** | test-2-red.log: `FAIL! : AudioBusHandleTest::busRetiredWhileItsRegistrationIsPendingIsNotRegistered() Compared values are not the same` (Actual 3, Expected 2) and `FAIL! : AudioBusHandleTest::busDestroyedWhileItsRegistrationIsPendingIsNotRegistered() Compared values are not the same` (Actual 4, Expected 3) |
| restore the fix | `git apply logs/fix.patch` | **0** | - |
| build (restored) | `cmake --build build --target zene AudioBusHandleTest -j8` | 0 | build-4-green.log |
| **GREEN** (restored) | `ctest -R '^AudioBusHandleTest$' --output-on-failure` | **0** | test-3-green.log: 1/1 passed |
| **GREEN** (restored, direct) | `./AudioBusHandleTest` | **0** | test-4-green-direct.log: `Totals: 11 passed, 0 failed, 0 skipped` |
| family build | `cmake --build build --target AudioBusHandleTest AudioBusPlayHandleQueueTest MissingDevicesPreserveTest AudioClipLoopTest AudioEditingTest AudioResamplerRatioTest AudioStretcherTest ClipFadesRenderTest ClipOffsetFadePairTest ClipOffsetFadeTest FreezeRegionRepairTest FrozenTakeHandoffTest FrozenTakeRoundTwoTest InstrumentLatencyPdcTest MixerConcurrencyTest MixerToolsTest PdcMixerTest RackZoneFeedRunTest RackZoneRoutingTest W3EngineTest -j8` | 0 | build-5-family.log |
| family test | `ctest -R '^(AudioBusHandleTest\|AudioBusPlayHandleQueueTest\|MissingDevicesPreserveTest\|...\|W3EngineTest)$' --output-on-failure` | **0** | test-5-family.log: **20/20 passed** |
| gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | **0** | gate-1-feature-check.log: all 8 gates PASS |

The RED run is the final product code with only the product hunks reverted
(`git apply -R logs/fix.patch`); the test file was not touched. Both new tests failed on
the bus-list size, the other nine functions of the same binary still passed.

### Affected family (final binaries)

| command | EXIT | result |
| --- | --- | --- |
| `ctest -R '^(AudioBusHandleTest\|AudioBusPlayHandleQueueTest\|MissingDevicesPreserveTest\|AudioClipLoopTest\|AudioEditingTest\|AudioResamplerRatioTest\|AudioStretcherTest\|ClipFadesRenderTest\|ClipOffsetFadePairTest\|ClipOffsetFadeTest\|FreezeRegionRepairTest\|FrozenTakeHandoffTest\|FrozenTakeRoundTwoTest\|InstrumentLatencyPdcTest\|MixerConcurrencyTest\|MixerToolsTest\|PdcMixerTest\|RackZoneFeedRunTest\|RackZoneRoutingTest\|W3EngineTest)$'` | 0 | 20/20 passed (test-5-family.log) |

The three tests the brief names for this area - `AudioBusHandleTest`,
`AudioBusPlayHandleQueueTest`, `MissingDevicesPreserveTest` - are part of that set and
pass. `zene` itself was rebuilt (build-2.log) before the test runs.

## Files committed

- `include/AudioBusHandle.h` - the registration state of a bus (declaration only).
- `include/AudioEngine.h` - the hand-over queue for the buses the engine keeps alive,
  the held-bus counter and the three new helper declarations.
- `src/core/AudioEngine.cpp` - the lifetime protocol of a pending registration.
- `tests/src/core/AudioBusHandleTest.cpp` - the two regression tests.
- `tests/upstream-modifications.txt` - the divergence ledger entries for the four
  inherited files (the two headers, the engine source and the test file).
- `docs/qwen-fixes-2026-10/cluster-new-p15-253.md` - this report.

No new product file, so `tests/fork-sources.txt` needs no entry. The inherited files
changed are declared in `tests/upstream-modifications.txt` with a reason, as the ledger
requires.

## Gates

`python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` -> **EXIT 0**
(log: gate-1-feature-check.log):

```
feature-check zene-qwen-new-p15-253 (base b61e5f965, 8 changed files; logs in .../zene-qwen-new-p15-253-feature-check)
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

The complexity and file-length ratchets report "no new item in your files": the files I
changed are not fork-NEW sources, and no new function of mine is over the CCN target or
in a file over the line limit. `git status --short` after the gates lists only the files
this report commits; no gate rewrote a file.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it (exit
  codes).** RED: `ctest -R '^AudioBusHandleTest$' --output-on-failure` EXIT **8** (both
  new tests failed on the bus-list size; test-2-red.log). GREEN after `git apply
  logs/fix.patch`: EXIT **0**, `Totals: 11 passed, 0 failed` (test-3-green.log,
  test-4-green-direct.log).
- **The test is deterministic: no wall-clock timing, sleeps or races.** The realtime path
  is entered through `RealtimeScope` (the same marker the production code uses), the
  registration is observed through the engine's bus list, and the period is driven by the
  existing public `AudioEngine::renderNextPeriod()`. No timer, no sleep, no thread.
- **The fix is at the cause and complete: every caller and sibling was checked.** The
  cause is the missing lifetime of a published bus: the engine published a raw pointer
  into a queue that the render owner drains one period later, without any record that
  the bus must stay alive until then, and without asking whether the bus was still
  wanted. Both are fixed in the four places that decide the state of a bus'
  registration (publish, retire, promote, free), and the free path was routed through
  one helper (`releaseAudioBusHandle`) that all three callers use. The sibling paths
  (the removal, the destruction, the device port update, the capacity top-up) keep the
  behaviour the round-2 fix gave them.
- **No collateral damage.** The control-thread behaviour is unchanged: a bus created on
  the control thread is still registered inline, under the model lock, and the device
  port is still set up there. The bus list is only appended to, never reordered, so the
  mix order and the latency graph see the same order as before. The only observable
  difference on the audio path is that a bus that was retired before the render owner
  consumed its registration is not registered - which is the point: its owner asked for
  it to go away. No project file, save format or undo step is touched; no command was
  added. The 20 tests of the affected family pass (test-5-family.log).
- **Realtime.** Nothing new on the audio path allocates, frees, locks, waits or logs. On
  the audio path the engine only stores one atomic and pushes into a fixed-size
  lock-free queue; the render owner only reads that atomic, appends to a vector the
  control thread keeps room in, and pushes into a fixed queue. The render owner no
  longer frees a bus it destroys - that free moved to the control thread. The hand-over
  queue is fixed storage, so the hand-over does not allocate either. The control thread
  takes the model lock as it always did.
- **Undo / commands.** No control command was added, no command id changed, so the
  `ControlCommandsSnapshot` canary is untouched. The fix changes no undo step: a bus is
  not an undoable model object, and its registration was never an undo step.
- **"If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the
  intended contract that shows it."** Not applicable: the verdict is FIXED. The defect
  was in the tree, the code is cited above, and the tests fail without the change.

### The contract this fix keeps

`src/core/AudioBusHandle.cpp:93-95` states the contract the round-2 fix had to keep: a
standalone sample bus may be created on the audio path. The round-2 fix made that
registration safe; this round gives the published bus the lifetime that path needs: a bus
created on the audio path must be destroyed through `AudioEngine::destroyAudioBusHandle`
(the only heap-allocated bus in the tree does exactly that,
`src/core/SamplePlayHandle.cpp:437-446`), because a bus that is freed while the pending
queue still names it cannot be reached safely any more. The engine keeps such a bus
alive on its own; only an owner that frees its bus behind the engine's back breaks the
contract - the same contract the round-2 fix already stated.
