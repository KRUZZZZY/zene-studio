FIXED

# cluster new-p15-64: Python discovery could block the constructing thread for minutes

new-p15-64: FIXED

## The defect (real)

On the base revision, the default constructor ran `m_python(... locatePython())`
(`git show 93ab9fb13f^:src/core/ExternalProcessStemSeparator.cpp`, line 143). That
probed up to four interpreters one after another. Each probe waited 5 s to start
and then 30 s for `import onnxruntime`. `stem.get_state` runs on the GUI thread
and reaches this code through `StemSurface::probe()` → `ExternalProcessStemSeparator().isAvailable()`
and through `fetchable()` → `locatePython()` in `src/core/ControlStemSupport.cpp`. A
hanging interpreter could therefore freeze the UI for minutes.

## Round 3: what the reviewer sent back, and how this round answers it

The reviewer found that round 2 split one 5 s deadline across the candidates
(`left / candidatesLeft`). A slow, failing `LMMS_STEM_PYTHON` interpreter used
up its share, and a working `/usr/bin/python3` that needs about 2 s to cold-import
onnxruntime got only 1.25 s. Discovery failed where the base succeeded.

This round replaces the sequential search with **parallel probes plus a cached
per-process discovery service**. The design is the one in the diagnostic
checkpoint, with one deliberate deviation, explained below.

## The fix (current lines)

* `src/core/StemPythonDiscovery.cpp` (new file; discovery moved here so that
  `ExternalProcessStemSeparator.cpp` stays under 500 lines):
  * `runParallelDiscovery` (`:187`) starts every candidate at the same time with
    `std::async`. Each probe gets the **full** 5 s deadline (`PythonDiscoveryBudgetMs`,
    `:54`), and its child process is killed at that deadline. The futures are then
    read in priority order, so the highest-priority success wins, and it is chosen
    only after every higher-priority probe has finished. With no time split, a
    slow candidate cannot starve a fallback.
  * The service is a leaked singleton guarded by a mutex and a condition variable.
    * `requestPythonDiscovery()` (`:325`) never blocks. It starts a detached
      background run unless one is already running, a result for the current
      `LMMS_STEM_PYTHON` value is cached, or a failure is younger than
      `PythonDiscoveryRetryDelayMs` (10 s, `:57`; checked by `shouldStartRun`, `:257`).
    * `pythonDiscoveryState(int waitMs = 0)` (`:345`) returns Pending, Found or Failed.
    * `waitForPythonDiscovery()` (`:386`) blocks, and is meant for worker threads and tests.
    * `discoveredPython()` (`:373`) waits for at most one bounded run.
    * A change of `LMMS_STEM_PYTHON` discards the cached result, and a generation
      counter drops the result of a stale run.
  * `locatePython()` (`:320`) is still the blocking, uncached wrapper over the same
    parallel run, for the CLI and tests.
* `src/core/ExternalProcessStemSeparator.cpp`:
  * The constructor stores the argument and does not probe (`:145`).
  * `isAvailable()` uses `m_python` when it was given, and otherwise the service (`:201`).
  * `separate()`, on the worker thread, calls `waitForPythonDiscovery()`.
* `src/core/ControlStemSupport.cpp`:
  * The `StemSurface` constructor starts discovery in the background (`:135`).
  * `fetchable()` (`:401`) and `probe()` (`:419`) use `discoveredPython()`. They no
    longer call `locatePython()` and never construct a probing backend.

**Deliberate deviation from the checkpoint: a bounded wait, not zero.** Step 3 of
the checkpoint asks `stem.get_state` to answer `available:false` immediately while
discovery is pending. The end-to-end harness, however, asserts
`state.get("available") is True` on the **first** `stem.get_state`
(`tests/stem_commands_lib.py:232`, run by `ControlStemCommands`). The GUI's
`StemSplitController::ensureBackendReady` (`src/gui/StemSplitController.cpp:184`)
also shows "Stem separation is not ready" after a single call. A zero wait would
turn both into a race. Instead, the control surface waits for at most
`pythonDiscoveryWaitBoundMs()`, which is the deadline plus the kill grace (7 s).
That wait happens only while a run is in flight and nothing is cached. The probes
themselves never run on the GUI thread.

