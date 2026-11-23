ALREADY-FIXED

# Cluster 5468 — Legacy macOS worker wake-up locks and allocates on the render path

The finding is the same defect previously fixed on this train as Q050-167 (commit
`de249b1ecb`, "keep the audio worker wake off the render thread's mutex"), and that fix is
present in this branch. The historical SKIPPED-SCOPE note on this cluster (that the
`LMMS_WORKER_WAKE_CONDVAR` block only exists on Apple below macOS 11) no longer describes
the tree: the conditional mutex/condition-variable block is gone, and both Apple and Linux
compile the same wait-on-address protocol.

## Code evidence

- `src/core/AudioEngineWorkerThread.cpp:248-252` — `notifyWorkers()`, the function the
  finding names, is now a pure post:
  `workerwake::notify(s_wakeGeneration);`. No mutex, no `new`, no wait. It is called every
  period from `wakeWorkers()` (`:259-263`), which `startAndWaitForJobs()` calls at `:229`.
- `src/core/AudioWorkerWake.cpp:84-105` — `wake()` posts with `__ulock_wake` on Apple
  (`:94`) or `FUTEX_WAKE_PRIVATE` on Linux (`:97`); neither blocks nor allocates.
- `src/core/AudioWorkerWake.cpp:69-73` — `static_assert`s require the wake word to be a
  lock-free 32-bit atomic (a lock-based atomic would be the mutex the finding describes).
- `src/core/AudioWorkerWake.cpp:109-112` — an Apple build that did not select the
  wait-on-address primitive fails to compile, so `std::atomic::wait` (unavailable below
  macOS 11) cannot silently return to the Apple path.
- `include/AudioWorkerWake.h:56-90` — the selection is constexpr: Apple and Linux select
  `Primitive::WaitOnAddress`; only Windows/POSIX without futex uses `std::atomic::wait`.
- `src/core/AudioEngineWorkerThread.cpp:236-246` — the comment records the removal of the
  old lazy-`new` mutex+condition-variable path.
- `CMakeLists.txt:267-277` and `README.md:29-34` — the supported macOS floor is 10.13;
  `__ulock` is available since 10.12, below that floor, so the Apple path needs no
  availability guard.

## Regression test in the tree

`tests/src/core/AudioWorkerWakeTest.cpp` (registered in `tests/CMakeLists.txt:850`) holds
the shipping `AudioEngineWorkerThread::wakeWorkers()` to the promise:

- `theRenderSideWakeAllocatesNothingAndTakesNoMutex` (`:362`) drives 256 real wake periods
  through an allocation probe and a `pthread_mutex_lock` interposer, first call included,
  and cross-checks the test-only notify counter so it cannot pass against a copy.
- `theProbesCatchTheOldCondvarWake` (`:397`) runs the old lazy-`new` mutex+condvar shape
  through the same probes and requires them to see it.
- `theWakePathNamesNoLockOrAllocationInItsSource` scans the comment-stripped
  `notifyWorkers()`/`notify()` bodies for lock/allocation tokens on every platform.
- `repeatedWakeupsDeliverEveryPeriodWithoutDeadlock` (`:526`) drives 240 periods × 8 jobs
  on real pool workers and requires every job once and exactly 240 posts.

## Red/green evidence from this session

No product code was changed in this session; the fix was already committed. To prove the
existing test catches this defect, I temporarily restored the old wake shape inside
`notifyWorkers()` (mutex + lazily allocated condition variable) in the product file only,
built and ran the test, then reverted that temporary change with `git apply -R`.

| Command | Exit |
|---|---|
| `cmake --build build --target AudioWorkerWakeTest -j4` (green tree) | 0 |
| `cd build/tests && ctest -R '^AudioWorkerWakeTest$' --output-on-failure` | 0 |
| `git diff -- src/core/AudioEngineWorkerThread.cpp > logs/red-revert.patch` | 0 |
| `cmake --build build --target AudioWorkerWakeTest -j4` (temporary old shape) | 0 |
| `cd build/tests && ./AudioWorkerWakeTest theRenderSideWakeAllocatesNothingAndTakesNoMutex` | **1** |
| `git apply -R logs/red-revert.patch` (restore product code) | 0 |
| `cmake --build build --target AudioWorkerWakeTest -j4` (restored) | 0 |
| `cd build/tests && ctest -R '^AudioWorkerWakeTest$' --output-on-failure` (restored) | 0 |
| `python3 .../feature_check.py <worktree>` | 0 |

