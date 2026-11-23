FIXED
# new-p15-253: constructing an audio bus on the realtime path is deferred to the control thread

Verdict: **REAL, FIXED** (round 10). A bus constructed while `RealtimeScope` is active no longer
takes the engine's model lock, grows the bus list, prepares storage, or calls the audio device.
It marks itself `Pending`, is pushed onto an intrusive per-bus registration link, requests its
device-port state through a second link, and returns. `serviceAudioBusHandlesOnControlThread()` is
the only activation point and runs on the control thread from the play-handle capacity timer.

## The bug (current line numbers)

- Realtime constructor branch: `src/core/AudioBusHandle.cpp:88-96`. Before this branch existed, the
  same constructor ran `prepareStorage()` (allocates the buffer and effect chain, `:98`) and
  `AudioEngine::addAudioBusHandle()` inline, which took `m_changeMutex` and did
  `m_audioBusHandles.push_back` (`src/core/AudioEngine.cpp:1203-1205`), then called
  `setExtOutputEnabled(true)`, which calls the device (`:156-163`).
- A render worker that did that would wait for `m_changeMutex`, which the render owner holds for
  the whole period (`renderNextPeriod()` locks it at `src/core/AudioEngine.cpp:932`) and the owner
  cannot finish the period until that worker returns: a deadlock. On the render owner the same
  code allocates inside the realtime period. `RealtimeScope` is active on both paths
  (`src/core/AudioEngine.cpp:934`).

Independently visible in the previous round's code: `addAudioBusHandle()` deferred the bus to an
MpscQueue whose consumer was the **render owner** (`promotePendingAudioBusHandles()`), so the
render period still registered buses and grew the list. The diagnostic checkpoint asked for the
per-bus intrusive links and a single control-thread activation instead; that is the fix below.

## The fix (file:line at HEAD)

- Realtime constructor: `src/core/AudioBusHandle.cpp:88-96` calls only
  `AudioEngine::addAudioBusHandle(this)` and `setExtOutputEnabled(true)` and returns. No
  `prepareStorage`, no lock, no device call. Storage and the effect chain are prepared on the
  control thread in `serviceRegistrations()`.
- Link path: `src/core/AudioEngine.cpp:1183-1197`. The state is stored `Pending` before the link
  (`:1191-1192`); `linkOnce()` (`:1172-1178`) exchanges a per-bus `enqueued` flag and then pushes
  the bus onto `m_registrationHead`, a lock-free intrusive stack with no allocation.
- Control-thread branch of `addAudioBusHandle`: `:1199-1221` takes the model lock, appends, and
  marks the bus `Registered`.
- Removal and destruction: `:1222-1247` and `:1249-1268`. A worker never frees a bus the render
  path can still read; it sets `Retired` and links the retirement stack
  (`RetirementRemove` / `RetirementRemoveAndFree`). The control thread releases a bus immediately
  only when `hasQueuedLinks()` is false (`:1280-1296`).
- The single activation point: `:1314-1322` runs `serviceRegistrations()`, then
  `serviceRetirements()`, then `servicePortUpdates()`. The timer calls it once per tick
  (`:380`). The render period does not call it and does not promote a bus: a grep over
  `renderNextPeriod()` (`:930-1010`) finds no service, registration, pending or promotion call.
- Registration phase: `:1325-1355`. Under `m_changeMutex` it takes the whole stack, clears each
  link's `enqueued` after reading `next`, CASes `Pending -> Registered`, calls `prepareStorage()`
  and appends the bus (once). A bus retired meanwhile (`Retired`) is marked `Handled` and is never
  registered.
- Retirement phase: `:1357-1383`. Removes each bus from the list, then frees only a bus with no
  queued link left (`hasQueuedLinks()`, `include/AudioBusHandle.h:158-162`); otherwise it re-links
  it for a later tick.
- Port phase: `:1386-1414`. Reads the coalesced wanted state and calls `applyDevicePortState()`
  (`:1435-1449`) only for a listed bus; the realtime request path is
  `AudioBusHandle::setExtOutputEnabled` (`src/core/AudioBusHandle.cpp:151-163`) and
  `AudioEngine::requestDevicePortUpdate` (`:1426-1433`).
- Data: `include/AudioBusHandle.h:82-92` (`RegistrationState`), `:254` (`m_registrationState`),
  `:263-266` (intrusive links), and `include/AudioEngine.h:260`, `:701-707`,
  `:731-733` (the three stack heads).