The worst case for the GUI thread falls from four sequential 35 s probes (minutes)
to one bounded 7 s wait, once per discovery. If the run is still going after that,
the state reports "discovering a Python interpreter with onnxruntime; ask again in
a moment". I did not add the `python_discovery` reply field: it would change the
`stem.get_state` reply schema (`src/core/ControlCommandsStems.cpp:338`), and the
`error` reason already carries the pending state.

## Regression tests

The tests are deterministic. They use no sleeps and read no wall clock. Probes
come from the `LMMS_TESTING` `DiscoveryHooks` seam, and the clock is an injected
`std::atomic<qint64>`.

* `tests/src/core/StemPythonDiscoveryTest.cpp:295`
  `slowFailingCandidateDoesNotStarveAColdFallback` reproduces the reviewer's case.
  `LMMS_STEM_PYTHON` points to an interpreter that costs 6000 ms and fails;
  `/usr/bin/python3` costs 2000 ms and works. The test asserts that the fallback is
  returned and that every candidate was handed the full 5000 ms budget.
* `tests/src/core/StemPythonDiscoveryServiceTest.cpp` covers the service:
  * `:125` `stateIsPendingWhileAProbeRunsAndNeverBlocks`. The probe is held by a
    gate the test controls. The state reads Pending, constructing the backend
    probes nothing, and a 1 ms bounded wait returns Pending. The gate is closed, so
    the outcome is fixed and does not depend on timing. After the gate opens, the
    state reads Found.
  * `:181` `failureIsCachedThenRetriedAfterTheDelay`. A failure is not re-probed
    before 10 s of injected time have passed, and is re-probed after.
  * `:224` `successIsCachedPerLmmsStemPythonValue`. A success is cached, and
    changing `LMMS_STEM_PYTHON` starts a new discovery.
* The existing tests (constructor does not probe, a zero budget launches no
  process, `separate()` resolves on demand, the first importable candidate wins,
  reconfiguration is retried) are kept. `init()`/`cleanup()` now reset the service
  and the hooks between tests.

## Commands and EXIT codes (copied from the logs in /home/kruzzzzy/.config/kads/zene/state/runs/new-p15-64.r3/logs)

| step | command | EXIT | log |
|---|---|---|---|
| build | `cmake --build build -j4 --target zene StemPythonDiscoveryTest StemPythonDiscoveryServiceTest StemSplitPipelineTest StemSplitMenuTest StemJobManagerTest` | 0 | build-20.log |
| family | `cd build/tests && ctest -R Stem --output-on-failure` | 0 (`100% tests passed out of 17`; ControlStemCommands and StemQualityBench skipped with code 77) | test-21.log |
| save patch | `git diff -- include/StemSeparation/ExternalProcessStemSeparator.h src/core/ExternalProcessStemSeparator.cpp src/core/StemPythonDiscovery.cpp src/core/ControlStemSupport.cpp src/core/CMakeLists.txt > fix.patch` (5 files) | 0 | fix.patch |
| revert | `git apply -R fix.patch` | 0 | |
| red build | `cmake --build build -j4 --target zene StemPythonDiscoveryTest StemPythonDiscoveryServiceTest` | 0 | build-22-red.log |
| **red** | `cd build/tests && ctest -R StemPythonDiscovery --output-on-failure` | **8** (`0% tests passed, 2 tests failed out of 2`) | test-23-red.log |
| reapply | `git apply fix.patch` | 0 | |
| green build | same build as red | 0 | build-24-green.log |
| **green** | `cd build/tests && ctest -R StemPythonDiscovery --output-on-failure` | **0** (`100% tests passed out of 2`) | test-25-green.log |
| gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 0 (`RESULT: PASS`) | gates-26.log |

The red run failed with these lines (test-23-red.log). Each failure is a runtime
assertion; nothing failed to compile:

```
FAIL!  : StemPythonDiscoveryTest::slowFailingCandidateDoesNotStarveAColdFallback() Compared values are not the same
FAIL!  : StemPythonDiscoveryServiceTest::stateIsPendingWhileAProbeRunsAndNeverBlocks() no non-blocking discovery service
FAIL!  : StemPythonDiscoveryServiceTest::failureIsCachedThenRetriedAfterTheDelay() no non-blocking discovery service
FAIL!  : StemPythonDiscoveryServiceTest::successIsCachedPerLmmsStemPythonValue() no non-blocking discovery service
```

