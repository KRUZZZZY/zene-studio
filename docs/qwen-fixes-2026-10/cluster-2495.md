# Cluster 2495 — Arrangement Record: an unmatched start makes a performance unlandable

Verdict: FIXED — all three reports are REAL and share one root cause.

2495: FIXED
2492: FIXED
new-p16-212: FIXED

LOGS = `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/2495.r2/logs`.

## The shared cause (found once, fixed at each of its three doors)

Arrangement Record carries a performance in a ring of start/stop events
(`include/SessionArrangementRecorder.h`). The land pass pairs them per cell
(`src/core/ControlCommandsSessionRecordInternal.h:103` `landPairEvents()`) and
**refuses an unpaired start without consuming anything** — a start with no stop
makes the whole performance unlandable, for good: there is no reset or recovery
operation on the recorder.

So the single invariant every one of these reports breaks is: *the ring must never
end up holding a start that no stop closes.* Three different doors let it happen,
and each door needed its own fix because they fail for genuinely different
reasons — but they are the same defect, seen from three sides:

| Door | How the start is left open | Fix |
|---|---|---|
| 2495 | the model thread disarms and **snapshots the ring before the in-flight audio-thread write publishes** — the start lands behind the consumer's back | disarm handshake: the consumer waits out the in-flight producer write |
| new-p16-212 | the tap is switched off while a cell still plays — **the closing stop is dropped** because `push()` refused it while disarmed | a disarmed tap still tapes the stop that closes a start the ring already carries |
| 2492 | a cell is **retriggered** — a second start is recorded without closing the span the first start opened | the retrigger closes the previous span before it opens the next one |

The three fixes live in the two places all the traffic goes through
(`SessionArrangementRecorder::push()` / `shouldRecord()` and
`SessionScheduler::afterLaunchEvents()`), so no third path can slip past them.

## 2495 — Disarming Arrangement Record can leave an unmatched event

**Evidence.** `include/SessionArrangementRecorder.h:164` `push()` read `armed`
and published later; `snapshot()` (line 221) read the ring without any barrier
against a producer that had already passed the armed gate. So the model thread
could disarm, snapshot (the start is not there yet), and the audio thread could
then publish the start — a start the consumer never saw, with no stop to close
it.

**Fix.** `include/SessionArrangementRecorder.h:170` — the producer counts itself
in flight around its write (`m_producerInFlight`, line 322); the consumer waits
that count out in `waitForProducer()` (line 297), called from `snapshot()`
(line 227). **The wait is on the model thread only**: the audio thread does one
atomic increment and one decrement and never waits. `setArmed()`/`armed()` are
`seq_cst` (line 127), so the two outcomes are exhaustive: a push that read
`true` is in the snapshot, a push that starts after the disarm reads `false` and
records nothing.

**Regression test.** `tests/src/core/SessionArrangementRecordTest.cpp:275`
`snapshotWaitsForAnInFlightProducerWrite()` — a REAL second thread parks inside
the write (a `LMMS_TESTING` hook, `setPushBoundaryHookForTest`,
`include/SessionArrangementRecorder.h:136`), the model thread disarms and
snapshots, and the consumer's wait hook releases the parked producer. No sleep,
no timer, no wall clock.

**RED** (`logs/fix-2495.patch` reversed, `logs/build-red-2495.log`,
`logs/test-red-2495.log`):
```
git apply -R logs/fix-2495.patch                       # EXIT=0
cmake --build build --target SessionArrangementRecordTest   # EXIT=0
cd build/tests && ctest -R '^SessionArrangementRecordTest$' --output-on-failure   # EXIT=8
  FAIL! : SessionArrangementRecordTest::snapshotWaitsForAnInFlightProducerWrite()
         'g_consumerWaited.load( std::memory_order_acquire )' returned FALSE.
```
**GREEN** (`logs/fix-2495.patch` re-applied, `logs/build-green-2495.log`,
`logs/test-green-2495.log`):
```
git apply logs/fix-2495.patch                          # EXIT=0
cmake --build build --target SessionArrangementRecordTest   # EXIT=0
cd build/tests && ctest -R '^SessionArrangementRecordTest$' --output-on-failure   # EXIT=0
  1/1 Test #497: SessionArrangementRecordTest .....   Passed
```

