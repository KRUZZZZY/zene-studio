FALSE-POSITIVE

# Cluster new-p17-44: an unprepared realtime buffer request can return null to callers

Finding: "On a realtime worker thread, `valueBuffer()` returns `nullptr` when the buffer was not
prepared for the period. The previous implementation returned the buffer address; callers that
assume it is non-null can dereference it and crash." (where: `src/core/Controller.cpp:138`)

Verdict: **FALSE-POSITIVE**. The null return exists and is intended: it is the same
"no sample-exact data" signal `AutomatableModel::valueBuffer()` has always sent through the same
call, the one caller of `Controller::valueBuffer()` tests for it, and the missed preparation is
counted in the engine instead of being dereferenced. There is no crash to fix, and replacing the
null with a stale non-null buffer would break the documented null-buffer contract.

## Where the code is now (line numbers moved; `` src/core/Controller.cpp ``)

- `src/core/Controller.cpp:150-163` - `Controller::valueBuffer()`. The only null return is
  `155-159`: `RealtimeScope::isActive() && !Engine::audioEngine()->isRenderThread()` and
  `m_bufferLastUpdated != s_periods`. Every other thread (the render owner, the control/GUI
  thread) takes `160 prepareValueBufferForRender()` and returns `162 &m_valueBuffer`.
- `src/core/Controller.cpp:134-147` - the scalar sibling `Controller::value()`. The same guard
  (`139-143`) answers with `m_currentValue`, the scalar the serial pass publishes at
  `src/core/Controller.cpp:175`, rather than with a buffer. The finding's line 138 is this guard
  block, not a dereference.
- `src/core/Controller.cpp:167-177` - `prepareValueBufferForRender()`, the serial pass entry
  point; idempotent per period.

## Why no caller can dereference it

1. **Null is this API's documented "no data" answer, through the very same expression.**
   `include/AutomatableModel.h:244-245`: "`@return` pointer to model's valueBuffer when
   s.ex.data exists, NULL otherwise". `src/core/AutomatableModel.cpp:1048-1051`: "if we have no
   sample-exact source for a ValueBuffer, return NULL to signify that no data is available at the
   moment in which case the recipient knows to use the static value() instead".
   `AutomatableModel::valueBuffer()` itself returns null to the same consumers on the same
   condition (`src/core/AutomatableModel.cpp:806-815`, `843`), so a null that arrives via a
   controller is not a new failure mode for any consumer - the consumers were written against it
   (e.g. `src/core/MixerSendPan.cpp:71-84`, `plugins/LadspaEffect/LadspaEffect.cpp:204-221`,
   `src/core/MixerSurroundMix.cpp:78`).
2. **The only caller in the tree tests it.** `include/ControllerConnection.h:76-79` forwards to
   `m_controller->valueBuffer()`; the single product call site of that forwarding is
   `src/core/AutomatableModel.cpp:980-981` - `auto vb = m_controllerConnection->valueBuffer();`
   `if( vb ) { ... }` - and a null there just means "fall through to the non-sample-exact path"
   (lines `1008-1051`). `rg -n 'valueBuffer\(\)->'` over `src/ include/ plugins/` finds no other
   consumer of a controller buffer.
