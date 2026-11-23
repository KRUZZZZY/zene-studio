FIXED
# new-p15-253: an audio bus built on the realtime path is deferred to the control thread

Verdict: **REAL, FIXED** (round 11). Two parts, both at the cause:

1. A bus constructed while `RealtimeScope` is active no longer takes the engine's model lock,
   grows the bus list, prepares storage, or calls the audio device. It marks itself `Pending`,
   is pushed onto an intrusive per-bus registration link, requests its device-port state through
   a second link, and returns. `serviceAudioBusHandlesOnControlThread()` is the only activation
   point and runs on the control thread from the play-handle capacity timer.
2. The standalone (sample play handle) bus call site now names the bus with
   `QStringLiteral("SamplePlayHandle")`, so the audio-path construction of that bus does not build
   a heap `QString` for its name either.

## The bug (current line numbers)

- Realtime constructor branch: `src/core/AudioBusHandle.cpp:88-96` calls only
  `AudioEngine::addAudioBusHandle(this)` and `setExtOutputEnabled(true)` and returns; storage and
  the effect chain are prepared on the control thread.
- Before this branch existed, the same constructor ran `prepareStorage()` (allocates the buffer and
  effect chain, `:98`) and `AudioEngine::addAudioBusHandle()` inline, which took `m_changeMutex`
  and did `m_audioBusHandles.push_back` (`src/core/AudioEngine.cpp:1203-1205`), then called the
  device (`src/core/AudioBusHandle.cpp:156-163`). A render worker that did that waits for
  `m_changeMutex`, which the render owner holds for the whole period (`renderNextPeriod()` locks
  it at `src/core/AudioEngine.cpp:932`) and cannot release until that worker returns: a deadlock.
  On the render owner the same path allocates in-period. `RealtimeScope` is active on both paths
  (`src/core/AudioEngine.cpp:934`).
- Round 11: the standalone bus was created with `new AudioBusHandle("SamplePlayHandle", false)`
  at `src/core/SamplePlayHandle.cpp:78`; the `const char*` to `const QString&` conversion builds a
  heap `QString` (`QArrayData`, i.e. `malloc`) on the audio path for every standalone bus.
  `docs/KNOWN-LIMITATIONS.md:2900-2901` records that Qt's implicitly shared containers allocate
  through `QArrayData`/`malloc`, which the operator-new allocation probe does not count.

## The fix (file:line at HEAD)

- Realtime constructor: `src/core/AudioBusHandle.cpp:88-96`. No `prepareStorage`, no lock, no
  device call; the port request is `setExtOutputEnabled()` (`:151-163`) ->
  `AudioEngine::requestDevicePortUpdate()` (`src/core/AudioEngine.cpp:1426-1433`).
- Registration link: `src/core/AudioEngine.cpp:1183-1197`. State `Pending` is stored before the
  link (`:1191-1192`); `linkOnce()` (`:1172-1178`) exchanges the per-bus `enqueued` flag and pushes
  onto `m_registrationHead` - lock-free, no allocation. The control-thread branch (`:1199-1221`)
  takes the model lock, appends, and marks the bus `Registered`.
- The single activation point: `:1314-1322` runs `serviceRegistrations()` (`:1325-1355`, storage
  and append under `m_changeMutex`; a retired bus is marked `Handled` and never registered), then
  `serviceRetirements()` (`:1357-1383`, free only a bus with no queued link), then
  `servicePortUpdates()` (`:1386-1414`, coalesced wanted/applied state). The timer calls it once
  per tick (`:380`). `renderNextPeriod()` does not call it and does not promote a bus (no
  service/registration/promotion call in `:930-1010`).
- Removal and destruction: `:1222-1247`, `:1249-1268`, release `:1280-1296`. A worker sets
  `Retired` and links the retirement stack; the control thread frees only when `hasQueuedLinks()`
  is false (`include/AudioBusHandle.h:158-162`).
- Standalone bus name: `src/core/SamplePlayHandle.cpp:78` now passes
  `QStringLiteral("SamplePlayHandle")`, which is static data (no heap `QString` per bus).
