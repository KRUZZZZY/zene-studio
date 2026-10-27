ALREADY-FIXED

# Cluster new-p9-477

Finding: "A zero hardware thread count creates a pool that never runs work" (`include/ThreadPool.h:89`
in the finding; "BUG 9"). The finding asks for the default worker count to be clamped to at least one
before the pool is constructed. This branch already does that, and a regression test for it is already
on the branch. No product code is changed by this cluster.

## Why the finding's line is not the bug

`include/ThreadPool.h:89` on this branch is `promise->set_exception(std::current_exception());` inside
`enqueue`'s `catch (...)`. That line has nothing to do with worker count. The line moved; the
described defect is about the worker count, which is handled at the sites below.

## Evidence (file:line on this branch, HEAD `5c1c6715d9`)

| Layer | Location | What it does |
| --- | --- | --- |
| Default count | `include/ThreadPool.h:111-114` | `workerCount(hw)` returns `1` when `hw == 0`, else `hw` |
| Global singleton count | `include/ThreadPool.h:122` | `s_numWorkers = workerCount(std::thread::hardware_concurrency())` |
| Constructor | `src/core/ThreadPool.cpp:32` | `numWorkers = workerCount(numWorkers);` before the `assert` |
| Constructor assert | `src/core/ThreadPool.cpp:33` | `assert(numWorkers > 0);` — now only fires after the clamp, so it cannot fire for a zero request |
| Singleton | `src/core/ThreadPool.cpp:101` | `static auto s_pool = ThreadPool{s_numWorkers};` |

The clamp arrived in commit `4d56342039` ("fix: clamp ThreadPool workers for cluster new-p10-414-port-port"),
which is an ancestor of HEAD (`git merge-base --is-ancestor 4d56342039 HEAD` → EXIT=0). That commit also
added the regression, `tests/src/core/ThreadPoolTest.cpp`:

- `:121-126` `runZeroConcurrencyChild()` sets `ThreadPool::s_numWorkers = 0`, builds the global pool, and
  requires `numWorkers() >= 1` and that an enqueued task runs.
- `:273-276` `zeroWorkerRequestStillRunsTasks` — `ThreadPool{0}` must report `numWorkers() == 1`.
- `:286-300` `zeroHardwareConcurrencyStillRunsTasks` — runs the child in its own process so the singleton
  is still unbuilt, and requires exit code 0.

Sibling checked: `src/wasm/WasmWorkerPool.cpp:40-44` (`computeLaneCount`) already clamps with
`std::max<std::size_t>(available, 1)`, so a zero `hardware_concurrency()` cannot produce zero lanes there.
That is a separate pool and is not in the finding.

## Test matrix (what the existing test covers)

| Path | Covered by |
| --- | --- |
| Normal path (positive count) | existing `ThreadPoolTest` slots (`enqueues*`, `destructionDrainsQueuedTasks`) |
| Zero request to the constructor | `zeroWorkerRequestStillRunsTasks` |
| Zero hardware concurrency into the singleton | `zeroHardwareConcurrencyStillRunsTasks` (child process) |
| Failure / partial startup | `partialStartupFailureStopsStartedWorkers`, `throwingTaskCompletesItsFutureAndKeepsThePoolUsable` |
| Audio-thread constraints | not applicable (pool construction and enqueue are not on the audio path; no change made) |
| Platform | Linux build tree here; the child-process test is Linux/Qt `QProcess` |

Deterministic: no sleeps; the child has a 10 s bound only as a hang guard, and the pass/fail is decided by
the child's exit code.

## RED / GREEN proof (existing regression, clamp temporarily removed)

The product change used for the proof was a one-line deletion of `src/core/ThreadPool.cpp:32`, saved as
`logs/red-sabotage.patch`, and restored with `git apply -R` afterwards. The working tree is clean again
(`git status --short` empty before the gate run).

| Step | Command | EXIT |
| --- | --- | --- |
| Confirm ancestry | `git merge-base --is-ancestor 4d56342039 HEAD` | 0 |
| Build (first attempt) | `cmake --build build --target ThreadPoolTest -j4` (`logs/build-1.log`) | 2 — ccache could not create a temp file under `dsh/home/ccache-tmp-r6` (`Permission denied`); environment, not code |
| Build (retry) | same, `logs/build-2.log` | 2 — same ccache error |
| Build (workaround) | `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp/ccache-tmp-new-p9-477 cmake --build build --target ThreadPoolTest -j4` (`logs/build-3.log`) | 0 |
| GREEN on HEAD | `cd build/tests && ctest -R '^ThreadPoolTest$' --output-on-failure` (`logs/test-1.log`) | 0 — 1 test executed, 100% passed |
| Sabotage | `sed` deleted `src/core/ThreadPool.cpp:32`; patch saved to `logs/red-sabotage.patch` | — |
| Build (sabotaged) | `cmake --build build --target ThreadPoolTest -j4` (`logs/build-red.log`) | 0 |
| RED | `cd build/tests && ctest -R '^ThreadPoolTest$' --output-on-failure` (`logs/test-red.log`) | 8 — `zeroWorkerRequestStillRunsTasks()` and `zeroHardwareConcurrencyStillRunsTasks()` both `FAIL!  : Compared values are not the same`; 0% passed, 1 failed |
| Restore | `git apply -R logs/red-sabotage.patch` | 0 — `git status` clean |
| Build (restored) | `cmake --build build --target ThreadPoolTest -j4` (`logs/build-green.log`) | 0 |
| GREEN after restore | `cd build/tests && ctest -R '^ThreadPoolTest$' --output-on-failure` (`logs/test-green.log`) | 0 — 1 test executed, 100% passed |