3. **The scenario the finding describes cannot reach a worker in the product.** The finding's
   "when" is "a worker requests a controller buffer before the serial preparation pass has
   stamped it". The serial pass stamps every registered model, and each model's prepass stamps
   its controller (`src/core/AutomatableModel.cpp:976`), *before* any worker job exists:
   `src/core/AudioEngine.cpp:1079` (`prepareAutomatableModelBuffers()`, commented "before
   instrument, effect or mixer jobs can read their fixed buffers") runs in
   `renderStageNoteSetup()`, and the dispatch is `src/core/AudioEngine.cpp:1111-1112`
   (`fillJobQueue` / `startAndWaitForJobs`). A worker that reads a *model* buffer does not reach
   the controller at all: `AutomatableModel::valueBuffer()`'s worker branch (`806-815`) records
   and returns before any controller lookup.
4. **The missed preparation is surfaced separately, which is the finding's own second remedy.**
   `src/core/Controller.cpp:157` records it in `AudioEngine`:
   `include/AudioEngine.h:549-553` (`recordUnpreparedControllerBufferRequest`, one relaxed atomic
   add - no allocation, no lock, no log on the audio thread) and it is published at
   `include/AudioEngine.h:561-564`. `tests/src/core/PhaseDSidechainTest.cpp:692-699` asserts the
   counter stays flat across a real parallel render driven by one shared LFO, and
   `tests/src/core/PhaseDSidechainTest.cpp:658-668` shows the same "report, do not fall back
   silently" shape for an unregistered model.
5. **A non-null fallback would be the regression, not the fix.**
   `docs/m0-design/automation-lockfree.md:25`: "Null-buffer behavior, sample count, model fitting,
   automation transforms, interpolation endpoints, and controller evaluation frequency stay
   unchanged." Returning `&m_valueBuffer` while unstamped would hand `AutomatableModel` a stale
   period buffer and make it mark `m_hasSampleExactData = true` (line `1008-1020`), i.e. claim
   sample-exact data it does not have, exactly what the contract forbids.

This is not ALREADY-FIXED either: no commit on the branch was needed to prevent a crash here. The
guard rails (`value()`'s scalar fallback, the miss counter) came in with the automation-prepass
work (`c28751cef2`), and the null is the pre-existing optional-buffer contract, not a new bug that
someone then fixed.

## What I added (a contract pin, not a regression test)

`tests/src/core/PhaseDSidechainTest.cpp:721-798`,
`PhaseDSidechainTest::unpreparedControllerBufferRequestIsNullAndReported`. It drives the finding's
exact path with every stamp moved by hand - no clock, no sleep, no race:

- control side reads the connected model once while the controller covers the period (publishes
  the scalar), then `Controller::triggerFrameCounter()` moves the controller's stamp;
- a `std::thread` enters `RealtimeScope` (a mixer job, `isRenderThread()` false) and calls
  `gain.value()`, `gain.controllerConnection()->valueBuffer()`, `gain.valueBuffer()`;
- asserts: the controller buffer is null, the model buffer is null, the scalar read returns the
  published value bit-for-bit, the controller miss counter moved by exactly 1 for the scalar read
  and +1 again for the buffer request, the model miss counter by 1;
- then `gain.prepareValueBufferForRender()` on the control side and asserts both buffers are
  non-null for `framesPerPeriod()` (256 here) and that nothing more is counted.

Because the verdict is FALSE-POSITIVE there is no product diff, so this test cannot fail before a
fix and pass after one - it passes on the unchanged tree (EXIT 0 below) and is there to catch a
future change that hands an unprepared buffer to a worker, stops counting the miss, or returns
null from the prepared path.

Test matrix checked for this area: normal path (prepared for the period -> non-null, not counted),
failure path (unstamped worker -> null, counted, scalar fallback), reconfiguration (`setSampleExact`
/ `setControllerConnection` / `resetFrameCounter` all just move a stamp; no pool, no capacity, so
there is no exhaustion path for a controller buffer - it is one fixed `ValueBuffer` member per
controller, sized from `framesPerPeriod()` at construction), audio-thread constraints (the only
worker-side work is a relaxed atomic add and a pointer return; no allocation, lock, wait or log
was added), platforms (plain C++/Qt, no POSIX-only paths, no absolute-path test).

## Commands and EXIT codes (copied from the logs in `new-p17-44.r1/logs`)

| # | Command | EXIT | Log |
| --- | --- | ---: | --- |
| 1 | `cmake --build build --target PhaseDSidechainTest AutomationPrepassProofTest -j4` | 0 | `logs/build-1.log` |
| 2 | `ctest -R '^(PhaseDSidechainTest\|AutomationPrepassProofTest)$' --output-on-failure` (from `build/tests`) | 0 | `logs/test-1.log` (2/2 passed) |
| 3 | `cmake --build build --target PhaseDSidechainTest -j4` (after adding the contract test) | 0 | `logs/build-2.log` |
| 4 | `ctest -R '^PhaseDSidechainTest$' --output-on-failure` (from `build/tests`) | 0 | `logs/test-2.log` (1/1 passed) |
| 5 | `./PhaseDSidechainTest unpreparedControllerBufferRequestIsNullAndReported` (from `build/tests`) | 0 | `logs/test-3-single.log` |
| 6 | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 0 | `logs/gate-feature-check.log` |
| 7 | `ctest -R '^(PhaseDSidechainTest\|AutomationPrepassProofTest\|AutomatableModelTest)$'` (from `build/tests`), before `AutomatableModelTest` was built | 8 | `logs/test-4-final.log` - `24 - AutomatableModelTest (Not Run)`, i.e. a missing binary, not a failing test |
| 8 | `cmake --build build --target AutomatableModelTest -j4` | 0 | `logs/build-3.log` |
| 9 | the same three-test `ctest -R` after that build | 0 | `logs/test-5-final.log` (3/3 passed) |

Step 5 printed, verbatim from `logs/test-3-single.log`:

```
PASS   : PhaseDSidechainTest::initTestCase()
BUG7_UNPREPARED_CONTROLLER_BUFFER reported=2 fallback=0.500000 prepared_length=256
PASS   : PhaseDSidechainTest::unpreparedControllerBufferRequestIsNullAndReported()
PASS   : PhaseDSidechainTest::cleanupTestCase()
Totals: 3 passed, 0 failed, 0 skipped, 0 blacklisted, 486ms
```

Step 6 printed, verbatim from `logs/gate-feature-check.log`:

```
feature-check kads-new-p17-44 (base fa4a5f2e7, 430 changed files; logs in .../build/feature-check)
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

`build-1.log` and `build-2.log` contain no `error:`; `test-1.log` reports
`100% tests passed out of 2`, `test-2.log` `100% tests passed out of 1`.

## Self-check

- **"The regression test FAILS with your product change reverted and PASSES with it (exit codes)."**
  Not applicable, and not claimed: the verdict is FALSE-POSITIVE, so there is no product change to
  revert (`git diff -- src include plugins` for this cluster is empty). The test I added is a
  contract pin for the described path; it passes on the unchanged tree (EXIT 0,
  `logs/test-3-single.log`, step 5) and would fail if a future change made an unprepared worker
  query hand out a non-null buffer, stopped counting the miss, or returned null from the prepared
  path. I have not run a RED/GREEN pair for it because there is no fix to prove.
- **"The test is deterministic: no wall-clock timing, sleeps or races."** It uses no clock and no
  sleep. Every period stamp is moved explicitly (`Controller::triggerFrameCounter()`,
  `gain.prepareValueBufferForRender()`), the worker is a joined `std::thread` whose result is read
  only after `join()`, and the scalar is compared bit-for-bit (`std::bit_cast<std::uint32_t>`) with
  the value the control side published before the period moved. Counters are compared as deltas
  captured around each call inside the worker, so other tests leaving state behind cannot affect
  them.
- **"The fix is at the cause and complete: every caller and sibling of what you changed was
  checked."** No product code changed. For the verdict I walked every path into
  `Controller::valueBuffer()`: `include/ControllerConnection.h:76-79` (forwarder) ->
  `src/core/AutomatableModel.cpp:980-981` (the only caller, null-tested); every other
  `valueBuffer()` consumer in `src/ include/ plugins/` reads `AutomatableModel::valueBuffer()`,
  which is null-able by its own contract (`include/AutomatableModel.h:244-245`). The sibling
  `Controller::value()`'s fallback (`src/core/Controller.cpp:139-143`) and the model-level sibling
  branch (`src/core/AutomatableModel.cpp:806-815`) have the same shape - record, then return.
- **"No collateral damage."** Only a test file changed, so no project load/save, render or command
  behaviour can move; `tests/src/core/PhaseDSidechainTest.cpp` already existed and is registered in
  `tests/CMakeLists.txt:675`, so no list file needed an entry (the fork-sources gate agrees, exit 0).
  No limit or check was added.
- **"If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the intended
  contract that shows it."** `src/core/Controller.cpp:155-159` returns null only on a realtime
  non-render thread with a stale stamp; `src/core/AutomatableModel.cpp:980-981` is the only caller
  and tests for null; `include/AutomatableModel.h:244-245` and
  `src/core/AutomatableModel.cpp:1048-1051` state null means "no sample-exact data, use the static
  value"; the repair the finding asks for exists at `include/AudioEngine.h:549-564` +
  `src/core/Controller.cpp:157` (surface the miss separately) and at
  `src/core/AudioEngine.cpp:1079` (the pass covers every registered model before worker dispatch).
- **"Sibling commands in the command files you changed..."** I changed no command file and added no
  control command (the brief's realtime/command rule). The list the check prints (`app.*`,
  `arrangement.*`, ...) comes from the branch's earlier commits between the merge base and HEAD, not
  from this cluster's patch; `git show --stat` for my commits touches only
  `tests/src/core/PhaseDSidechainTest.cpp` and this report.

## Other findings (not fixed here, outside this cluster)

1. `include/Controller.h:167-168` - `m_currentValue` and `m_sampleExact` are declared without an
   initialiser and the constructor's init list (`src/core/Controller.cpp:65-70`) does not set them.
   `m_currentValue` is the fallback that `value()` hands to an unprepared worker
   (`src/core/Controller.cpp:142`) and is only written by a control-side `currentValue()` or by a
   later period's prepass, so a controller created mid-period can hand a worker an indeterminate
   float before either has run. Not this finding (which is about the null *buffer*) and I did not
   change it; a fix would be an initialiser in the constructor.
2. `ControllerType::Dummy` controllers never call `setSampleExact()` (only
   `src/core/LfoController.cpp:65`, `src/core/PeakController.cpp:57`,
   `src/core/midi/MidiController.cpp:46` do), so `isSampleExact()` can read the same uninitialised
   member for a Dummy controller.
3. `Controller::value()` and `Controller::valueBuffer()` both take a *fresh* miss count per call:
   one unprepared worker read of a sample-exact parameter during a period in which the prepass
   missed it records two misses (visible in the test output as `reported=2`). Harmless for the
   counter's purpose, but the two counts are not one-per-request-per-parameter.