The red run's own output:

```
FAIL!  : AudioWorkerWakeTest::theRenderSideWakeAllocatesNothingAndTakesNoMutex()
         'allocations == 0' returned FALSE. (the render-side wake allocated 2 time(s);
         the first call of the old path allocated its mutex and condition variable)
Totals: 2 passed, 1 failed, 0 skipped, 0 blacklisted, 0ms
```

Logs in `/home/kruzzzzy/.config/kads/zene/state/runs/5468.r1/logs/`:
`build-1-green.log`, `test-1-green.log`, `build-2-red.log`, `test-2-red.log`,
`build-3-restored.log`, `test-3-restored-green.log`, `feature-check-1.log`, and the
temporary change in `red-revert.patch`.

The product fix commit is in this branch's history (`git branch --contains de249b1ecb`
lists `050/m0-train-13` and later trains); the worktree was clean after the restore
(`git status --short` printed nothing).

## Self-check

- **Regression test fails with the product change reverted and passes with it (exit
  codes).** Proven above: the temporary restoration of the old wake shape makes
  `theRenderSideWakeAllocatesNothingAndTakesNoMutex` exit **1** ("allocated 2 time(s)"),
  and after `git apply -R` the full `AudioWorkerWakeTest` exits **0**. No product change was
  left in this session's diff; the fix is the existing commit `de249b1ecb`.
- **The test is deterministic: no wall-clock timing, sleeps or races.** The red/green case
  counts allocations and mutex calls around 256 real `wakeWorkers()` posts and checks the
  notify counter; it uses no clock and no sleep. (`quitRacingAWakeupAlwaysJoinsTheWorker`
  in the same file uses a 1 ms sleep to let a worker park; it is pre-existing and is not
  part of this cluster's proof.)
- **The fix is at the cause and complete: every caller and sibling checked.**
  `notifyWorkers()`'s only callers are `wakeWorkers()` (`src/core/AudioEngineWorkerThread.cpp:259-263`),
  called from `startAndWaitForJobs()` (`:291`) and `quit()` (`:229`); both now use the same
  generation-value protocol, and `waitForWake()` (`:254-257`) waits on the same word.
  The sibling `src/wasm/WasmWorkerPool.cpp:91,163,246` uses `workerwake::notify` /
  `notifyOne` / `wait`; its one-lane-per-block wake shape is asserted by
  `theWasmPoolWakesOneLaneForWorkAndAllOnShutdown`. `LMMS_WORKER_WAKE_CONDVAR` and the
  old `std::condition_variable` wake no longer exist in the tree (only the test's
  deliberate old-shape control names one).
- **No collateral damage.** No project or save data is touched: this is a thread wake
  primitive, and the generation value remains the signal, so the no-lost-wake guarantee
  is unchanged. No limit or check was added. `repeatedWakeupsDeliverEveryPeriodWithoutDeadlock`
  and `quitRacingAWakeupAlwaysJoinsTheWorker` still pass (exit 0).
- **Sibling commands in changed command files.** This session changed no command,
  reversibility or control-registry file: the only file committed for cluster 5468 is this
  report. The `app.*` and `arrangement.*` ids the check lists come from train history
  between `FIX-BASE` and this branch, not from this unit. The wake is reached by the
  existing Play/`transport.play` render path, and no command id, schema, undo step or
  reversibility class was added or changed, so none of those commands is affected.
- **ALREADY-FIXED cites code (file:line).** `src/core/AudioEngineWorkerThread.cpp:248-252`
  (`notifyWorkers()` is `workerwake::notify`), `src/core/AudioWorkerWake.cpp:84-105`
  (futex/ulock post, no lock/allocation), `src/core/AudioWorkerWake.cpp:69-73`
  (lock-free 32-bit wake word asserted), `src/core/AudioWorkerWake.cpp:109-112`
  (Apple must select the wait-on-address path).

## Other findings

- `tests/src/core/AudioWorkerWakeTest.cpp:quitRacingAWakeupAlwaysJoinsTheWorker` uses
  `std::this_thread::sleep_for(1 ms)` to let the worker park before the quit/wake race.
  Not introduced here and not part of this cluster's red/green proof; noted only.
