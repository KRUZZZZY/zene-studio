FIXED

# Cluster new-p15-257 — queue-full fallback recursed until the stack ran out

**Round 2.** The reviewer's finding was correct and is now fixed: with the round-1 code the
fallback's own list could fill, and the branch that then ran a job synchronously nested again
(measured 130 frames for a 16384-job fan-out plus a 128-link chain). Both new regressions
below fail on the round-1 product code and pass on this one.

Worktree `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p15-257`,
branch `bugs/qwen-new-p15-257`. Logs for this round:
`/home/kruzzzzy/.config/kads/zene/state/runs/new-p15-257.r2/logs` (every file named below is in it).

Note on the files the round-1 review did not find: `AGENTS.md`, `HANDOVER-2026-10-05.md` and
`plans/CLAUDE-READ-FIRST.md` do not exist in this worktree —

```
$ ls -1 AGENTS.md HANDOVER-2026-10-05.md plans/CLAUDE-READ-FIRST.md
ls: cannot access 'AGENTS.md': No such file or directory
ls: cannot access 'HANDOVER-2026-10-05.md': No such file or directory
ls: cannot access 'plans/CLAUDE-READ-FIRST.md': No such file or directory
```

— and the round-1 `proof.json` went with the round-1 run folder, which no longer exists. The
manifest for this round is `…/new-p15-257.r2/logs/proof.json`. The rules this job follows are in
the job brief (`…/new-p15-257.r2/brief.md`), which is present.

## The defect

`JobQueue::addJob()` (`src/core/AudioEngineWorkerThread.cpp:279-298`) cannot grow the queue on
the audio path, so a job past `JOB_QUEUE_SIZE` is run on the spot. `process()` runs
`doProcessing()`, and the mixer queues successors from inside it: `MixerChannel::processed()`
(`src/core/Mixer.cpp:465-505`) calls `incrementDeps()` on every receiver it just satisfied, and
`incrementDeps()` (`src/core/Mixer.cpp:535-543`) calls `AudioEngineWorkerThread::addJob(this)`.
Any job queued while the queue is full therefore re-entered the same branch: one nested
`process()` frame per link, i.e. a stack that follows the graph. Reachable because mixer
channels are an unbounded vector and `Mixer::render` resets the queue only once per period
(`src/core/Mixer.cpp:2462`), so a period can enqueue far more than 8192 jobs.

Round 1 made the first spill iterative but kept a synchronous branch for when the fallback's
own list filled — the reviewer's probe: a fan-out that fills the list, then a chain queued from
the spilling job, nested 128 deep again.

## The fix

`src/core/AudioEngineWorkerThread.cpp:126-264`, file-local, preallocated, thread-local:

- `OverflowDrain` (`:168-178`): `OverflowBankCount = 2` banks (`:166`) of
  `OverflowBankCapacity = JOB_QUEUE_SIZE` entries (`:165`). Trivial fields only, so a thread's
  copy is zero-initialised storage: no constructor, no allocation, no lock, no wait.
- `parkOverflowJob()` (`:181-197`): parks in the first bank with room, so a fan-out that fills
  one bank spills into the next instead of running the job inside the frame that queued it.
- `takeOverflowJob()` (`:199-214`): takes an entry out of the first non-empty bank, freeing its
  slot. The drain loop takes before it runs, so the slot an entry leaves behind is the slot its
  own successor is parked in — a chain of any length is one frame deep.
- `processOverflowJob()` (`:216-221`): unchanged per-job semantics — the same `RealtimeScope`,
  one `process()` call, one `m_itemsDone` increment for the owning queue.
- `runOverflowFallback()` (`:225-264`): a thread already draining only parks; the thread that
  entered the fallback runs the banks in a loop. When both banks are full, one *pending* entry
  is taken and run to make room (its own successors are then parked in the slot it freed), and
  only if that still leaves no room does the new job run on the spot. That is what removes the
  sharing of the 128-frame chain: the depth no longer follows the chain, it is 1 while the
  fallback has room and at most 2 when it does not.

