FIXED

# cluster new-p12-267: the period wait could end after the period

## Defect

`RemotePlugin::periodDeadlineForTesting()` reserved only `budget / 8` before the end
of the render period. `receiveFrameBefore()` converts the remaining span to whole
milliseconds with `std::chrono::ceil`, so a wait of e.g. 0.5 ms became 1 ms and could
end just after the period. The rest of the graph then had no time left for a late
client. The p1 baseline failed 33 of 72 injected time-point cases.

## Fix

`src/core/RemotePlugin.cpp:497-500` now reserves `std::max(budget / 8, 1 ms)`:

```cpp
const auto minimum = std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::milliseconds{1});
const auto usable = budget - std::max(budget / 8, minimum);
```

This is a deadline-margin change only. The shared millisecond conversion stays
`ceil` (`src/common/RemotePluginBase.cpp:630-634`): changing it to `floor` would
shorten every bounded wait, including main-thread queries, and would need its own
product seam. The margin is scoped to the processing-period deadline.

## Gate evidence

- `feature_check.py` on this worktree: `RESULT: PASS`, exit 0, 8 PASS / 0 FAIL.
  Full table: `logs/gate-feature.log`.
- Re-run after this report edit: `RESULT: PASS`, exit 0, 8 PASS / 0 FAIL.
  Log: `logs/gate-feature-2.log`.
- `./RemotePluginAudioPortsTest periodWaitNeverEndsAfterThePeriod` run directly:
  `PASS`, `Totals: 3 passed, 0 failed, 0 skipped`.
  Log: `logs/test-green-slot.log`.
- Before the commit, `git diff --stat` showed only `src/core/RemotePlugin.cpp`
  (4 insertions / 2 deletions); the committed tree is checked with `git show --stat HEAD`.
  No test file and no `SystemSemaphore.cpp` changed.

## Caller audit

**(a) SystemSemaphore::tryAcquire is untouched and still honours the integer
timeout.** POSIX (`src/common/SystemSemaphore.cpp:122-131`) builds a
`steady_clock` deadline from the clamped `timeoutMs`, polls `sem_trywait()` and
returns false at the deadline. Win32 (`:173-176`) passes the same clamped value to
`WaitForSingleObject`. Neither file is in the diff.

**(b) Audio-thread path.** `RemotePlugin::processPeriod()`
(`src/core/RemotePlugin.cpp:545`) calls
`collectProcessingReplies(periodDeadline(start))` at `:627`.
`collectProcessingReplies` (`:656-692`) calls
`receiveFrameBefore(slice, m, true)` at `:676`. `receiveFrameBefore`
(`src/common/RemotePluginBase.cpp:627-645`) converts the remaining span with
`std::chrono::ceil` at `:632` and passes it to
`m_in->waitMessageFor(remainingMs())` (`:640`) and
`m_in->tryLockFor(remainingMs())` (`:641`). Both are
`include/RemotePluginBase.h:325-343`, which call `SystemSemaphore::tryAcquire`.
The fixed deadline is therefore the one this path uses.

**(c) Main-thread queries never use periodDeadline.** `waitForReplyWithin` is
declared with the default `QueryReplyTimeout` (`include/RemotePlugin.h:242`; the
constant is `:218`) and defined at `src/core/RemotePlugin.cpp:871`. It builds its
own deadline from `now + _timeout` (`:878`) and calls `receiveWithin` (`:882`);
`periodDeadline()` appears only at `:627` in the whole file. Callers such as
`updateSampleRate()` (`:854`) use the default timeout.

**(d) The socket path uses the same conversion.** The non-`SYNC_WITH_SHM_FIFO`
branch of `receiveFrameBefore` (`src/common/RemotePluginBase.cpp:661-690`) uses the
same `remainingMs()` lambda at `:678` for `waitForMessageReadable`, so the 1 ms
margin covers the socket receive too.

**(e) Why ceil stays.** `remainingMs()` is shared by the FIFO and socket receive
paths. Lowering it would shorten every bounded wait, including the main-thread
query path, and is a separate product decision. The fix instead raises the margin
inside `periodDeadlineForTesting`, which only the processing-period deadline uses.

**(f) Registration.** `tests/upstream-modifications.txt:6022-6023` declares both
`include/RemotePlugin.h` and `src/core/RemotePlugin.cpp` for this cluster with
reasons. The file is 6023 lines and ends with a newline (last byte 10).

## Platform limit

Win32 cannot be built in this worktree. The change is pure `std::chrono`
arithmetic plus `std::max`; `<algorithm>` is already included
(`src/core/RemotePlugin.cpp:32`) and no platform API is touched.

## Test matrix (p1)

The p1 matrix is kept in `logs/test-matrix.md` (run record, HEAD `f880667386`).

