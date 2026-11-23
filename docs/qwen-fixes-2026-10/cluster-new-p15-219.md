FIXED

# cluster new-p15-219 — destroying a ThreadPool from one of its workers terminated the process

## Verdict and cause

REAL. Before the fix, `ThreadPool::~ThreadPool` joined every element of `m_workers`:

- `src/core/ThreadPool.cpp:71-74` (pre-fix) — `for (auto& worker : m_workers) { if (worker.joinable()) { worker.join(); } }`

A task can hold the last `shared_ptr` to its own pool; when that task releases the
owner, the destructor runs on the pool's own worker. `std::thread::join()` on the
calling thread throws `std::system_error` (`resource deadlock avoided`), and because a
destructor is implicitly `noexcept`, the escaping exception calls `std::terminate`.
The new child-process regression reproduced exactly that: the child died by signal
instead of returning.

## Fix

- `src/core/ThreadPool.cpp:34` — a `thread_local` marker identifies the worker whose own
  pool is being destroyed on it.
- `src/core/ThreadPool.cpp:76-97` — the destructor still stops and joins every other
  worker; when the join candidate is the calling thread it sets the marker and
  `worker.detach()`es it (line 88-89) instead of joining it. A detached `std::thread`'s
  destructor is safe, and the worker thread is dedicated to this pool.
- `src/core/ThreadPool.cpp:106` — the `run()` loop checks the marker before touching any
  member, so the detached worker returns from `run()` after the task that destroyed the
  pool finishes and never reads `m_queue`/`m_runMutex`/`m_runCond` again.
- `include/ThreadPool.h:46-50` — the destructor's contract documents the worker case.

The ordinary path is unchanged: an outside thread still sets `m_done`, notifies, drains
queued tasks and joins every worker (`ThreadPoolTest::destructionDrainsQueuedTasks`
still passes). The constructor's startup-failure join path never runs on a worker, so it
is unchanged. No other file includes `ThreadPool.h` except `src/core/ThreadPool.cpp`,
`src/core/FileSearchJob.cpp`, `src/gui/FileBrowser.cpp` and the test; `zene` was rebuilt
after the header change.

## Regression test

`tests/src/core/ThreadPoolTest.cpp:312` `ThreadPoolTest::destructionFromWorkerDoesNotTerminate`.
It starts the test binary with `--thread-pool-worker-destruction-child`
(`tests/src/core/ThreadPoolTest.cpp:366`). The child (`runWorkerDestructionChild`,
`tests/src/core/ThreadPoolTest.cpp:136`) builds a two-worker pool owned by a
`shared_ptr`, enqueues a task that holds a copy, releases the test thread's copy, then
lets the task release the last copy — a promise/future gate, no sleeps or timeouts, so the
destructor provably runs on the worker. The parent asserts a normal child exit with code
0; a crash is a failure. The child then `task.get()`s the future, which completes only
after the destructor returned.

## Commands and EXIT codes (logs under .../new-p15-219.r1/logs)

Baseline, before any edit:

- `cmake --build build --target ThreadPoolTest -j4` → build-0-baseline.log, EXIT=0
- `ctest -R ThreadPoolTest --output-on-failure` (from build/tests) → test-0-baseline.log, EXIT=0

RED, test added, no product change:

- `cmake --build build --target ThreadPoolTest -j4` → build-1-red.log, EXIT=0
- `ctest -R ThreadPoolTest --output-on-failure` → test-1-red.log, EXIT=8
  - `FAIL! : ThreadPoolTest::destructionFromWorkerDoesNotTerminate() 'child.exitStatus() == QProcess::NormalExit' returned FALSE. (The child crashed instead of shutting down a pool destroyed by its own worker.)`

GREEN, fix applied:

- `cmake --build build --target ThreadPoolTest -j4` → build-2-green.log, EXIT=0
- `ctest -R ThreadPoolTest --output-on-failure` → test-2-green.log, EXIT=0
- `./ThreadPoolTest destructionFromWorkerDoesNotTerminate` → test-2b-green-slot.log, EXIT=0
  (`Totals: 3 passed, 0 failed`)

Proof the test catches the bug (step 4b):

- `git diff -- src/core/ThreadPool.cpp include/ThreadPool.h > .../logs/fix.patch` → EXIT=0
- `git apply -R .../logs/fix.patch` → EXIT=0
- `cmake --build build --target ThreadPoolTest -j4` → build-3-reverted.log, EXIT=0
- `ctest -R ThreadPoolTest --output-on-failure` → test-3-reverted-red.log, EXIT=8
  (same `FAIL!`, child crashed)
- `git apply .../logs/fix.patch` → EXIT=0
- `cmake --build build --target ThreadPoolTest -j4` → build-4-reapplied.log, EXIT=0
- `ctest -R ThreadPoolTest --output-on-failure` → test-4-reapplied-green.log, EXIT=0

Header change rebuilt the app target too:

- `cmake --build build --target zene -j4` → build-5-zene.log, EXIT=0

Registration: `tests/upstream-modifications.txt` gained one entry each for
`src/core/ThreadPool.cpp` and `include/ThreadPool.h` in this commit. The test file is
already a fork source (`tests/fork-sources.txt`).

## Gates

- `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` → EXIT=0
  (`RESULT: PASS`; all 8 rows PASS, complexity and file-length report "no new item in
  your files"), saved as feature-check-1.log.

## Test matrix

- Normal path: outside-thread shutdown drains the queue and joins all workers —
  `destructionDrainsQueuedTasks` passed before and after.
- Failure path under test: destruction from a worker — new slot, red before the fix,
  green after.
- Exhaustion/reconfiguration: zero-worker and zero-hardware-concurrency paths untouched
  and still pass; partial worker-startup failure still passes.
- Audio thread: no change touches process/render code; the destructor never runs on the
  audio thread and the fix adds no allocation or lock to any per-period path beyond the
  existing `unique_lock` in the destructor.
- Platforms: `detach()` and `thread::id` comparison are standard C++20. The new test is
  platform-neutral (no glibc interposition; it uses `QProcess`, like its siblings).

## Self-check

- Every verdict here is backed by a log listed above.
- The test fails without the product change (`test-3-reverted-red.log`, EXIT=8; also
  `test-1-red.log`, EXIT=8) and passes with it (`test-2-green.log` EXIT=0,
  `test-2b-green-slot.log` EXIT=0, `test-4-reapplied-green.log` EXIT=0).
- No test, gate or ratchet was weakened; no gate file was touched.
- `git status` is clean after the commit; only the four files above are changed.

## Other findings

- The finding is latent for the product's only use, `ThreadPool::instance()` (a static
  object destroyed on the main thread); no current caller heap-allocates a pool. The
  class is public API and constructible from tests, so the crash was reachable as soon
  as any owner released it inside a task, which is what the test exercises.
