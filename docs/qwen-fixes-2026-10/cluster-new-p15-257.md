FIXED

# Cluster new-p15-257 — queue-full fallback recursed until the stack ran out

Verdict: **FIXED** (REAL defect, reproduced). Branch `bugs/qwen-new-p15-257`, worktree
`/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p15-257`.
All logs are in `/home/kruzzzzy/.config/kads/zene/state/runs/new-p15-257.r1/logs`.

## The defect, and why it is real

`src/core/AudioEngineWorkerThread.cpp:222-240` (`JobQueue::addJob`, before this fix): when
`m_writeIndex` reaches `JOB_QUEUE_SIZE` the excess job cannot take a slot, so the old code
ran it on the spot:

```cpp
queueFullRefusals.fetch_add(1, std::memory_order_relaxed);
const RealtimeScope realtime;
_job->process();          // <-- executes the job inside addJob's frame
++m_itemsDone;
```

`process()` calls `doProcessing()`, and the mixer's channels queue their successors from
inside their own `doProcessing()`: `MixerChannel::processed()` (`src/core/Mixer.cpp:465-505`)
calls `incrementDeps()` on every receiver it just satisfied, and `incrementDeps()`
(`src/core/Mixer.cpp:535-543`) calls `AudioEngineWorkerThread::addJob(this)`. So any job
queued while the queue is full re-entered `addJob()`, found the queue still full
(`m_writeIndex` only grows until the next `reset()`, and `Mixer::render` resets it once per
period — `src/core/Mixer.cpp:2462`), and took the same branch again: one nested `process()`
frame per link of the dependency chain.

Reachability: the queue holds `JOB_QUEUE_SIZE = 8192` slots (`include/AudioEngineWorkerThread.h:60`)
and mixer channels are an unbounded vector (AGENTS.md, "Verified facts"), so a project with
more than 8192 channels in one period — or any caller that queues past the bound — makes the
overflow path the path for every remaining job, and the C++ stack then follows the graph's
depth. That is the finding's claim, and it reproduces deterministically (below).

## The fix (at the cause)

`src/core/AudioEngineWorkerThread.cpp:126-205`, new file-local machinery:

- `OverflowJob` — one job the fallback still owes, plus a pointer to the owning queue's
  `m_itemsDone` (the queue counts an overflow job's enqueue in `m_writeIndex` before it knows
  the queue is full, so the completion must still be recorded once).
- `OverflowDrainList` / `tl_overflowDrain` (`:161-168`) — a preallocated, thread-local
  pending list of `JobQueue::JOB_QUEUE_SIZE` entries. Trivial fields only, so it is
  zero-initialised storage with no constructor, no allocation, no lock and no wait.
- `processOverflowJob()` (`:172-177`) — unchanged per-job semantics: the old
  `RealtimeScope` plus `job->process()` plus one `m_itemsDone` increment.
- `runOverflowFallback()` (`:181-205`) — the change: a thread that is already draining the
  fallback appends to the list and returns; the thread that entered the fallback runs the
  list in a `while` loop. The stack therefore stays one job deep however long the chain is.
- `addJob()`'s overflow branch (`:229-236`) now calls `runOverflowFallback(_job, m_itemsDone)`
  instead of `_job->process()`.

Only the order in which fallback work is executed changes (depth-first nesting becomes an
iterative drain of the same set of jobs, last-queued first). Job order is not a contract of
this queue: `run()` already hands slots to several workers concurrently, and the mixer
schedules by dependency count, not by queue position. `MixerChannel::processed()` is the
last statement of `MixerChannel::doProcessing()` (`src/core/Mixer.cpp:1042`) and the
receivers' sidechain/feedback taps are written before it, so running a receiver after the
sender's `doProcessing()` returns reads exactly the data the inline version read.

Edge case the storage cannot cover, stated rather than hidden: a full queue *and*
`JOB_QUEUE_SIZE` jobs already parked in the fallback (more than 2 x `JOB_QUEUE_SIZE` jobs in
one period) runs that one job on the spot — the old path, at a scale the queue itself no
longer covers. Bounded memory and zero allocation make a larger guarantee impossible; the
fallback holds as much work as the queue it stands in for.

Callers and siblings checked: `AudioEngineWorkerThread::addJob` is the only entry to the
queue, called from `MixerChannel::incrementDeps()` (`src/core/Mixer.cpp:541`),
`Mixer::masterMix` (`src/core/Mixer.cpp:2527`), and `fillJobQueue()` for the play-handle and
bus stages (`include/AudioEngineWorkerThread.h:100-111`). All of them go through the single
`JobQueue::addJob` changed here; none has its own overflow path. `run()`/`wait()` are
untouched, and `m_itemsDone` is still incremented once per fallback job, so the
`m_itemsDone < m_writeIndex` bookkeeping `wait()` spins on is unchanged.

