FIXED

# cluster new-p15-219 — destroying a ThreadPool from one of its workers

Round 2. Round 1 (commits `d90bb0c4ec`, `4ee99b9b7d`) removed the self-join crash but the
review sent it back: its destructor still joined sibling workers, and a sibling task that
waits for the releasing task's future deadlocks there. That deadlock, plus the queued-work
outcome, is fixed in this round.

## Verdict and cause

REAL. `ThreadPool::~ThreadPool` must not join the thread it runs on, and must not wait for
any sibling while it runs inside one of the pool's tasks.

- `src/core/ThreadPool.cpp:71-74` (original) — the destructor joined every worker.
  `join()` on the calling thread throws `std::system_error`, which escapes the implicit
  `noexcept` destructor and calls `std::terminate`; round 1 reproduced that with a child
  that died by signal.
- Round 1 kept a `thread_local` marker and detached only the calling worker, but still
  joined the siblings. A sibling task that waits for the releasing task's future can only
  finish after the releasing task returns, and the releasing task cannot return until the
  destructor does: the destructor joined a sibling that waited on it. Reproduced this
  round: the two-worker child of the new test hung (`timeout 5 ...` exited 124,
  `probe-2-workers-round1.log`), matching the reviewer's probe.
- A one-worker round-1 pool also stopped its loop after the destroying task and abandoned
  work already queued, so the queued task's future became a broken promise (`timeout 5`
  on the one-worker child also exited 124, `probe-1-worker-round1.log`).

## Fix

- `include/ThreadPool.h:118-124` — the queue, condition variable, mutex and stop flag live
  in a private `State`.
- `include/ThreadPool.h:138-142` — `State` is held by `std::shared_ptr`; workers receive
  their own copy, so the state outlives a `ThreadPool` destroyed from one of its tasks.
  `run()` is static and takes that state, so no worker touches the `ThreadPool` object
  after a worker-initiated destruction. The existing test's `m_done` observation stays
  valid as a reference into the state (`m_done`), and copying/moving are deleted so the
  reference can never bind a moved-from pool's state.
- `src/core/ThreadPool.cpp:65-104` — the destructor sets the stop flag and notifies once.
  From an external thread it then joins every worker (unchanged behaviour,
  `src/core/ThreadPool.cpp:85-92`). When it runs on one of the pool's own workers it
  detaches every worker instead of joining any of them
  (`src/core/ThreadPool.cpp:96-104`): nothing it could join is allowed to be waited on
  from here. Each detached worker keeps the shared state alive, runs the tasks already
  queued (the `run()` loop still drains until the queue is empty,
  `src/core/ThreadPool.cpp:111-126`), and exits, releasing the state with it.
- `include/ThreadPool.h:47-54` — the destructor contract now states the worker-initiated
  outcome, including that queued tasks still run (their futures are not broken).

Constructor startup-failure joining is unchanged: it runs on the constructing thread, never
on a worker, and still stops and joins the workers that started
(`ThreadPoolTest::partialStartupFailureStopsStartedWorkers` passes).

## Regression tests

- `tests/src/core/ThreadPoolTest.cpp:372`
  `ThreadPoolTest::destructionFromWorkerDoesNotWaitForSibling` runs the child with two
  workers. `runWorkerDestructionChild` (`tests/src/core/ThreadPoolTest.cpp:153`) creates a
  pool owned by a `shared_ptr`, warms both workers up with gate-blocked tasks so each has
  provably entered `run()`, then enqueues the releasing task (which destroys the pool) and
  a sibling that waits on the releasing task's future. Both tasks must complete and both
  workers must leave `run()`.
- `tests/src/core/ThreadPoolTest.cpp:381`
  `ThreadPoolTest::destructionFromWorkerDrainsQueuedWork` runs the same child with one
  worker, so the sibling sits in the queue behind the releasing task: it must still run and
  its future must not be a broken promise, then the worker must exit.
- Worker exit is observed from the threads themselves: a `thread_local` object with a
  non-trivial constructor and destructor (`tests/src/core/ThreadPoolTest.cpp:70-82`) is
  armed inside each warm-up task and destroyed when that thread exits; the child waits for
  the count to reach the worker count. So a passing child has proven both tasks ran and
  every detached worker left `run()`, not merely that a task future became ready.
- No sleeps: ordering is promise/future gated. Worker startup is a gate all workers must
  pass, not a timing assumption. A missing completion hangs the child, and the parent
  test's `QProcess` timeout turns that into a deterministic failure
  (`runWorkerDestructionChildProcess`, `tests/src/core/ThreadPoolTest.cpp:203`).

## Commands and EXIT codes (logs under .../new-p15-219.r2/logs)

RED against the round-1 product code (the code the reviewer rejected), with the new test:

- `git diff -- src/core/ThreadPool.cpp include/ThreadPool.h > .../logs/fix.patch` → EXIT=0
- `git apply -R .../logs/fix.patch` → EXIT=0
- `cmake --build build --target ThreadPoolTest -j4` → build-1-red-round1.log, EXIT=0
- `ctest -R ThreadPoolTest --output-on-failure` (from `build/tests`) →
  test-1-red-round1.log, EXIT=8, `Totals: 12 passed, 2 failed`, including
  `FAIL! : ThreadPoolTest::destructionFromWorkerDoesNotWaitForSibling() ... The child hung or crashed while a sibling waited for the releasing task's future.`
  and `FAIL! : ThreadPoolTest::destructionFromWorkerDrainsQueuedWork() ...`
- `timeout 5 ./ThreadPoolTest --thread-pool-worker-destruction-child 2` →
  probe-2-workers-round1.log, EXIT=124 (the deadlock)
