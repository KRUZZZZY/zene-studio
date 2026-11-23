ALREADY-FIXED

# Cluster review-f1a74cc54 — automated sidechain mixer update and the audio-path mutex

## Verdict

ALREADY-FIXED on `bugs/qwen-review-f1a74cc54`. The reported lock is not in the current
`AutomatableModel::valueBuffer()` at all, and the sidechain call site now takes the same
render-prepared, lock-free lookup as every other mixer consumer.

The finding is a re-report of cluster 2647. It was fixed on this branch in two commits,
both ancestors of the branch tip:

- `c674877bc5` — "the sidechain amount read waited on the amount's valueBuffer mutex
  (cluster 2647, review f1a74cc54)". This added the named regression
  `PhaseDSidechainTest::theSidechainAmountReadTakesNoLock` and first moved the sidechain
  read off the mutex.
- `c28751cef2` — "prepare model buffers before worker jobs (cluster 2647)". This replaced
  the one-caller exception with the general design: every registered model is prepared
  serially after `Song::processNextBuffer()` and before mixer jobs, and the render-time
  `valueBuffer()` is a prepared-cache lookup. `Mixer.cpp` moved back to the ordinary
  `valueBuffer()` call and the sole-reader API was removed.

`git merge-base --is-ancestor c28751cef HEAD` exited 0 (see the command log below), so the
fix is present on the branch tip, not merely in history.

## Code evidence (line numbers at this branch tip)

- `src/core/Mixer.cpp:654` — `sumSidechainInputs` reads the automated amount through
  `route->amount()->valueBuffer()`, the same prepared lookup used by faders and sends.
  `sumSidechainInputs` itself is called from `MixerChannel::process` at
  `src/core/Mixer.cpp:991`, i.e. inside the mixer stage.
- `src/core/AutomatableModel.cpp:796` — `AutomatableModel::valueBuffer()`.
  - `:804` — `if (RealtimeScope::isActive())` is the audio-thread branch.
  - `:806-815` — a cache miss on the render thread is computed by
    `prepareValueBufferForRender()` (no mutex); a cache miss on a worker returns
    `nullptr` and increments `recordUnpreparedAutomatableBufferRequest()` (no mutex, no
    wait, no growth).
  - `:818-824` — a period-cache hit returns `cachedValueBuffer()` directly. The comment
    records why that is a plain read: control writes are serialised against the render
    period by the engine's change lock, which the writer takes and the reader never does.
  - `:828-838` — the only guarded path is the control-side query, outside
    `RealtimeScope`; it is not an audio-thread path.
- `src/core/AutomatableModel.cpp:841` — `cachedValueBuffer()` is a period-stamp check and
  returns the model-owned buffer, no lock.
- `include/AutomatableModel.h:677-678` — the remaining `m_valueBufferMutex` is explicitly
  a legacy test sentinel: "Legacy lock retained as a test sentinel; product code no longer
  acquires it." `rg -n "QMutexLocker|m_valueBufferMutex" src/core/AutomatableModel.cpp`
  finds no acquisition. The only users of the sentinel are the regression tests
  (`tests/src/core/AutomatableModelTest.cpp:52`, `tests/src/core/PhaseDSidechainTest.cpp:580`).
- `src/core/AudioEngine.cpp:1065-1069` — `Song::processNextBuffer()` publishes the period's
  automation and ramps, then `prepareAutomatableModelBuffers()` walks the registered model
  registry once, serially, before any worker job.
- `src/core/AudioEngine.cpp:1210-1212` — the whole render period runs under
  `m_changeMutex` and inside `RealtimeScope`; `:1230` calls `renderStageMix()`, which owns
  the mixer path. Mixer jobs therefore see the cache prepared at `:1069`, never compute it
  and never take a model lock.
- The ramp output the finding asks to preserve is still per frame:
  `src/core/AutomatableModel.cpp:946-960` fills the value buffer from a published
  `AutomationRamp` and the `sumSidechainInputs` loop at `src/core/Mixer.cpp:662-668` reads
  `amountBuf->value(f)` per frame.

## Regression evidence

The named regression for this finding already exists:
`tests/src/core/PhaseDSidechainTest.cpp:557` (`theSidechainAmountReadTakesNoLock`). It
holds the amount's legacy `m_valueBufferMutex` on another thread, runs
`receiver->sumSidechainInputs(fpp)`, and requires the sum to finish and keep the ramp
shape. It is deterministic in the passing direction: a promise/future signals that the
holder owns the sentinel before the sum starts, and that the sum returns; the 2 s bound
only separates "returned" from "blocked" with a wide margin (green run: 502 ms).

I also ran a negative control this session: I temporarily put the historical shape back at
the top of `AutomatableModel::valueBuffer()`
(`QMutexLocker negativeControl(&m_valueBufferMutex);`), rebuilt, and the existing test
failed with exactly the finding's symptom. Then I removed the line, verified the two
product files were byte-identical to their pre-control sha256, rebuilt, and the test
passed again.

## Commands and exit codes

All build/test logs are in
`/home/kruzzzzy/.config/kads/zene/state/runs/review-f1a74cc54.r1/logs/`.

