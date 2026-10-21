FIXED

# Cluster new-p10-414-port-port

Port of the accepted `bugs/qwen-new-p10-414-port` fix (`394daa2f4` + `00b8d7149`), re-applied by
hand against this line — no merge, no cherry-pick, no rebase. The accepted branch was cut from an
older line, where `tests/src/core/ThreadPoolTest.cpp` held only `destructionDrainsQueuedTasks`; on
this line that file also carries the five `enqueues*` slots, and those slots call
`ThreadPool::instance()`. That is why the accepted regression was order-dependent: it forced the
cached worker count to zero and then asserted on the *global* singleton, which the earlier slots
have already built with the real hardware concurrency. The regression here does not depend on the
slot order at all (see "Determinism" below).

## Finding and evidence

`std::thread::hardware_concurrency()` is permitted to return zero. Before this change:

- `include/ThreadPool.h:116` — `inline static size_t s_numWorkers = std::thread::hardware_concurrency();`
  cached the platform value verbatim, so it could be zero.
- `src/core/ThreadPool.cpp:100` — `ThreadPool::instance()` passed that value straight into the
  constructor.
- `src/core/ThreadPool.cpp:32` — the constructor only guarded it with `assert(numWorkers > 0)`,
  which compiles out in release builds (`CMAKE_BUILD_TYPE=RelWithDebInfo`, `-DNDEBUG`): the pool was
  built with **no workers at all**, so every enqueued task stayed in the queue and its future never
  became ready.

The finding's "where" pointer (`src/core/ThreadPool.cpp:29-37, 77-80`) is the same code on this
line; the constructor is at `src/core/ThreadPool.cpp:30-60` and `numWorkers()` at
`src/core/ThreadPool.cpp:77-80`.

### The fix

- `include/ThreadPool.h:109-114` — a new private helper `workerCount(size_t)` returns at least one:
  `hardwareConcurrency == 0 ? size_t{1} : hardwareConcurrency`.
- `include/ThreadPool.h:122` — the cached count is now `workerCount(std::thread::hardware_concurrency())`,
  so the global pool is built from a usable count.
- `src/core/ThreadPool.cpp:32` — the constructor clamps the requested count with the same helper
  before any worker is started, so **every** path that builds a pool (the global one and any direct
  construction) owns at least one worker.

The count therefore flows `hardware_concurrency() -> workerCount() -> s_numWorkers -> instance() ->
constructor -> workerCount() -> m_workers`, and `numWorkers()` reports at least one everywhere.

### Callers and siblings checked

- `src/gui/FileSearchJob.cpp:57` — the only product user of `ThreadPool::instance()`
  (`m_task = ThreadPool::instance().enqueue(std::move(fn));`). Unchanged behaviour: it enqueues one
  task and waits on its future; with the fix that future now becomes ready on a platform that
  reports zero cores instead of hanging forever.
- `src/wasm/WasmWorkerPool.cpp:40-45` — the only other reader of `std::thread::hardware_concurrency()`
  in the tree. It already clamps: `cores > 1 ? cores - 1 : 1` yields one lane when `cores == 0`, so
  it was never affected and is deliberately **not** touched.
- The private constructor is the only place workers are started (`src/core/ThreadPool.cpp:35-41`),
  so clamping there covers every construction path, including the one the test drives directly.

### Regression tests

Both new slots live in the already registered `tests/src/core/ThreadPoolTest.cpp`
(registered in `tests/CMakeLists.txt:663` and `tests/fork-sources.txt:2476`; no new test file, so
no `tests/CMakeLists.txt` change was needed).

- `tests/src/core/ThreadPoolTest.cpp:245-256` — `zeroWorkerRequestStillRunsTasks()`: asks for a pool
  with **zero** workers, asserts `numWorkers() == 1`, enqueues a task and blocks on `future.get()`.
- `tests/src/core/ThreadPoolTest.cpp:258-273` — `zeroHardwareConcurrencyStillRunsTasks()`: runs
  `--thread-pool-zero-concurrency-child` (`tests/src/core/ThreadPoolTest.cpp:120-131`,
  dispatched at `:287-290`) in a child process. The child sets the cached count to zero, builds the
  global pool through `ThreadPool::instance()` and runs one task. It returns 2 if the pool has no
  worker (so the child can never hang) and 0 only if the task ran.

## Command evidence

All logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-414-port-port.r1/logs`.
The pre-configured build tree was used; no configure command was run.

| Stage | Exact command | Exit | Result |
|---|---|---:|---|
| Baseline build | `cmake --build build --target ThreadPoolTest VcaStripViewTest -j 8 > .../logs/build-baseline.log 2>&1` | 0 | Both targets built. |
| Baseline ctest | `cd build/tests && ctest -R '^(ThreadPoolTest|VcaStripViewTest|VcaStripLifetimeTest)$' --timeout 90 --output-on-failure > .../logs/test-baseline2.log 2>&1` | 0 | 3/3 passed before any change (the two tests the accepted fix failed on the train are green here). |
| Save product patch | `git diff -- include/ThreadPool.h src/core/ThreadPool.cpp > .../logs/fix.patch` | 0 | Patch holds the two product files only. |
| Reverse product patch | `git apply -R .../logs/fix.patch` | 0 | Fix removed, test file untouched. |
| RED build | `cmake --build build --target ThreadPoolTest -j 8 > .../logs/build-red.log 2>&1` | 0 | Rebuilt against the unfixed product code. |
| **RED ctest** | `cd build/tests && timeout 300 ctest -R '^ThreadPoolTest$' --output-on-failure > .../logs/test-red.log 2>&1` | **8** | `FAIL!  : ThreadPoolTest::zeroWorkerRequestStillRunsTasks() Actual (pool->numWorkers()): 0, Expected (std::size_t{1}): 1` and `FAIL!  : ThreadPoolTest::zeroHardwareConcurrencyStillRunsTasks() Actual (child.exitCode()): 2, Expected (0): 0` — 9 passed, 2 failed. |
| Restore product patch | `git apply .../logs/fix.patch` | 0 | Fix restored. |
| GREEN build | `cmake --build build --target ThreadPoolTest -j 8 > .../logs/build-green.log 2>&1` | 0 | Rebuilt with the fix. |
| **GREEN ctest** | `cd build/tests && timeout 300 ctest -R '^ThreadPoolTest$' --output-on-failure > .../logs/test-green.log 2>&1` | **0** | 1/1 affected-family CTest target passed. |
| GREEN slot detail | `cd build/tests && timeout 120 ./ThreadPoolTest > .../logs/test-green-verbose.log 2>&1` | 0 | 11 passed / 0 failed (2 new slots + 7 pre-existing slots + init/cleanup). |
| Build `zene` | `cmake --build build --target zene -j 8 > .../logs/build-zene.log 2>&1` | 0 | The header changed, so `zene` and every other includer was rebuilt. |
| Affected family | `cd build/tests && timeout 400 ctest -R '^(ThreadPoolTest|FileSearchJobTest|VcaStripViewTest|VcaStripLifetimeTest)$' --timeout 120 --output-on-failure > .../logs/test-family.log 2>&1` | 0 | 4/4 passed — `ThreadPoolTest`, `FileSearchJobTest` (the global pool's only product user), `VcaStripViewTest` and `VcaStripLifetimeTest`. |
| Stability | 10 consecutive `cd build/tests && timeout 60 ./ThreadPoolTest` runs | 0 | 10/10 runs, 11 passed / 0 failed each time. |
| Gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > .../logs/gate-feature.log 2>&1` | 0 | `RESULT: PASS` — fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length all PASS. |
| Complexity ratchet | `bash tests/complexity-gate.sh --check > .../logs/gate-complexity.log 2>&1` | 1 | Pre-existing baseline entries only (see below). |
| File-length ratchet | `bash tests/file-length-gate.sh --check > .../logs/gate-filelength.log 2>&1` | 1 | Pre-existing baseline entries only (see below). |

### Gate status (honest note)

The two standalone ratchets exit 1, and they exit 1 on the untouched base as well: the 6 over-target
functions listed in `gate-complexity.log` (in `tests/src/gui/XyPadPairTest.cpp`,
`src/core/DawProjectReadTracks.cpp`, `src/core/ControlCommandsMixerTools.cpp`,
`src/core/ControlCommandsMixerStrip.cpp`, `src/core/ProjectRevisions.cpp`,
`src/gui/ControlCommandsShortcuts.cpp`) and the 15 over-500-line files in `gate-filelength.log` are
newest-line drift I did not introduce and did not touch. **No** regression entry names
`include/ThreadPool.h`, `src/core/ThreadPool.cpp` or `tests/src/core/ThreadPoolTest.cpp`; the new
`workerCount` helper has CCN 2 and no file grew. No gate limit or baseline was re-anchored.

## Files committed

- `include/ThreadPool.h`
- `src/core/ThreadPool.cpp`
- `tests/src/core/ThreadPoolTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-414-port-port.md`

## Self-check

**The regression test FAILS with the product change reverted and PASSES with it (exit codes).**
Reverted (product patch reverse-applied, test file untouched): `ctest -R '^ThreadPoolTest$'` exit
**8** — `zeroWorkerRequestStillRunsTasks` saw `numWorkers() == 0` instead of 1, and
`zeroHardwareConcurrencyStillRunsTasks` saw the child's exit code 2 (no worker) instead of 0
(`logs/test-red.log`). Restored: the same command exits **0** (`logs/test-green.log`), and the
binary itself reports 11 passed / 0 failed (`logs/test-green-verbose.log`).