- Lifetime contract: `include/AudioBusHandle.h:66-71` says a bus built on the audio path must be
  destroyed through `AudioEngine::destroyAudioBusHandle`. The only heap bus in `src/` is the
  standalone sample bus (`src/core/SamplePlayHandle.cpp:78`), and its only destroy call is
  `:445`; it now goes through that function.

## Every-caller audit

- `new AudioBusHandle` in product code: `src/core/SamplePlayHandle.cpp:78` only. `src` stack
  buses (`src/core/AudioEngine.cpp:148`, mixer/track buses) are constructed on the control
  thread and take the non-realtime branch.
- `addAudioBusHandle` callers: the constructor (`src/core/AudioBusHandle.cpp:92` and `:110`) and
  the tests; no plugin constructs a bus.
- `removeAudioBusHandle`: bus destructor (`src/core/AudioBusHandle.cpp:143`).
- `setExtOutputEnabled`: constructor and `~AudioBusHandle`; both now route through the same
  wanted/applied state.
- No `promotePendingAudioBusHandles`, `MpscQueue` bus queue or held-bus list remains
  (`rg` over `src include`).

## State sequences (driven by `AudioBusHandleTest::realtimeBusRegistrationStatesFollowTheControlThreadHandoff`, `tests/src/core/AudioBusHandleTest.cpp:521`)

- A bus constructed on the audio path: `Pending`, then `Registered` after
  `serviceAudioBusHandlesOnControlThread()`.
- A bus retired while its registration is pending: `Pending`, `Retired`, then `Handled` after the
  service consumes the registration link; it is never listed.

The same test is the `state-sequence` artifact in `proof.json`.

## Evidence run in this session (round 10)

Logs are in `/home/kruzzzzy/.config/kads/zene/state/runs/new-p15-253.r10/logs`.
Build/test logs end with the command's exit where the shell captured it; every value below is the
exit printed by that command in this session.

- Baseline GREEN: `cmake --build build --target AudioBusHandleTest AudioBusPlayHandleQueueTest AudioBusTest -j4`
  EXIT=0 (`build-1.log`); `ctest -R 'AudioBus' --output-on-failure` from `build/tests` EXIT=0,
  `100% tests passed out of 3` (`test-1-green.log`).
- Product patch used for the proof: `git diff 730db9d871 HEAD -- include/AudioBusHandle.h
  include/AudioEngine.h src/core/AudioBusHandle.cpp src/core/AudioEngine.cpp
  src/core/SamplePlayHandle.cpp > fix.patch` EXIT=0, 749 lines (`fix.patch`,
  `fix-patch-err.log`). It is the change that replaced the MpscQueue/render-owner promotion with
  the intrusive-link/control-thread activation.
- RED (whole product patch reversed): `git apply -R fix.patch` EXIT=0 (`apply-reverse.log`);
  `cmake --build ...` EXIT=0 (`build-2-red.log`); `ctest -R 'AudioBus'` EXIT=8
  (`test-2-red.log`): `33% tests passed, 2 tests failed out of 3` - `AudioBusHandleTest` and
  `AudioBusPlayHandleQueueTest`. Failures include
  `realtimeBusConstructorIsActivatedByControlService` (allocated an EffectChain before the
  service), `engineBusRegistrationOnTheRealtimePathIsDeferred` (list grew 2 -> 3),
  `realtimeBusRegistrationStatesFollowTheControlThreadHandoff`,
  `realtimeBusConstructionAllocatesNothing`, and
  `realtimeConstructorDoesNotTakeEngineChangeLockHeldByAnotherThread` (state Pending vs
  Registered).
- GREEN (restored): `git apply fix.patch` EXIT=0 (`apply-forward.log`); rebuild EXIT=0
  (`build-3-green.log`); `ctest -R 'AudioBus'` EXIT=0, `100% tests passed out of 3`
  (`test-3-green.log`). The final rebuild of `zene` plus the three tests EXIT=0
  (`build-6-final.log`) and the final family run EXIT=0 (`test-6-final-green.log`).
- Targeted held-lock mutation (proves the deadlock test itself is RED): remove the realtime
  branches from `AudioBusHandle::AudioBusHandle` and `AudioEngine::addAudioBusHandle`, so the
  constructor takes the model lock (`heldlock-mutation.patch`, 47 lines). Build EXIT=0
  (`build-4-heldlock-red.log`); `./AudioBusPlayHandleQueueTest
  realtimeConstructorDoesNotTakeEngineChangeLockHeldByAnotherThread` EXIT=1 ("the realtime
  constructor did not return within 2 s: it blocked on the change lock held by another thread",
  `test-4-heldlock-red.log`). Reverse the mutation EXIT=0
  (`apply-heldlock-reverse.log`), rebuild EXIT=0 (`build-5-heldlock-green.log`), rerun the same
  test EXIT=0 (`test-5-heldlock-green.log`).