The red baseline is the round-2 code on this branch, the code the reviewer
rejected. With it, the fallback gets a 1250 ms share and fails.

Gate table (gates-26.log):

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

An earlier run of the gate (gates-12.log) failed `file-length` on
`src/core/ControlStemSupport.cpp (503)`. I shrank the change there by moving the
state check into `ExternalProcessStemSeparator::discoveredPython()`, and the gate
then passed.

## Concurrency (reasoned; local TSan does not run on this box)

* All service state is read and written under `Service::mutex`.
* The detached run thread only touches the leaked singleton, so it cannot reach a
  destroyed object at exit.
* Probes read the test hooks at the same time as each other. `setDiscoveryHooksForTest`
  first waits until no run thread is active (`activeThreads == 0`), so the hooks are
  never swapped mid-read.
* No audio-thread code is touched.

## Files changed

`include/StemSeparation/ExternalProcessStemSeparator.h`,
`src/core/ExternalProcessStemSeparator.cpp`, `src/core/StemPythonDiscovery.cpp`
(new), `src/core/ControlStemSupport.cpp`, `src/core/CMakeLists.txt`,
`tests/src/core/StemPythonDiscoveryTest.cpp`,
`tests/src/core/StemPythonDiscoveryServiceTest.cpp` (new), `tests/CMakeLists.txt`,
`tests/fork-sources.txt`, `tests/all-sources.txt`, `doc/STEM-SPLIT.md`, and this
report. All of these are fork files, so no `upstream-modifications.txt` entry is
needed.

Delegation:
* Sonnet: implemented the parallel run, the discovery service, the file split,
  the registrations, the ControlStemSupport changes and the tests, from my design.
* Haiku (×4): every build and ctest run, the two red/green rounds, and the gate
  runs.
* Me: the diagnosis, the design, review of the diff, the bounded-wait deviation
  and the `discoveredPython()` refactor, switching `separate()` to the cached
  service (Sonnet's change had left it on `locatePython()`), the test-fixture
  guards so the red run compiles, and this report.

## Other findings

* `ControlStemCommands` (the end-to-end harness that would exercise the
  first-call `available:true` contract) skips on this host because no Python here
  has onnxruntime (`tests/control-stem-commands.py:183-188`). The bounded wait was
  chosen to keep it green on hosts that do. That outcome was not verified here.
* With a cached failure, every `stem.get_state` made at least 10 s after the
  failure starts a new run and can wait up to 7 s for it. That is bounded, but a
  GUI that polls while no interpreter exists will see that wait about once every
  10 s.

## Self-check

1. **Red without the fix, green with it.** Red: `ctest -R StemPythonDiscovery`
   EXIT=8 (test-23-red.log), four runtime `FAIL!` lines quoted above. Green: EXIT=0
   (test-25-green.log). The two `git apply` commands both exited 0.
2. **Deterministic.** The tests contain no sleep, `qWait` or wall-clock read
   (`rg -n "sleep|qWait|steady_clock"` on both test files finds only the comments
   saying so). The pending state is held by a gate the test controls. The 1 ms
   bounded wait runs with that gate closed, so its result is fixed.
3. **Cause and siblings.** Every path into discovery was changed:
   * the constructor (`:145`);
   * `isAvailable` (`:201`);
   * `separate` (worker thread, `waitForPythonDiscovery`);
   * `ControlStemSupport` `fetchable` (`:401`) and `probe` (`:419`).

   `locatePython()` is left only for the CLI and test callers
   (`tests/src/core/StemSplitPipelineTest.cpp:149,304`).
4. **No collateral damage.**
   * No project, save, undo or command-schema code changed.
   * The `stem.get_state` reply fields are unchanged.
   * The stem family passes: 17/17 (test-21.log).
   * Discovery order and the first-importable-wins rule are unchanged
     (`discoveryFindsAnInterpreterThatImportsOnnxRuntime`).
5. **ALREADY-FIXED/FALSE-POSITIVE reference.** Not applicable (FIXED).
6. **Sibling commands in changed command files.** No `ControlCommands*`,
   `ControlRegistry*` or reversibility table is changed. No command is added,
   renamed or removed. `ControlStemSupport.cpp` changes only the availability
   helpers.
7. **Realtime.** Nothing here runs on the audio thread.