- Data: `include/AudioBusHandle.h:82-92` (`RegistrationState`), `:254` (`m_registrationState`),
  `:263-266` (links); `include/AudioEngine.h:260`, `:701-707`, `:731-733`.
- Lifetime contract: `include/AudioBusHandle.h:66-71`; the only heap bus in `src/` is the
  standalone sample bus (`src/core/SamplePlayHandle.cpp:78`), destroyed through
  `AudioEngine::destroyAudioBusHandle` at `:445`.

## Every-caller audit

- `new AudioBusHandle` in product code: `src/core/SamplePlayHandle.cpp:78` only. Stack/track buses
  (`src/core/AudioEngine.cpp:148` and the mixer/track construction) are created on the control
  thread and take the non-realtime branch.
- `addAudioBusHandle` callers: the constructor (`src/core/AudioBusHandle.cpp:92`, `:110`) and the
  tests; no plugin constructs a bus.
- `removeAudioBusHandle`: the bus destructor (`src/core/AudioBusHandle.cpp:143`).
- `setExtOutputEnabled`: constructor and destructor; both use the same wanted/applied state.
- No `promotePendingAudioBusHandles`, MpscQueue bus queue or held-bus list remains
  (`rg` over `src include`).

## State sequences

Driven by `AudioBusHandleTest::realtimeBusRegistrationStatesFollowTheControlThreadHandoff`
(`tests/src/core/AudioBusHandleTest.cpp:521`):

- `Pending -> Registered`: a bus constructed on the audio path is `Pending` until
  `serviceAudioBusHandlesOnControlThread()` registers it.
- `Pending -> Retired -> Handled`: a bus retired while its registration is pending is marked
  `Retired`, the service consumes the link and marks it `Handled`, and it is never listed.

## Regression test for round 11

`AudioBusPlayHandleQueueTest::realtimeStandaloneBusNameIsStaticAcrossConstructions`
(`tests/src/core/AudioBusPlayHandleQueueTest.cpp:486`) builds the real product path - two
`SamplePlayHandle`s that own their bus - inside one `RealtimeScope`, with the allocation probe
armed. It requires (a) exactly the four object allocations (two handles, two buses; the empty
`BufferManager` period keeps the handle buffers out), (b) both buses `Registered` after the
control-thread service, and (c) the two live buses' `name().constData()` to be the same pointer,
i.e. one static `QStringLiteral` buffer rather than a heap `QString` per bus. Before the fix two
heap buffers exist and the pointers differ; Qt's `malloc` is invisible to the operator-new probe,
so the pointer identity is the observable for the name (see the test comment and
`docs/KNOWN-LIMITATIONS.md:2900-2901`).

## Evidence commands run in this session (round 11)

Logs: `/home/kruzzzzy/.config/kads/zene/state/runs/new-p15-253.r11/logs`. Each log's last line is
`EXIT=n` for the command it records, except the compile-failure logs noted below.