- Gates: `python3 .../feature_check.py <worktree>` EXIT=0, `RESULT: PASS`, eight rows PASS
  (`gate-1.log`). `git status --porcelain` empty after the mutation was reversed.
- GUI collateral check: `QT_QPA_PLATFORM=offscreen python3 tests/control-shell.py build/zene
  <logs>/ui` EXIT=0, `PASS: shell control-surface driver (every check held)` (`ui-harness.log`),
  three `window.screenshot` PNGs in `logs/ui/` (sha256 of `shell-arrange.png`:
  `7b8bf222e67825ed63c52f166840db7163de88db6be4ec6824be88443553a68f`).
- Completion check: `python3 .../submit_check.py <worktree> new-p15-253` EXIT=0,
  `RESULT: PASS - submit` (`submit-1.log`).

## Realtime contract

- The realtime constructor allocates and frees nothing: `realtimeBusConstructionAllocatesNothing`
  (`tests/src/core/AudioBusPlayHandleQueueTest.cpp:403`) counts zero allocations and zero frees
  around a placement construction inside `RealtimeScope`.
- It does not take the engine lock even when another thread holds it:
  `realtimeConstructorDoesNotTakeEngineChangeLockHeldByAnotherThread` (`:483`) holds
  `m_changeMutex` on the main thread, constructs on a worker inside `RealtimeScope`, and requires
  the constructor to return; the targeted mutation above makes that test fail.
- The only control-thread work is the timer's `serviceAudioBusHandlesOnControlThread()`; no new
  lock, allocation, log or wait was added to a render path. (Local TSan does not run on this box,
  so the cross-thread argument is the seq_cst `enqueued` exchange in `linkOnce()` plus the
  one-consumer `exchange(nullptr)` in each service phase, not a TSan run.)

## Legacy tests reconciled with the control-thread contract

- The two pre-existing audio-bus tests that construct a bus on the control thread
  (`audioBusHandle48FrameBlock`, `realtimeBusAppendUsesInlineSlotWithoutAllocation`) are unchanged
  and pass; a control-thread bus still prepares storage inline.
- The two tests written for the earlier bounded-queue design
  (`realtimeRegistrationsBeyondQueueCapacityAreAllRegistered`,
  `realtimeDestructionsBeyondQueueCapacityAreAllFreed`) now assert the intrusive-link contract:
  no capacity refusal exists, so all 200 realtime registrations are registered and all 200
  realtime destructions are freed (200/200 in the GREEN run).

## Self-check (answers to the submit_check ASK items)

- **RED/GREEN exit codes.** Reversing the product patch gives EXIT=8 with two failing test
  binaries (`test-2-red.log`); restoring it gives EXIT=0 (`test-3-green.log`, `test-6-final-green.log`).
  The focused held-lock mutation is EXIT=1 vs EXIT=0 (`test-4-heldlock-red.log`,
  `test-5-heldlock-green.log`).
- **Determinism.** No sleep or wall-clock assertion. The realtime cases enter `RealtimeScope` and
  drive the control-thread service explicitly (`serviceAudioBusHandlesOnControlThread`,
  `flushOnControlThread`); the held-lock test forces the lock order and uses a bounded
  `wait_for(2 s)` only as a deadlock detector after which it releases the lock and joins the
  worker, making no timing assertion. There is no `sleep_for`, `sleep`, `usleep` or `msleep` in
  either test file.
- **Cause and completeness.** See the every-caller audit: the only heap bus is the standalone
  sample bus, destroyed through `destroyAudioBusHandle`; every realtime-to-control request
  (registration, removal/destruction, port) uses a per-bus intrusive link; the render period
  neither registers nor promotes a bus; the service order is registrations, retirements, ports.
- **No collateral damage.** Control-thread buses still register inline and saved projects are
  untouched (no format or model change). The intrusive links cannot be exhausted, so no new limit
  rejects callers: the 200-bus burst tests register/free all 200 (the old queue would have
  dropped at 64). The GUI harness still passes end to end (`ui-harness.log`).
- **Verdict-specific items.** N/A: the verdict is FIXED, not ALREADY-FIXED or FALSE-POSITIVE.

## Other findings (not fixed here)

- The engine destructor (`src/core/AudioEngine.cpp`) joins its workers and stops the timer but
  does not drain the three bus stacks. Any link still queued at engine destruction is not
  serviced there. This is unchanged by this fix and out of scope for the deadlock/allocation
  finding.