- `timeout 5 ./ThreadPoolTest --thread-pool-worker-destruction-child 1` →
  probe-1-worker-round1.log, EXIT=124

GREEN with this round's fix:

- `git apply .../logs/fix.patch` → EXIT=0
- `cmake --build build --target ThreadPoolTest -j4` → build-2-green.log, EXIT=0
- `ctest -R ThreadPoolTest --output-on-failure` → test-2-green.log, EXIT=0
- `./ThreadPoolTest destructionFromWorkerDoesNotWaitForSibling destructionFromWorkerDrainsQueuedWork`
  → test-2b-green-slots.log, EXIT=0 (`Totals: 4 passed, 0 failed`)
- `cmake --build build --target ThreadPoolTest -j4` after adding the deleted copy/move
  → build-5-green-deletes.log, EXIT=0; `ctest -R ThreadPoolTest` → test-5-green-deletes.log, EXIT=0

Final proof that the test catches the remaining bug (step 4b, final patch `fix-final.patch`):

- `git diff -- src/core/ThreadPool.cpp include/ThreadPool.h > .../logs/fix-final.patch` → EXIT=0
- `git apply -R .../logs/fix-final.patch` → EXIT=0
- `cmake --build build --target ThreadPoolTest -j4` → build-6-final-reverted.log, EXIT=0
- `ctest -R ThreadPoolTest --output-on-failure` → test-6-final-reverted-red.log, EXIT=8
  (two `FAIL!` lines)
- `git apply .../logs/fix-final.patch` → EXIT=0
- `cmake --build build --target ThreadPoolTest -j4` → build-7-final-reapplied.log, EXIT=0
- `ctest -R ThreadPoolTest --output-on-failure` → test-7-final-reapplied-green.log, EXIT=0

Header change rebuilt the app target too:

- `cmake --build build --target zene -j4` → build-8-zene.log, EXIT=0

Registration: `tests/upstream-modifications.txt` has this round's entries for
`src/core/ThreadPool.cpp` and `include/ThreadPool.h`. The test file is a fork source
(`tests/fork-sources.txt`). The file ends with a newline.

## Gates

- `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` →
  EXIT=0 (`RESULT: PASS`, all 8 rows PASS), saved as feature-check-1.log.

## Test matrix

- Normal path: outside-thread shutdown drains the queue and joins all workers —
  `destructionDrainsQueuedTasks` passes (it also does not hang: it reads `m_done`, which is
  still a member reference).
- Reviewer's failure path: sibling waits on the releasing task's future — red on round-1
  code, green now.
- Exhaustion/reconfiguration: zero-worker, zero-hardware-concurrency and partial
  worker-startup failure paths pass unchanged.
- Queued-work outcome: the one-worker child proves queued work still runs after a
  worker-initiated destruction.
- Audio thread: no process/render code touched; the destructor is not on the audio thread
  and adds no allocation to any per-period path.
- Platforms: `detach()`, `shared_ptr` and `thread::id` are standard C++20. The test uses
  `QProcess` like its siblings; the glibc interposer tests still skip/run as before.

## Self-check

- **Regression test fails reverted, passes fixed (exit codes).** Reverted final product
  patch: test-6-final-reverted-red.log EXIT=8 with two `FAIL!` lines. Reapplied:
  test-7-final-reapplied-green.log EXIT=0. Also RED against the round-1 product code the
  reviewer rejected: test-1-red-round1.log EXIT=8; GREEN: test-2-green.log EXIT=0,
  test-2b-green-slots.log EXIT=0, test-5-green-deletes.log EXIT=0.
- **Deterministic; no wall-clock timing, sleeps or races.** Promise/future gates order
  pool ownership release, the sibling's wait and the worker exits; every worker must pass
  a blocking warm-up task before the scenario, so the `thread_local` exit witness counts
  real workers. The only clock is the parent's `QProcess` timeout, which converts a hang
  into a failure; it is not used to order assertions.
- **Fix at the cause and complete.** The cause was the destructor waiting for workers
  (`src/core/ThreadPool.cpp:65-104`). Worker-visible state now lives in the shared `State`
  (`include/ThreadPool.h:118-124`), workers only use it (`src/core/ThreadPool.cpp:111-126`),
  and a worker-initiated destructor detaches instead of waiting
  (`src/core/ThreadPool.cpp:96-104`). External destruction still drains and joins
  (`src/core/ThreadPool.cpp:85-92`). Constructor failure join is unaffected (external
  thread). `#include "ThreadPool.h"` appears in `src/core/ThreadPool.cpp`,
  `src/core/FileSearchJob.cpp`, `src/gui/FileBrowser.cpp` and the test; `zene` was rebuilt
  (build-8-zene.log, EXIT=0).
- **No collateral damage.** No project/save format is touched; no caller limit added.
  Copying/moving of `ThreadPool` was already impossible (an atomic member); it is now
  explicitly deleted, same contract. `ThreadPool::instance()` is a static object destroyed
  on the main thread, so its path is unchanged.
- **ALREADY-FIXED/FALSE-POSITIVE:** not applicable, verdict FIXED.
- **Sibling commands:** this change touches no command file, no control command and no
  `ControlCommandsSnapshot`; `app.*` and `arrangement.*` are untouched.

## Other findings

- The finding is latent for the product's only use, `ThreadPool::instance()` (a static
  object destroyed on the main thread); no product caller heap-allocates a pool. The class
  is public API and constructible from tests, and any owner released inside a task hits
  both failure modes above.
- A handler that calls `enqueue()` after the pool was destroyed from a worker is still a
  use-after-free by the caller; the destructor contract does not and cannot allow that.