## new-p16-212 — An unmatched arrangement start can make recording impossible to land

**Evidence.** `include/SessionArrangementRecorder.h:261` `shouldRecord()`
refused *every* event while the tap was disarmed — including the stop that ends
a cell whose start is already in the ring. The ring kept the open start, and
`landPairEvents()` refuses it on every pass without consuming: the performance
can never be landed. The user-visible path is `session.back_to_arrangement`
(`src/core/ControlCommandsSessionRecord.cpp`): it disarms the tap and *then*
resets the session, and the reset records the stop of every playing slot
(`src/core/SessionFollow.cpp:458`) — every one of those stops was being dropped.

**Fix.** `include/SessionArrangementRecorder.h:261-268` — a disarmed tap still
records no new **start**, but a **stop** that closes a start the ring already
carries is taped (`closesRecordedStart()`, line 276). The check is one bounded
pass over the ring's own events (at most `Capacity` entries), on the audio
thread, with no allocation and no second piece of state to desynchronise; the
pairing rule is exactly the one `landPairEvents()` applies (FIFO per cell).

**Regression tests.**
`tests/src/core/SessionArrangementRecordTest.cpp:327`
`recorderTapesTheStopThatClosesARecordedStart()` (the unit rule: the closing
stop is taped, a stop that closes nothing is not, a start is never taped while
disarmed) and `:356` `aDisarmedTapStillClosesTheStartItRecorded()` (the
scheduler path: disarm while a cell plays, the cell stops afterwards, nothing is
left open); plus the user-visible land path,
`tests/src/core/ControlSessionRecordLandTest.cpp:371`
`aDisarmedTapStillLandsThePerformanceItRecorded()` (`session.arrangement_record_land`
lands one clip and consumes the ring).

**RED** (`logs/fix-new-p16-212.patch` reversed, `logs/build-red-new-p16-212.log`,
`logs/test-red-new-p16-212.log`):
```
git apply -R logs/fix-new-p16-212.patch                # EXIT=0
cmake --build build --target SessionArrangementRecordTest ControlSessionRecordLandTest   # EXIT=0
cd build/tests && ctest -R '^(SessionArrangementRecordTest|ControlSessionRecordLandTest)$' --output-on-failure   # EXIT=8
  FAIL! : ControlSessionRecordLandTest::aDisarmedTapStillLandsThePerformanceItRecorded()
         'recorder().recordStop( 0, 0, 192 )' returned FALSE.
  FAIL! : SessionArrangementRecordTest::recorderTapesTheStopThatClosesARecordedStart()
         'recorder.recordStop( 0, 0, 288 )' returned FALSE.
  FAIL! : SessionArrangementRecordTest::aDisarmedTapStillClosesTheStartItRecorded()
         Compared values are not the same
```
**GREEN** (`logs/fix-new-p16-212.patch` re-applied, `logs/build-green-new-p16-212.log`,
`logs/test-green-new-p16-212.log`):
```
git apply logs/fix-new-p16-212.patch                   # EXIT=0
cmake --build build --target zene SessionArrangementRecordTest ControlSessionRecordLandTest SessionTransportBarTest   # EXIT=0
cd build/tests && ctest -R '^(SessionArrangementRecordTest|ControlSessionRecordLandTest|SessionTransportBarTest)$' --output-on-failure   # EXIT=0
  100% tests passed out of 3
```

## 2492 — Retriggered launches can leave arrangement recordings with unmatched starts