| Case | Input | Expected | Where checked |
|---|---|---|---|
| Normal | a count is available | `tryAcquire` succeeds at once, no wait | `SystemSemaphoreTest`, `tryAcquire` contract |
| Failure / deadline | no count | false once the deadline is reached, bounded and monotonic | `SystemSemaphore::tryAcquire`, `src/common/SystemSemaphore.cpp:122-131` |
| Exhaustion | sub-ms periods, usable <= 0 | wait 0 ms, end == now, never past the period end | `periodDeadlineForTesting` with a small budget |
| Reconfiguration | 3 rates (44100/48000/96000) x 6 frame counts (32..1024) x 4 offsets = 72 | end <= start + budget for all 72 | `tests/src/core/RemotePluginAudioPortsTest.cpp:376-415` (33/72 failed at RED) |
| Audio thread | injected `steady_clock` points only | no thread, sleep or clock call in the check | test uses fixed start and offsets, `:374-396` |
| Platforms | POSIX built here | Win32 uses the same integer-ms timeout, unchanged | `src/common/SystemSemaphore.cpp:173-176` |

Failure spread at the RED baseline: 44100 -> 12, 48000 -> 9, 96000 -> 12 (33 total).

## Evidence

Log folder: `/home/kruzzzzy/.config/kads/zene/state/runs/new-p12-267.r1/logs/`.