Affected family: `ThreadPoolTest` (1 ctest entry, which runs all slots); executed count 1, not zero.

## Gates

The wrapper was run as the procedure says:

`python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-477` → EXIT=1
(`logs/gates-1.log`). It stopped before running any gate: `PermissionError` creating
`zene-qwen-new-p9-477-feature-check` beside the worktree, which is outside the write-jail. I did not
change the wrapper or the gate scripts.

So the same eight gate commands were run directly from the worktree, with each log and exit code in the
log folder (`logs/gates-direct-summary.txt`):

| Gate | Command | EXIT |
| --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 |
| check-namespace | `tests/scripted/check-namespace` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 — lists `REGRESSION` items only in files this branch does not change (e.g. `src/wasm/WasmSandbox.cpp`, `tests/src/gui/XyPadPairTest.cpp`) |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 — lists `REGRESSION` items only in files this branch does not change (e.g. `plugins/Vst3Effect/Vst3Host.cpp`, `src/core/TakeLane.cpp`) |

For the two ratchet gates, the wrapper's rule is to fail only on items that are new in a file the branch
changed. This branch changes no files relative to its base (`git diff --name-only <merge-base with 050/m0-train-19>..HEAD`
→ 0 lines), so by that rule they would pass. I could not run the wrapper's baseline comparison, because it
writes a baseline cache and a worktree in `zene-cachyos`, both outside my jail. That is unverified, and
I'm reporting it as such rather than as a pass.

## Files committed

- `docs/qwen-fixes-2026-10/cluster-new-p9-477.md` (this report) — the only file in this cluster's commit.

No product file, test, or registration file is changed.

## Self-check

- **Regression FAILS with the fix reverted, PASSES with it.** Yes. Reverted: `ctest -R '^ThreadPoolTest$'`
  → EXIT=8 (`logs/test-red.log`, two `FAIL!  : ... Compared values are not the same`). Restored:
  same command → EXIT=0 (`logs/test-green.log`, 1 test executed). Build exits for both: 0 and 0.
- **Deterministic.** No sleeps and no wall-clock assertions. The child (`tests/src/core/ThreadPoolTest.cpp:121-135`)
  blocks on `future.get()` and returns 0 only if the task ran. The parent's `waitForFinished(10000)` at `:291`
  exists only as a hang guard; a hang fails the test, it does not pass it. The seam is the existing
  `ThreadPool::s_numWorkers` (`include/ThreadPool.h:122`) set to 0 inside the child process.
- **Fix at the cause, callers and siblings.** The cause is the unclamped count, and the clamp sites are
  `include/ThreadPool.h:111-114`, `:122` and `src/core/ThreadPool.cpp:32`. Every caller of the global pool
  (`ThreadPool::instance()`, e.g. `src/gui/FileSearchJob.cpp:57`) goes through `s_pool` at
  `src/core/ThreadPool.cpp:101`, so it inherits the clamp. The other `hardware_concurrency()` user in the
  thread-pool area, `src/wasm/WasmWorkerPool.cpp:40-44`, already clamps to at least one lane.
- **No collateral damage.** This cluster changes no product code. The clamp only changes a zero request,
  which could never run tasks before, so positive counts and existing callers are unaffected.
- **ALREADY-FIXED evidence.** `include/ThreadPool.h:111-114,122`, `src/core/ThreadPool.cpp:32-33`, commit
  `4d56342039` (ancestor of HEAD, EXIT=0), regression `tests/src/core/ThreadPoolTest.cpp:121-135,273-300`.
  The finding's `include/ThreadPool.h:89` is `set_exception` (`:89`), not the worker count.
- **Gate row FAIL (environment, not code).** `feature_check.py` cannot create its log folder beside the
  worktree (`PermissionError`, `logs/gates-1.log`), so the wrapper's table could not be produced. Its eight
  commands were run directly (table above); six exit 0, and the two ratchet gates exit 1 on items in
  files this branch does not change. The wrapper's baseline comparison is unverified. This needs a
  writable parent folder or the wrapper's log directory set elsewhere by the owner.