Bound, stated rather than hidden: the fallback holds 2 x `JOB_QUEUE_SIZE` = 16384 pending jobs
plus the queue's own 8192, so ~24 576 jobs can be live in one period without any nesting. Work
beyond that still runs on the spot, because the pool is thread-local and preallocated: the only
thread that can free a slot is the one that is blocked, so waiting deadlocks, dropping breaks
the mixer's dependency count (and would hang `wait()`), and a larger guarantee needs either an
allocator or unbounded memory. Both regressions measure the property that matters — depth does
not grow with the chain — and the second pins the constant at 2.

Callers and siblings checked: `AudioEngineWorkerThread::addJob` is the only entry to the queue
(`MixerChannel::incrementDeps` at `src/core/Mixer.cpp:541`, `Mixer::masterMix` at
`src/core/Mixer.cpp:2527`, `fillJobQueue` at `include/AudioEngineWorkerThread.h:100-111`); all
go through the single `JobQueue::addJob` changed here. `run()`, `wait()` and `reset()` are
untouched, and `m_itemsDone` is still incremented exactly once per fallback job, so the
`m_itemsDone < m_writeIndex` bookkeeping `wait()` spins on is unchanged. No header was changed.

## Regressions (deterministic: single thread, no worker, no sleeps, no wall clock)

`tests/src/core/RenderJobQueueTest.cpp`:

- `queueOverflowStaysShallowWhenTheFallbackListIsFull` (`:755-828`) — fills the queue's slots,
  then a 16384-job fan-out fills the whole fallback list, and the chain (128 links) hangs off
  the successor that spills first. Asserts every job ran exactly once and
  `maxDepth == 1`.
- `queueOverflowBoundsTheNestingWhenTheFallbackListIsExhausted` (`:837-903`) — same fill, but
  the chain head is queued from the frame that just exhausted the list, so a pending entry has
  to be run to make room. Asserts every job ran exactly once and `maxDepth <= 2`.
- Helpers: `OverflowFillerJob` (`:159`), `ChainedOverflowJob` (`:172`), `FanOutOverflowJob`
  (`:200`), all in the file's anonymous namespace, all counting their own `doProcessing`
  nesting depth on the test thread (no atomics, no timing).

Nesting depth is measured inside the jobs themselves, so no product seam was added for it.

## Commands and exit codes (copied from the logs)

RED — round-2 regression added, round-1 product code still in place:

```
cd <worktree> && cmake --build build --target RenderJobQueueTest -j4 > LOGS/build-1-red.log 2>&1; echo EXIT=$?
EXIT=0

cd build/tests && ctest -R '^RenderJobQueueTest$' --output-on-failure > LOGS/test-1-red.log 2>&1; echo EXIT=$?
EXIT=8
FAIL!  : RenderJobQueueTest::queueOverflowStaysShallowWhenTheFallbackListIsFull() 'maxDepth == 1' returned FALSE. (a 16384-job fan-out with a 128-link chain nested process() 130 calls deep; ...)
Totals: 14 passed, 1 failed, 0 skipped, 0 blacklisted, 1067ms
```

GREEN — spill banks in place:

```
cmake --build build --target RenderJobQueueTest -j4 > LOGS/build-2-green.log 2>&1; echo EXIT=$?
EXIT=0

cd build/tests && ctest -R '^RenderJobQueueTest$' --output-on-failure > LOGS/test-2-green.log 2>&1; echo EXIT=$?
EXIT=0

cd build/tests && ./RenderJobQueueTest > LOGS/test-3-cases-green.log 2>&1; echo EXIT=$?
EXIT=0          # Totals: 15 passed, 0 failed
```

RED — the exhausted-list regression, against the spill-bank code before the make-room step:

```
cmake --build build --target RenderJobQueueTest -j4 > LOGS/build-10-red-exhausted.log 2>&1; echo EXIT=$?
EXIT=0

cd build/tests && ctest -R '^RenderJobQueueTest$' --output-on-failure > LOGS/test-11-red-exhausted.log 2>&1; echo EXIT=$?
EXIT=8
FAIL!  : RenderJobQueueTest::queueOverflowBoundsTheNestingWhenTheFallbackListIsExhausted() 'maxDepth <= 2' returned FALSE. (an exhausted fallback with a 16384-job fan-out and a 128-link chain nested process() 129 calls deep; ...)
```

GREEN — make-room step in place:

```
cmake --build build --target RenderJobQueueTest -j4 > LOGS/build-12-green-exhausted.log 2>&1; echo EXIT=$?
EXIT=0

cd build/tests && ctest -R '^RenderJobQueueTest$' --output-on-failure > LOGS/test-13-green-exhausted.log 2>&1; echo EXIT=$?
EXIT=0

cd build/tests && ./RenderJobQueueTest > LOGS/test-14-cases-green.log 2>&1; echo EXIT=$?
EXIT=0          # Totals: 16 passed, 0 failed, 0 skipped, 0 blacklisted
```

Step 4b — the whole product change (`fix.patch`, 143 lines, `src/core/AudioEngineWorkerThread.cpp`
only) reversed and restored:

```
git diff -- src/core/AudioEngineWorkerThread.cpp > LOGS/fix.patch; echo EXIT=$?
EXIT=0
git apply -R LOGS/fix.patch; echo EXIT=$?
EXIT=0
cmake --build build --target RenderJobQueueTest -j4 > LOGS/build-15-red-without-fix.log 2>&1; echo EXIT=$?
EXIT=0
cd build/tests && ctest -R '^RenderJobQueueTest$' --output-on-failure > LOGS/test-16-red-without-fix.log 2>&1; echo EXIT=$?
EXIT=8
FAIL!  : RenderJobQueueTest::queueOverflowStaysShallowWhenTheFallbackListIsFull() 'maxDepth == 1' returned FALSE. (a 16384-job fan-out with a 128-link chain nested process() 130 calls deep; ...)
FAIL!  : RenderJobQueueTest::queueOverflowBoundsTheNestingWhenTheFallbackListIsExhausted() 'maxDepth <= 2' returned FALSE. (an exhausted fallback with a 16384-job fan-out and a 128-link chain nested process() 129 calls deep; ...)
Totals: 14 passed, 2 failed, 0 skipped, 0 blacklisted, 1072ms

git apply LOGS/fix.patch; echo EXIT=$?
EXIT=0
cmake --build build --target RenderJobQueueTest zene -j4 > LOGS/build-17-green-with-fix.log 2>&1; echo EXIT=$?
EXIT=0
cd build/tests && ctest -R '^RenderJobQueueTest$' --output-on-failure > LOGS/test-18-green-with-fix.log 2>&1; echo EXIT=$?
EXIT=0           # 100% tests passed out of 1
```

Family (`ctest -R` over the queue, the mixer and the byte-exact render reference):

```
cmake --build build --target zene RoutingGraphScheduleTest AudioWorkerWakeTest MixerConcurrencyTest PdcMixerTest WorkerAltStackTest -j4 > LOGS/build-19-family.log 2>&1; echo EXIT=$?
EXIT=0
cd build/tests && ctest -R '^(RenderJobQueueTest|RoutingGraphScheduleTest|AudioWorkerWakeTest|MixerConcurrencyTest|PdcMixerTest|WorkerAltStackTest)$' --output-on-failure > LOGS/test-20-family.log 2>&1; echo EXIT=$?
EXIT=0           # 100% tests passed out of 6
```

Gates:

```
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > LOGS/gates-1.log 2>&1; echo EXIT=$?
EXIT=0
  PASS  fork-sources / all-sources-reproduce / no-upstream-regression / check-namespace /
        no-tautology / doc-figures
  PASS  complexity            no new item in your files
  PASS  file-length           no new item in your files
RESULT: PASS
```

Registration: both files are inherited and already listed in `tests/upstream-modifications.txt`;
this cluster's two entries (lines 6077-6078) now describe the banks, the make-room step and both
regressions. No new file, so `tests/fork-sources.txt` is unchanged.

## Self-check

- **The regression tests FAIL with the product change reverted and PASS with it.** Yes, both, in
  one run: reversed `EXIT=8` with `queueOverflowStaysShallowWhenTheFallbackListIsFull` at 130
  nested frames and `queueOverflowBoundsTheNestingWhenTheFallbackListIsExhausted` at 129
  (`test-16-red-without-fix.log`), restored `EXIT=0` (`test-18-green-with-fix.log`). Each case
  also has its own RED log against the code as it stood when it was written
  (`test-1-red.log`, `test-11-red-exhausted.log`).
- **The tests are deterministic.** No worker thread, no sleep, no clock, no sampling: one thread
  drives `JobQueue` in `Dynamic` mode with fixed counts (8192 fillers, 16384 fan-out jobs, 128
  chain links) and the depth is an `int` incremented and decremented around `doProcessing()` in
  the test's own job classes. No product seam was needed, so `setWorkerDrainProbeForTesting` is
  not involved.
- **Fix at the cause, callers and siblings consistent.** The only overflow site in the product
  (`JobQueue::addJob`, `:279-298`) now always parks or drains iteratively; all three callers
  (`MixerChannel::incrementDeps`, `Mixer::masterMix`, `fillJobQueue`) share it. `run()`,
  `wait()`, `reset()`, the `m_itemsDone`/`m_writeIndex` contract and the per-job
  `RealtimeScope` are unchanged. No header changed, so no other target sees a new declaration.
- **No collateral damage.** No save/load, model or project code touched. The mixer's arithmetic
  is unchanged: `RoutingGraphScheduleTest`'s byte-identical reference render still matches
  (`test-20-family.log`, `EXIT=0`), and `queueFullFallbackIsCounted` still sees exactly one
  counted fallback for one excess job. No new limit rejects a caller: the fallback only decides
  *where* overflow work runs, and a caller beyond its size still gets its work done, as before.
  Realtime: the added code allocates nothing, frees nothing, locks nothing, waits on nothing and
  logs nothing — a fixed thread-local array, `process()`, and one atomic increment, inside the
  same `RealtimeScope`. Cost: 2 x 8192 x 16 B = 256 KB of thread-local storage per thread that
  enters the fallback (was 128 KB in round 1). Undo and control commands are untouched, so
  `ControlCommandsSnapshot` cannot move.
- **Sibling commands (`app.*`, `arrangement.*` in the pre-submit ASK).** This cluster changes no
  command file and adds no command id: the commit is `src/core/AudioEngineWorkerThread.cpp`,
  `tests/src/core/RenderJobQueueTest.cpp`, `tests/upstream-modifications.txt` and this report
  (`git show --stat`). The command files the check lists come from the other clusters already
  merged into this branch's base (the branch diff against its base is 433 files wide), so
  `commands_snapshot.json` is unchanged by this fix.

## Other findings

- Out of scope, unchanged: `JobQueue::reset()` clears `m_writeIndex`/`m_itemsDone` but not the
  slots of `m_items`, so a caller that resets a queue while slots are occupied leaves those
  pointers behind (`RoutingGraphScheduleTest::queueFullFallbackIsCounted` does exactly that; the
  product path resets before every fill, so it is a test-only pattern).
- The last-resort path (more than ~24 576 live jobs in one period) still runs on the spot. It is
  bounded in *number*, not eliminated; see "Bound" above for why a thread-local preallocated pool
  cannot do better without an allocator, and why waiting or dropping is worse on the audio thread.