**The test is deterministic: no wall-clock timing, sleeps or races (name the seam or injected values).**
The injected value is the worker count itself: the first slot asks the constructor for zero workers,
the child process writes `ThreadPool::s_numWorkers = 0` before the global pool is built. Both then
block on `future.get()`, which only returns once the task has run. There is no sleep, no timer, no
`wait_for`, no timeout-based assertion and no thread-order dependency: the first slot uses its own
pool and the second one runs in a child process, so the singleton's construction order relative to
the other slots cannot change the outcome (this is what the accepted branch's regression got wrong —
it asserted on the already-built global singleton). The child returns 2 before enqueueing anything
when the pool has no worker, so the failing run cannot hang either. Ten consecutive runs of the
binary were green.

**The fix is at the cause and complete: every caller and sibling of what you changed was checked and stays consistent.**
The cause is a worker count of zero reaching worker creation. It is clamped at the single place where
workers are started (`src/core/ThreadPool.cpp:32`, inside the constructor, which is the only place
`m_workers.emplace_back` happens) and at the single place where the platform count is cached
(`include/ThreadPool.h:122`); both use the same `workerCount()` helper, so they cannot disagree.
`ThreadPool::instance()` (`src/core/ThreadPool.cpp:101`) forwards the already-clamped cached value.
Callers: `src/gui/FileSearchJob.cpp:57` is the only product user of the global pool and is
unaffected (it enqueues one task and waits for its future; a positive count is passed through
unchanged, only zero is raised to one). Sibling: `src/wasm/WasmWorkerPool.cpp:40-45` is the only
other reader of `hardware_concurrency()` in the tree and already clamps to at least one lane
(`cores > 1 ? cores - 1 : 1`), so it needs no change and was left alone.

**No collateral damage: old projects and saved data still load as before, and any limit or check you added does not hit other callers that legitimately exceed it.**
No project, save-format, undo or command code is touched: the diff is one count clamp and its test.
A positive worker count is passed through unchanged, so a normal machine builds exactly the same pool
as before; only the zero case changes, from a pool with no workers (whose tasks never ran) to a pool
with one worker. No limit was added that another caller could legitimately exceed — the clamp is a
floor of one, and one worker is exactly what the pool needs to make progress.

**Nothing you added runs on the audio thread and allocates, frees, locks, waits, does I/O or emits a signal.**
`ThreadPool` is not on the audio path (`tests/rt-safety-scope.txt` names no ThreadPool symbol) and
the product change is a pure value computation: `workerCount()` compares and returns a `size_t`, it
allocates nothing, takes no lock and emits no signal. It runs once at static initialisation for the
cached count and once per pool construction. No audio-thread function was modified, and no new
control command was added.

**Registration.** The two inherited product files are declared with a reason in
`tests/upstream-modifications.txt` (beside the other ThreadPool declarations, after the
new-p10-413-port entries); `tests/src/core/ThreadPoolTest.cpp` is fork-NEW and already listed in
`tests/fork-sources.txt:2476` and registered as a CTest target in `tests/CMakeLists.txt:663`, so no
new file had to be registered. `feature_check.py` exits 0 (`logs/gate-feature.log`).

## Port check against the newest line

This job is a port, so the change was checked against the line the train failed on. The four changed
files were applied to a `050/m0-train-9` extract:

| Command | Exit | Result |
|---|---:|---|
| `git diff HEAD~1 -- include/ThreadPool.h src/core/ThreadPool.cpp tests/src/core/ThreadPoolTest.cpp tests/upstream-modifications.txt > .../logs/port-patch-vs-train9.diff` | 0 | The port's patch. |
| `rm -rf /tmp/zene-t9 && mkdir -p /tmp/zene-t9 && git archive 050/m0-train-9 \| tar -x -C /tmp/zene-t9` | 0 | The newest line's tree. |
| `cd /tmp/zene-t9 && git apply --check --verbose .../logs/port-patch-vs-train9.diff` | 0 | All four files apply with **no conflict**: `include/ThreadPool.h` and `src/core/ThreadPool.cpp` unchanged there, the three `tests/src/core/ThreadPoolTest.cpp` hunks apply with offsets, and the ledger hunk applies at line 2613. |

Two things were done for that: the regression does not depend on the slot order (the newest line's
`enqueues*` slots build the global singleton before the accepted branch's regression asserted on it),
and the two ledger lines sit beside the other ThreadPool declarations instead of at the end of the
file — the tail of `tests/upstream-modifications.txt` differs between the lines, so an append there
does not apply.