| Step | Command | Exit | Evidence from the log |
|---|---|---|---|
| p1 baseline build | `cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p12-267 && cmake --build build --target zene RemotePluginAudioPortsTest > /home/kruzzzzy/.config/kads/zene/state/runs/new-p12-267.r1/logs/build-red-head.log 2>&1; echo EXIT=$?` | 0 | `[100%] Built target RemotePluginAudioPortsTest`, `build-red-head.log` |
| p1 RED test | `cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p12-267/build/tests && ctest -R '^RemotePluginAudioPortsTest$' --output-on-failure > /home/kruzzzzy/.config/kads/zene/state/runs/new-p12-267.r1/logs/test-red-head.log 2>&1; echo EXIT=$?` | 8 | `0% tests passed, 1 tests failed out of 1`; 33 `rate/frames ... end ... > period ...` lines out of 72 combos; first: `rate/frames 44100/32, now offset 0 us, wait 1 ms, end 1.000 ms > period 0.726 ms`; `test-red-head.log` |
| p2 GREEN test | `cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p12-267/build/tests && ctest -R '^RemotePluginAudioPortsTest$' --output-on-failure > /home/kruzzzzy/.config/kads/zene/state/runs/new-p12-267.r1/logs/test-green-fix.log 2>&1; echo EXIT=$?` | 0 | `1/1 Test #255: RemotePluginAudioPortsTest ....... Passed 240.92 sec`, `100% tests passed out of 1`; `test-green-fix.log` |
| reverse-apply fix | `cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p12-267 && git diff -- src/core/RemotePlugin.cpp > /home/kruzzzzy/.config/kads/zene/state/runs/new-p12-267.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.config/kads/zene/state/runs/new-p12-267.r1/logs/fix.patch; echo EXIT=$?` | 0 | no separate log; immediately followed by the reversed build (`build-red-reversed.log`) |
| reverse check | `cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p12-267 && git apply --check -R /home/kruzzzzy/.config/kads/zene/state/runs/new-p12-267.r1/logs/fix.patch > /home/kruzzzzy/.config/kads/zene/state/runs/new-p12-267.r1/logs/patch-reverse-check.log 2>&1; echo EXIT=$?` | 0 | empty log; the reverse step is applicable; `patch-reverse-check.log` |
| reversed build | `cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p12-267 && cmake --build build --target zene RemotePluginAudioPortsTest > /home/kruzzzzy/.config/kads/zene/state/runs/new-p12-267.r1/logs/build-red-reversed.log 2>&1; echo EXIT=$?` | 0 | `[100%] Built target RemotePluginAudioPortsTest`; `build-red-reversed.log` |
| reversed RED test | `cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p12-267/build/tests && ctest -R '^RemotePluginAudioPortsTest$' --output-on-failure > /home/kruzzzzy/.config/kads/zene/state/runs/new-p12-267.r1/logs/test-red-reversed.log 2>&1; echo EXIT=$?` | 8 | `0% tests passed, 1 tests failed out of 1`; 33 `rate/frames` lines, same first line `44100/32, now offset 0 us, wait 1 ms, end 1.000 ms > period 0.726 ms`; `test-red-reversed.log` |
| reapply fix | `cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p12-267 && git apply /home/kruzzzzy/.config/kads/zene/state/runs/new-p12-267.r1/logs/fix.patch; echo EXIT=$?` | 0 | no separate log; immediately followed by the restored build (`build-restored.log`) |
| restored build | `cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p12-267 && cmake --build build --target zene RemotePluginAudioPortsTest > /home/kruzzzzy/.config/kads/zene/state/runs/new-p12-267.r1/logs/build-restored.log 2>&1; echo EXIT=$?` | 0 | `[100%] Built target RemotePluginAudioPortsTest`; `build-restored.log` |
| restored GREEN test | `cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p12-267/build/tests && ctest -R '^RemotePluginAudioPortsTest$' --output-on-failure > /home/kruzzzzy/.config/kads/zene/state/runs/new-p12-267.r1/logs/test-restored.log 2>&1; echo EXIT=$?` | 0 | `1/1 Test #255: RemotePluginAudioPortsTest ....... Passed 240.76 sec`, `100% tests passed out of 1`; `test-restored.log` |
| test family | `cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p12-267/build/tests && ctest -R '^(SystemSemaphoreTest|RemotePluginAudioPortsTest|RemotePluginClientE2ETest)$' --output-on-failure > /home/kruzzzzy/.config/kads/zene/state/runs/new-p12-267.r1/logs/test-family.log 2>&1; echo EXIT=$?` | 0 | all three `Passed`; `100% tests passed out of 3`; `test-family.log` |
| feature check | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p12-267 > /home/kruzzzzy/.config/kads/zene/state/runs/new-p12-267.r1/logs/gate-feature.log 2>&1; echo EXIT=$?` | 0 | `RESULT: PASS`, 8 PASS / 0 FAIL (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length); `gate-feature.log` |
| direct slot | `cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p12-267/build/tests && ./RemotePluginAudioPortsTest periodWaitNeverEndsAfterThePeriod > /home/kruzzzzy/.config/kads/zene/state/runs/new-p12-267.r1/logs/test-green-slot.log 2>&1; echo EXIT=$?` | 0 | `PASS : RemotePluginAudioPortsTest::periodWaitNeverEndsAfterThePeriod()`, `Totals: 3 passed, 0 failed, 0 skipped`; `test-green-slot.log` |

## Files committed

- `7711cacb8` contains `src/core/RemotePlugin.cpp` and this report.
- `tests/src/core/RemotePluginAudioPortsTest.cpp`, the seam `include/RemotePlugin.h` and
  `tests/upstream-modifications.txt` were committed earlier in `858a910e07`; the test
  rounding was committed in `f880667386`.
- The follow-up docs commits change only this report.


## Self-check

- **Mutation proof.** Reverse-applying the fix and rebuilding made the test RED again:
  ctest exit 8, 33 of 72 combos failing, first line the same
  `rate/frames 44100/32 ... end 1.000 ms > period 0.726 ms`
  (`test-red-reversed.log`). Reapplying and rebuilding made it GREEN: ctest exit 0,
  1/1 passed (`test-restored.log`). The test fails on the old code and passes on the new code.
- **Determinism.** The check takes `std::chrono::steady_clock::time_point` values as
  parameters (`periodDeadlineForTesting(now, renderStart, budget)`) and builds them from
  fixed offsets (`RemotePluginAudioPortsTest.cpp:387-396`). It calls no sleep, starts no
  thread and reads no clock, so a slow machine cannot change the result.
- **Cause and completeness.** The cause is `budget / 8` being smaller than the ceil-rounded
  1 ms wait for short periods; the fix raises the margin to `max(budget / 8, 1 ms)`. The
  caller audit (a)-(f) traces every audio-thread path to this deadline and confirms the
  main-thread query path uses its own timeout.
- **No collateral damage.** The margin changes only the processing-period deadline.
  Sub-ms periods now wait 0 ms instead of rounding up to 1 ms, which is the intended
  bound. The semaphore, the shared `ceil` conversion, the socket path and the save/load
  format are unchanged; no data format is affected.
- **Classification.** This is FIXED and REAL: the defect reproduced at the baseline,
  the mutation reproduced it after reverse-apply, and the fix removes it. It is not
  ALREADY-FIXED (the old deadline still overshot) and not FALSE-POSITIVE (33/72 injected
  cases failed with a concrete overshoot).
- **Command siblings.** The sibling list (`app.about` through `arrangement.get_state`)
  comes from the pre-existing base..HEAD train changes, not from this cluster: commits
  `7711cacb8` and `2cd1c91ff9` touch only `src/core/RemotePlugin.cpp` and this report,
  not any ControlCommands or ControlReversibilityTable file. The deadline margin change
  therefore affects no command sibling.

## Other findings

- Stash `4b42c0970e` exists in the repository but was not used; the fix was produced
  directly in the worktree.
- The pre-existing `SystemSemaphoreTest` uses wall-clock timing; it is untouched and
  outside this change.
- The full target run had 7 unrelated skips caused by AF_UNIX paths that are too long for
  the socket limit; they are not failures of this cluster.
- Because the timeout is integer milliseconds, a sub-ms period gets a 0 ms wait: the
  deadline is before `now` and `remainingMs()` floors at 0. That is correct for such a
  period and is covered by the exhaustion case.
- The p1/p2 log files begin at the build/ctest output, not the launched argv; the
  reverse-apply and reapply steps left no separate log, so their exit 0 is recorded from
  the p2 run and corroborated by the builds and tests that immediately follow them.