| # | Command | EXIT |
| --- | --- | --- |
| 1 | `git merge-base --is-ancestor c28751cef HEAD` | 0 |
| 2 | `cmake --build build --target PhaseDSidechainTest -- -j4` (log `build-1.log`) | 0 |
| 3 | `ctest -R PhaseDSidechainTest --output-on-failure` from `build/tests` (log `test-1.log`) | 0 |
| 4 | `./PhaseDSidechainTest theSidechainAmountReadTakesNoLock` (log `test-2.log`) | 0 |
| 5 | negative control: added `QMutexLocker negativeControl(&m_valueBufferMutex);` to `valueBuffer()` | n/a |
| 6 | `cmake --build build --target PhaseDSidechainTest -- -j4` (log `build-2-negative-control.log`) | 0 |
| 7 | `./PhaseDSidechainTest theSidechainAmountReadTakesNoLock` (log `test-3-negative-control.log`) | **1** — `FAIL! : ... 'completed' returned FALSE. (the sidechain sum waited on the amount's valueBuffer() mutex (cluster 2647))`, `Totals: 2 passed, 1 failed`, 2482 ms |
| 8 | remove the negative-control line; `sha256sum -c logs/pre-control-sha256.txt` | 0 (`OK` for both files) |
| 9 | `cmake --build build --target PhaseDSidechainTest -- -j4` (log `build-3-restored.log`) | 0 |
| 10 | `./PhaseDSidechainTest theSidechainAmountReadTakesNoLock` (log `test-4-green.log`) | 0 — `Totals: 3 passed, 0 failed`, 502 ms |
| 11 | `ctest -R PhaseDSidechainTest --output-on-failure` from `build/tests` (log `test-5-ctest-green.log`) | 0 — `100% tests passed out of 1` |
| 12 | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (log `gates-1.log`) | 0 |

Gate table (unpiped exit 0):

```
feature-check kads-review-f1a74cc54 (base fa4a5f2e7, 349 changed files; logs in .../build/feature-check)
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

## Test matrix considered

- Normal path: mixer render with an automated sidechain amount (`PhaseDSidechainTest`
  automation/sidechain cases, all green in the `ctest -R PhaseDSidechainTest` run).
- Failure / blocking path: the held-sentinel probe above; green at HEAD, red when the
  historical mutex is reintroduced.
- Exhaustion / reconfiguration: the value-buffer path has no new capacity, allocation or
  growth; the registry growth/refusal behaviour and its tests were part of
  `c28751cef` (`AudioEngine::registerAutomatableModel`, `prepareAutomatableModelBuffers`)
  and were not touched here. `sumSidechainInputs` still clears and reuses the fixed
  pre-allocated `m_sidechainBuffer` and per-route tap buffers.
- Audio-thread constraints: `valueBuffer()` on `RealtimeScope` performs only atomic loads,
  period stamps and pointer returns. It has no `QMutexLocker`, no allocation, no signal
  and no wait; a worker cache miss returns `nullptr` and counts the miss instead of
  computing.
- Supported platforms: the change that fixes this finding is already built and tested on
  the branch; no new platform-specific code was added here. The value handoff used by the
  fix is guarded by `static_assert(std::atomic<uint64_t>::is_always_lock_free)` at
  `src/core/AutomatableModel.cpp:46-51`.

## Self-check

**The regression test FAILS with your product change reverted and PASSES with it (exit
codes).** This is an ALREADY-FIXED verdict, so there is no product change in this session.
As the equivalent control I reintroduced the historical `QMutexLocker` at the top of
`valueBuffer()`: `PhaseDSidechainTest::theSidechainAmountReadTakesNoLock` exited 1 with
`'completed' returned FALSE` after 2482 ms. With the line removed and the file restored to
its exact pre-control sha256, the same test exited 0 (502 ms) and the ctest family exited 0
(`test-3`, `test-4`, `test-5` logs).

**The test is deterministic: no wall-clock timing, sleeps or races (name the seam or
injected values it uses).** No test was added in this session. The cited pre-existing test
synchronises with `std::promise`/`std::shared_future`: the holder signals `holding` after
acquiring the sentinel and the summing thread signals `done` when `sumSidechainInputs`
returns. The 2 s `wait_for` is only an upper bound for "blocked", not a sleep or a race
window: green completes in 502 ms, and the negative control consistently reached the
bound. The `AutomatableModel` test seams also exist for this area
(`setValueBufferRampReadProbeForTest`, `setValueBufferCacheHitProbeForTest`,
`include/AutomatableModel.h:192-206`).

**The fix is at the cause and complete: every caller and sibling of what you changed was
checked and stays consistent.** No code was changed. The current design covers the
sidechain amount, mixer faders (`src/core/Mixer.cpp:707`), sends (`:889`), pan
(`:842`), surround (`src/core/MixerSurroundMix.cpp:78,339`), buses
(`src/core/AudioBusHandle.cpp:184-185`) and plug-in parameters: all read the same
prepared `valueBuffer()`. The only remaining `m_valueBufferMutex` acquisition is in tests.

**No collateral damage: old projects and saved data still load as before, and any limit or
check you added does not hit other callers that legitimately exceed it.** No product or
serialisation code changed; no limit or check was added. The old projects' render path is
the one already accepted with cluster 2647, and this session's full `PhaseDSidechainTest`
run is green.

**If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the intended
contract that shows it.** `src/core/AutomatableModel.cpp:804-824` is the lock-free
realtime branch (cache hit returns at `:824`; worker miss returns `nullptr` at `:813`),
and `src/core/AudioEngine.cpp:1065-1069` prepares every registered model before mixer jobs.
The fix commits are `c674877bc5` and `c28751cef2`, both ancestors of HEAD
(`git merge-base --is-ancestor` exited 0).

## Other findings

- `src/core/Mixer.cpp:652-653` still carries the comment from the first fix: "the
  sole-reader form takes no mutex. A linked amount still takes the linked model's mutex."
  The sole-reader API was removed by `c28751cef` and the current call is the ordinary
  prepared `valueBuffer()`; the comment is stale and could mislead a future reader. It is
  documentation only and was deliberately not changed here because this is an
  ALREADY-FIXED, report-only closure.