**Evidence.** `src/core/SessionFollow.cpp:228-230` `afterLaunchEvents()` — the
`Started` and the `Retriggered` branch both went on to record a start
(`recordArrangementStart()`, line 230), but only `Started` closed the previous
span (`takeColumnOver()`, line 206). A retrigger therefore left the span the
first start opened with no stop, and a later stop could only close one of the two
starts: one open start, and a land pass that refuses it.

**Fix.** `src/core/SessionFollow.cpp:229` — the `Retriggered` branch closes the
previous span before the new start is recorded:
`else { m_recorder.recordStop( slot.track, slot.scene, slot.state.startedTick ); }`.
The stop carries the retrigger's own tick, so the landed clip is the span the
cell actually played. A `Restart` follow action is deliberately *not* recorded
(`src/core/SessionFollow.cpp:328-341`): it is the same playback continuing, so
its clip is the one span from the launch to the stop that ends it.

**Regression test.** `tests/src/core/SessionArrangementRecordTest.cpp:609`
`repeatRetriggerRecordsAStopBeforeItsNextStart()` — a `Repeat` launch records
start(tick 0), then the retrigger at bar 1 records stop(bar 1) **before**
start(bar 1): three events, and every start is closed.

**RED** (`logs/fix-2492.patch` reversed, `logs/build-red-2492.log`,
`logs/test-red-2492.log`):
```
git apply -R logs/fix-2492.patch                        # EXIT=0
cmake --build build --target SessionArrangementRecordTest   # EXIT=0
cd build/tests && ctest -R '^SessionArrangementRecordTest$' --output-on-failure   # EXIT=8
  FAIL! : SessionArrangementRecordTest::repeatRetriggerRecordsAStopBeforeItsNextStart()
         Compared values are not the same
```
**GREEN** (`logs/fix-2492.patch` re-applied, `logs/build-green-2492.log`,
`logs/test-green-2492.log`):
```
git apply logs/fix-2492.patch                           # EXIT=0
cmake --build build --target SessionArrangementRecordTest   # EXIT=0
cd build/tests && ctest -R '^SessionArrangementRecordTest$' --output-on-failure   # EXIT=0
  1/1 Test #497: SessionArrangementRecordTest .....   Passed
```

## Evidence table

| Bug | Verdict | Product change | RED (reverted fix) | GREEN (fix applied) |
|---|---|---|---|---|
| 2495 | FIXED | `include/SessionArrangementRecorder.h` | `ctest -R '^SessionArrangementRecordTest$'` → **EXIT=8** (`snapshotWaitsForAnInFlightProducerWrite`, `g_consumerWaited` FALSE) | same command → **EXIT=0** (1/1 passed) |
| 2492 | FIXED | `src/core/SessionFollow.cpp` | `ctest -R '^SessionArrangementRecordTest$'` → **EXIT=8** (`repeatRetriggerRecordsAStopBeforeItsNextStart`) | same command → **EXIT=0** (1/1 passed) |
| new-p16-212 | FIXED | `include/SessionArrangementRecorder.h` | `ctest -R '^(SessionArrangementRecordTest\|ControlSessionRecordLandTest)$'` → **EXIT=8** (3 assertions) | same command → **EXIT=0** (2/2 passed) |

Affected family, with every fix in place (`logs/test-green-new-p16-212.log`):
`SessionArrangementRecordTest`, `ControlSessionRecordLandTest`,
`SessionTransportBarTest` — **3/3 passed, EXIT=0**.

Baseline before any of the RED runs (`logs/build-1.log`, `logs/test-baseline.log`):
build EXIT=0, the same three tests 3/3 passed, EXIT=0.

## Registration

No product-new files: `include/SessionArrangementRecorder.h` and
`src/core/SessionFollow.cpp` are fork-authored and already listed in
`tests/fork-sources.txt` (lines 1060 and 1848), so `tests/upstream-modifications.txt`
is untouched. The test files were already registered. No control command was
added, so the `ControlCommandsSnapshot` canary does not move.

## Self-check