## Regression test

`tests/src/core/RenderJobQueueTest.cpp:660-712`
(`queueOverflowDoesNotNestProcessCallsForAJobChain`), with two helpers at `:159-196`:
`OverflowFillerJob` (always claimable, fills all 8192 slots) and `ChainedOverflowJob`
(queues its successor from inside `doProcessing()` — the mixer's shape — and records the
nesting depth of `doProcessing()`).

The case is single-threaded and clock-free: no worker is started, nothing sleeps, nothing is
timed. It resets the queue in `Dynamic` mode (the mixer's mode), fills every slot, queues a
256-link chain, and asserts (a) every link ran exactly once and (b) the deepest nesting of
`doProcessing()` was 1. It then drains the filler slots and resets the queue, so the other
cases in the family start from an empty queue.

Failure message seen without the fix: `the queue-full fallback nested 256 process() calls for
a 256-link chain` — the nesting depth equals the chain length, which is the defect.

## Commands and exit codes (copied from the logs)

Logs: `/home/kruzzzzy/.config/kads/zene/state/runs/new-p15-257.r1/logs`.

RED — test added, product code untouched:

```
cmake --build build --target RenderJobQueueTest -j4 > LOGS/build-1-red.log 2>&1; echo EXIT=$?
EXIT=0

cd build/tests && ctest -R '^RenderJobQueueTest$' --output-on-failure > LOGS/test-1-red.log 2>&1; echo EXIT=$?
EXIT=8
FAIL!  : RenderJobQueueTest::queueOverflowDoesNotNestProcessCallsForAJobChain() 'maxDepth == 1' returned FALSE. (the queue-full fallback nested 256 process() calls for a 256-link chain; ...)
Totals: 13 passed, 1 failed, 0 skipped, 0 blacklisted, 1067ms
```

GREEN — fix in place:

```
cmake --build build --target RenderJobQueueTest -j4 > LOGS/build-2-green.log 2>&1; echo EXIT=$?
EXIT=0

cd build/tests && ctest -R '^RenderJobQueueTest$' --output-on-failure > LOGS/test-2-green.log 2>&1; echo EXIT=$?
EXIT=0

cd build/tests && ./RenderJobQueueTest > LOGS/test-6-cases-green.log 2>&1; echo EXIT=$?
EXIT=0
PASS   : RenderJobQueueTest::queueOverflowDoesNotNestProcessCallsForAJobChain()
Totals: 14 passed, 0 failed, 0 skipped, 0 blacklisted, 1072ms
```

Step 4b — prove the test catches the bug (`git apply -R`, rebuild, run):

```
git diff -- src/core/AudioEngineWorkerThread.cpp > LOGS/fix.patch; echo EXIT=$?
EXIT=0                          (107 lines, product file only)

git apply -R LOGS/fix.patch; echo EXIT=$?
EXIT=0

cmake --build build --target RenderJobQueueTest -j4 > LOGS/build-4-red-without-fix.log 2>&1; echo EXIT=$?
EXIT=0

cd build/tests && ctest -R '^RenderJobQueueTest$' --output-on-failure > LOGS/test-4-red-without-fix.log 2>&1; echo EXIT=$?
EXIT=8
FAIL!  : RenderJobQueueTest::queueOverflowDoesNotNestProcessCallsForAJobChain() 'maxDepth == 1' returned FALSE. (the queue-full fallback nested 256 process() calls for a 256-link chain; ...)

git apply LOGS/fix.patch; echo EXIT=$?
EXIT=0

cmake --build build --target RenderJobQueueTest zene -j4 > LOGS/build-5-green-with-fix.log 2>&1; echo EXIT=$?
EXIT=0

cd build/tests && ctest -R '^RenderJobQueueTest$' --output-on-failure > LOGS/test-5-green-with-fix.log 2>&1; echo EXIT=$?
EXIT=0
100% tests passed out of 1
```

Family (`ctest -R` around the queue, the mixer and the render reference):

```
cmake --build build --target zene RoutingGraphScheduleTest AudioWorkerWakeTest MixerConcurrencyTest PdcMixerTest WorkerAltStackTest -j4 > LOGS/build-3-family.log 2>&1; echo EXIT=$?
EXIT=0

cd build/tests && ctest -R '^(RenderJobQueueTest|RoutingGraphScheduleTest|AudioWorkerWakeTest|MixerConcurrencyTest|PdcMixerTest|WorkerAltStackTest)$' --output-on-failure > LOGS/test-3-family.log 2>&1; echo EXIT=$?
EXIT=0
100% tests passed out of 6

cmake --build build --target RoutingGraphScheduleTest AudioWorkerWakeTest MixerConcurrencyTest PdcMixerTest WorkerAltStackTest -j4 > LOGS/build-6-family-relink.log 2>&1; echo EXIT=$?
EXIT=0

cd build/tests && ctest -R '^(RenderJobQueueTest|RoutingGraphScheduleTest|AudioWorkerWakeTest|MixerConcurrencyTest|PdcMixerTest|WorkerAltStackTest)$' --output-on-failure > LOGS/test-7-family-final.log 2>&1; echo EXIT=$?
EXIT=0
100% tests passed out of 6
```

`RoutingGraphScheduleTest` includes the byte-identical reference render of the routing graph
(`publishedScheduleMatchesThePlanOrderExecution`), which still matches, and its
`queueFullFallbackIsCounted` case still observes exactly one counted fallback for one excess
job. `RenderJobQueueTest::queueOverflowRunsEveryJob` (8193 jobs through a static queue) still
passes, so the non-chained overflow path is unchanged.

Registration and gates:

```
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p15-257 > LOGS/gates-1.log 2>&1; echo EXIT=$?
EXIT=0
  PASS  fork-sources / all-sources-reproduce / no-upstream-regression / check-namespace /
        no-tautology / doc-figures
  PASS  complexity            no new item in your files
  PASS  file-length           no new item in your files
RESULT: PASS
```

`src/core/AudioEngineWorkerThread.cpp` and `tests/src/core/RenderJobQueueTest.cpp` were already
registered in `tests/upstream-modifications.txt`; this cluster's reason is appended there (two
lines, file still ends with a newline). No new file was added, so `tests/fork-sources.txt` is
unchanged.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.** Yes:
  `EXIT=8` with `git apply -R LOGS/fix.patch` (log `test-4-red-without-fix.log`, message names
  the 256 nested `process()` calls) and `EXIT=0` restored (log `test-5-green-with-fix.log`).
  The first RED run before the fix existed at all (`test-1-red.log`) shows the same failure.