- RED for the name fix (only that fix not in the tree; all other product code at HEAD): build
  `cmake --build build --target AudioBusPlayHandleQueueTest -j4` EXIT=0 (`build-6-red.log`);
  `./AudioBusPlayHandleQueueTest realtimeStandaloneBusNameIsStaticAcrossConstructions` EXIT=1
  (`test-3-red.log`: "each standalone bus built its own heap QString name; the call site is not
  QStringLiteral").
- GREEN: after `src/core/SamplePlayHandle.cpp:78` became `QStringLiteral(...)`, build EXIT=0
  (`build-7-green.log`); the same focused run EXIT=0 (`test-4-green.log`); the final focused run
  EXIT=0 (`test-8-focused-green.log`).
- Whole product patch RED (non-test hunks only): `git diff 730db9d871 --
  include/AudioBusHandle.h include/AudioEngine.h src/core/AudioBusHandle.cpp
  src/core/AudioEngine.cpp src/core/SamplePlayHandle.cpp > fix.patch` EXIT=0, 762 lines
  (`fix.patch`, `fix-patch-err.log`); `git apply -R fix.patch` EXIT=0 (`apply-reverse.log`); build
  the three AudioBus tests EXIT=0 (`build-12-red.log`); `ctest -R 'AudioBus'` EXIT=8
  (`test-10-red.log`): `33% tests passed, 2 tests failed out of 3`, including
  `realtimeStandaloneBusNameIsStaticAcrossConstructions` (pointer mismatch) and the r10 design
  tests.
- Whole product patch GREEN (restored): `git apply fix.patch` EXIT=0 (`apply-forward.log`); build
  `zene` plus the three tests EXIT=0 (`build-13-green.log`); `ctest -R 'AudioBus'` EXIT=0,
  `100% tests passed out of 3` (`test-11-green.log`).
- Final family and gate runs: `ctest -R 'AudioBus'` EXIT=0 (`test-9-family-green.log`).
  `python3 .../feature_check.py <worktree>` EXIT=0, `RESULT: PASS`, all eight rows PASS
  (`gate-2.log`). An earlier run on the same tree failed only `file-length` with
  `tests/src/core/AudioBusPlayHandleQueueTest.cpp (511)` (`gate-1.log`, EXIT=1); the new test was
  then compacted to 500 code lines and the gate passes.
- GUI harness: `QT_QPA_PLATFORM=offscreen python3 tests/control-shell.py build/zene <logs>/ui`
  EXIT=0, `PASS: shell control-surface driver (every check held)` (`ui-harness.log`); three
  `window.screenshot` PNGs in `logs/ui/` (sha256 of `shell-arrange.png`:
  `a75fab4878fa0f1865c5e1b288f5723d6669fced0d7be96a2dcc716518d9764d`).

Intermediate compile attempts, recorded for completeness: without `BufferManager.h` the test
failed to compile (`build-1-red.log`, `build-2-red.log`) and with `const char*` for
`QString::constData()` it failed again (`build-5-red.log`, `build-6-red.log`); both were fixed in
the test before the RED/GREEN runs above.

## Self-check (answers to the submit_check ASK items)

- **RED/GREEN exit codes.** The name regression is RED at EXIT=1 focused (`test-3-red.log`) and
  GREEN at EXIT=0 (`test-4-green.log`, `test-8-focused-green.log`). The whole-product patch gives
  `ctest` EXIT=8 (`test-10-red.log`) and EXIT=0 (`test-11-green.log`) with the final test file.
- **Determinism.** No sleep or wall-clock assertion. The realtime cases enter `RealtimeScope` and
  drive the control-thread service explicitly. The name assertion compares two live pointers; heap
  QStrings are distinct while both live, so there is no allocator-reuse race. The held-lock test
  (r10) forces the lock order and uses a bounded `wait_for(2 s)` only as a deadlock detector after
  which it releases the lock and joins the worker. No `sleep_for`, `sleep`, `usleep` or `msleep` in
  either test file.
- **Cause and completeness.** The only heap bus is the standalone sample bus; its call site now
  uses `QStringLiteral` and its destruction goes through `destroyAudioBusHandle`. Every
  realtime-to-control request (registration, removal/destruction, port) uses a per-bus intrusive
  link; the render period neither registers nor promotes a bus.
- **No collateral damage.** Control-thread buses still register inline; saved projects and the
  on-disk format are untouched. No new limit rejects callers: the 200-bus burst tests
  register/free all 200. The GUI harness still passes end to end (`ui-harness.log`). The untracked
  `Testing/` directories are `build/Testing` and `build/tests/Testing`; `.gitignore:1`
  (`/build*/`) excludes both, and `git status --porcelain --untracked-files=all` lists only the
  three modified files (no `??` entries).
- **Verdict-specific items.** N/A: the verdict is FIXED.

## Other findings (not fixed here)

- The engine destructor joins its workers and stops the timer but does not drain the three bus
  stacks. A link still queued at engine destruction is not serviced there. Unchanged by this fix
  and out of scope for the deadlock/allocation finding.