1. **The regression test FAILS with the product change reverted and PASSES with
   it.** Proven per bug above, with the exact commands and exit codes: 2495
   EXIT=8 → EXIT=0, 2492 EXIT=8 → EXIT=0, new-p16-212 EXIT=8 → EXIT=0. Each
   revert was a patch of the product files only (`logs/fix-2495.patch`,
   `logs/fix-2492.patch`, `logs/fix-new-p16-212.patch`), reversed with
   `git apply -R` and re-applied with `git apply`; both directions were
   round-tripped and checked with `git diff --stat` before each run.

2. **The test is deterministic: no wall-clock timing, sleeps or races.** The
   concurrency test drives a real second thread through a `LMMS_TESTING` seam
   (`setPushBoundaryHookForTest` / `setConsumerWaitHookForTest`,
   `include/SessionArrangementRecorder.h:136,140`): the producer parks on an
   atomic flag, the consumer releases it, and the only scheduling primitive is
   `std::this_thread::yield()`. No timer, no sleep, no elapsed-time comparison.
   The other regressions inject ticks (96, 288, 192, bar 1) rather than reading a
   clock.

3. **The fix is at the cause and complete.** Every producer of ring events goes
   through `SessionArrangementRecorder::push()` → `shouldRecord()`, so the 2495
   handshake and the new-p16-212 rule apply to *all* of them: the launch start
   (`src/core/SessionFollow.cpp:230`), the column take-over stop (line 206), the
   final stop (line 245), the follow-action stop and scene switch (lines 322,
   353, 369), and the reset stop (line 458). Every consumer of the ring reads it
   through `snapshot()`, which is where the fence is (`include/SessionArrangementRecorder.h:227`);
   `pop()` is only ever called after a snapshot, to consume exactly the events
   the snapshot counted (`src/core/ControlCommandsSessionRecordInternal.h:141`).
   The retrigger fix sits in the one branch that opens a second span
   (`src/core/SessionFollow.cpp:229`), beside the sibling `Started` branch that
   already closed the previous span.

4. **No collateral damage.** The recorder is runtime state: no project file,
   save format or saved data changed, so old projects load and render exactly as
   before. No limit was added or moved — `Capacity` is untouched (128, two
   events per slot for a whole worst-case performance). Behaviour outside the
   bugs is unchanged: a disarmed tap still records no new *start*, a stop that
   closes nothing is still not taped, an unarmed recorder still records nothing,
   and the `landable_pairs` / `open_starts` reading of
   `session.arrangement_record_status` is unchanged.

5. **Realtime, undo and commands.** The audio thread does not wait, allocate,
   free, lock or log: `push()` adds one atomic increment and one atomic
   decrement around its write (fixed storage, no allocation), and
   `closesRecordedStart()` is a bounded pass over the ring's own events
   (`Capacity` entries at most) with no allocation and no second state to keep in
   step. The waiting is on the model thread, in `snapshot()`, which is reached
   from command handlers only — never from `process()`. No new control command
   was added, so undo is unaffected: none of these paths records an undo step,
   and no existing command's semantics changed.

6. **Gates.** `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py
   <worktree>` → `logs/gates-1.log`, **EXIT=0** (RESULT: PASS; fork-sources,
   all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology
   and doc-figures all PASS, complexity and file-length "no new item in your
   files"). `python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py
   <worktree> 2495` → `logs/submit-check-4.log`, **EXIT=0** (RESULT: PASS -
   submit; the five ASK items are answered above). `git status --porcelain` is
   empty.

## Files committed

- `include/SessionArrangementRecorder.h` — the disarm handshake (2495) and the
  disarmed-tap closing stop (new-p16-212)
- `src/core/SessionFollow.cpp` — the retrigger closes the previous span (2492)
- `tests/src/core/SessionArrangementRecordTest.cpp` — the three regressions
- `tests/src/core/ControlSessionRecordLandTest.cpp` — the land-path regression
- `docs/qwen-fixes-2026-10/cluster-2495.md` — this report