- **The test is deterministic.** No wall clock, no sleeps, no worker thread, no race: it drives
  `JobQueue` directly on the test thread, in `Dynamic` mode, with a fixed 256-link chain, and
  measures nesting by incrementing/decrementing an `int` around `doProcessing()` in the test's
  own job class. The only "seam" is the product's existing `queueFullRefusals` counter, which
  this case does not need.
- **Fix at the cause, callers and siblings consistent.** The single overflow site is fixed
  (`addJob`, now `runOverflowFallback`); all three callers of the queue (`MixerChannel::incrementDeps`,
  `Mixer::masterMix`, `fillJobQueue`) share it. `run()`, `wait()`, `reset()` and the
  `m_itemsDone`/`m_writeIndex` contract are untouched; the per-job `RealtimeScope` and the
  completion increment moved into `processOverflowJob()` unchanged. The header was not
  changed, so no other target sees a new declaration; `zene` and every test in the family were
  rebuilt and run anyway.
- **No collateral damage.** Old projects: no save/load, project or model code touched — the
  change is inside the job queue only. The mixer's arithmetic is unaffected: the byte-identical
  routing-graph reference render still matches (`RoutingGraphScheduleTest`, `EXIT=0`). No new
  limit or check rejects a caller: the fallback's capacity equals the queue's, and a caller
  that exceeds both still gets its work done (on the spot), exactly as before. Realtime: the
  added code allocates nothing, frees nothing, locks nothing, waits on nothing and logs
  nothing — a fixed thread-local array plus `process()` and one atomic increment, inside the
  same `RealtimeScope` as before. Undo and control commands: untouched (no `Song`, undo or
  `ControlCommands` change), so `ControlCommandsSnapshot` is unaffected.
- **Verdict is not ALREADY-FIXED or FALSE-POSITIVE**, so no code-reference defence is needed;
  the "before" code is quoted above with `file:line`.

## Other findings

- Not fixed here, out of scope: `JobQueue::reset()` clears `m_writeIndex`/`m_itemsDone` but not
  the slots of `m_items`, so a caller that resets a queue while slots are still occupied leaves
  those pointers behind (`run()` then counts them when a later fill reaches their index). The
  existing `RoutingGraphScheduleTest::queueFullFallbackIsCounted` does exactly that; it is a
  test-only pattern and does not affect the product path, where `Mixer::render` resets before
  every fill.
- `AudioEngineWorkerThread::JobQueue` is a `public` nested class with a `public` constructor, so
  tests can instantiate an independent queue; that is what makes the new case hermetic. Noted
  in case a later tightening makes it private.
