FIXED

# Cluster new-p14-305: audio callback can drain an unbounded number of commands

## Verdict

REAL, and fixed. `SessionScheduler::drainCommands()` popped until the command queue was empty, with no bound per audio period. The queue is a single-producer / single-consumer ring, so the model thread can keep refilling freed slots while the audio thread drains, and the audio callback then stays in that loop for as long as the producer keeps pushing. The fix bounds each period's drain by a budget equal to the queue capacity (`CommandQueueCapacity`, 256). Commands past the budget stay queued in order and are applied at the next period.

## Evidence (file:line on this branch)

- The unbounded loop was the original `drainCommands` body. Before the fix it was `while( m_queue.pop( command ) )`, at `src/core/SessionScheduler.cpp:302-305` on the base commit.
- The queue is a fixed SPSC ring with capacity 256: `include/SessionScheduler.h:224` (`CommandQueueCapacity = 256`), `include/SessionScheduler.h:448` (storage), and the `CommandQueue` class declared in the private section.
- The producer side is `enqueue()`: `src/core/SessionScheduler.cpp:235-243`.
- The only caller of `drainCommands` is `processAudio()`. After the fix it passes the budget: `src/core/SessionScheduler.cpp:437` (`drainCommands( ctx, CommandQueueCapacity )`).
- The fixed drain loop: `src/core/SessionScheduler.cpp:302-313`. The loop condition is `drained < budget && m_queue.pop( command )`, and the per-command body moved unchanged into `applyCommand()` (`src/core/SessionScheduler.cpp:316-363`), with each `continue` becoming `return`.
- The declarations: `include/SessionScheduler.h:485-488`. The friend seam for the test is `include/SessionScheduler.h:41-42` (forward declaration) and `:394-395` (friend under `#ifdef LMMS_TESTING`, the same pattern as `AudioPortsModel.h`).
- The regression test: `tests/src/core/SessionSchedulerTest.cpp:494` (`aDrainStopsAtItsBudgetAndLeavesTheRestQueued`).

Why a seam is needed: a single thread cannot make the queue grow during a drain, so through the public API a bounded drain and an unbounded drain behave identically. The bug only shows when the producer runs concurrently. The test therefore drives the mechanism directly through the friend seam: it queues 10 commands, drains with budget 3, checks that exactly 3 were applied and 7 remain, then drains again and checks the rest are applied. The no-loss property across two real threads is already covered by the existing `commandsCrossThreadsWithoutLoss` test, which is unchanged.

## Decisions I made

- Budget = `CommandQueueCapacity`, not a smaller number. A legitimate caller never has more than the capacity queued at once, so the budget does not defer commands in normal use. Only a producer that refills during a single period is affected, and then the excess waits one period.
- The loop body moved into `applyCommand()` so that `drainCommands()` stays under the complexity target. My first version kept the body inline and the complexity ratchet flagged `drainCommands` at CCN 11 (`feature_check` complexity table, see gate logs below). The move changes no behaviour: every `continue` became a `return` from the same position.
- All three edited files are already fork-owned: `tests/fork-sources.txt` lines 1052 (`include/SessionScheduler.h`), 1833 (`src/core/SessionScheduler.cpp`), 2451 (`tests/src/core/SessionSchedulerTest.cpp`). No `upstream-modifications.txt` entry is needed. No new files, no new control commands, no changes to `ControlCommandsSnapshot`.
- Realtime: the new code adds no allocation, lock, log or wait on the audio thread. `budget` is passed by value and the counter is a local.
- Undo: not touched.

## RED / GREEN / proof (exact commands and exit codes)

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-305.r1/logs/`. Build commands were run with `CCACHE_TEMPDIR=/tmp/ccache-tmp-p14` because the default ccache temp dir under `/run/user` is not writable from this session.

| Step | Command | EXIT | Result | Log |
| --- | --- | --- | --- | --- |
| RED-0 (base code, test added) | `cmake --build build --target SessionSchedulerTest -j4` | 2 | Compile error: `no matching function for call to ...drainCommands(SessionClockContext, const std::size_t&)` at `tests/src/core/SessionSchedulerTest.cpp:505`. The test cannot be built against the base code (no budget seam). The first attempt failed on the ccache temp dir (build-1), not on the code. | build-2-red0-headcompile.log |
| RED-1 (seam added, loop still unbounded) | `cmake --build build --target SessionSchedulerTest zene -j4` then `ctest -R '^SessionSchedulerTest$' --output-on-failure` from `build/tests` | build 0, **ctest 8** | `FAIL! ... aDrainStopsAtItsBudgetAndLeavesTheRestQueued() 'scheduler.processedCommands() == kBudget' returned FALSE. (one period consumed 10 commands, budget is 3)`. Totals: 23 passed, 1 failed. | build-3-red1-seamonly.log, test-1-red1-seamonly.log |
| GREEN (loop bounded) | rebuild the same targets, then ctest | build 0, ctest 0 | `100% tests passed, 1/1` (`SessionSchedulerTest`). | build-4-green.log, test-2-green.log |
| 4b proof on the final code (bound removed, seam kept) | rebuild, then ctest | build 0, **ctest 8** | Same `FAIL!` line as RED-1. | build-10-4b-final-reverted-while.log, test-8-4b-final-reverted-while.log |
| 4b restore (bound back) | rebuild, then ctest | build 0, ctest 0 | Pass. | build-11-final-green.log, test-9-final-green-family.log |

Two earlier attempts at step 4b are invalid and are not used as evidence:

- build-9 (`for( std::size_t drained = 0; m_queue.pop( command ); ++drained )`) failed to compile with `-Werror=unused-but-set-variable`, so `ctest` then ran the stale GREEN binary and reported a pass (test-7). I discarded that result. The variant was changed to the plain `while( m_queue.pop( command ) )` form, which gives the valid FAIL above.
- The proof is a revert of the bound only. The protocol's full `git apply -R` of the whole diff would also remove the test seam, and the test then fails by not compiling. That is the RED-0 state, so the seam-only and bound-only checks are the ones that show the assertion.

Affected test family (`ctest` from `build/tests`, final code):

| Command | EXIT | Result |
| --- | --- | --- |
| `cmake --build build --target SessionSchedulerRenderTest SessionGridRecordTest zene -j4` (after the header change) | 0 | build clean |
| `ctest -R '^(SessionSchedulerTest\|SessionSchedulerRenderTest\|SessionGridRecordTest)$' --output-on-failure` | 0 | 3/3 passed (`SessionGridRecordTest` #425, `SessionSchedulerTest` #473, `SessionSchedulerRenderTest` #474) |

Tests executed: 3 ctest entries in the family, and `SessionSchedulerTest` contains 24 test functions (23 pre-existing plus the new one; counted in the RED-1 totals). Zero tests is not the case here.

## Gates

`feature_check.py` could not run to completion in this session. It writes its per-gate output to `<worktree>/../zene-qwen-new-p14-305-feature-check/`, which is outside the write-jail. It stopped with `PermissionError` before running any check (`gate-1-feature-check.log`, `EXIT=1`). I did not change the script. Instead I ran each gate command it lists directly from the worktree, on the final code (logs `gate-r2-10` … `gate-r2-17`):

| Gate | Command (from worktree) | EXIT | Note |
| --- | --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 | PASS |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 | REPRODUCES |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 | PASS |
| check-namespace | `tests/scripted/check-namespace` | 0 | 0 errors |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 | PASS |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 | PASS (re-run after this report was written, see Self-check) |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 | The regression list no longer contains `drainCommands` or `applyCommand`. The seven items it lists are in files this branch does not touch (for example `tests/src/gui/XyPadPairTest.cpp`, `src/core/ProjectRevisions.cpp`). |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 | All 15 items are new files in other lanes' areas (for example `src/core/TakeLane.cpp (575)`). None is one of my three files. |

The ratchets are red on the train on purpose, per the feature check notes. My changes add no new item to either list: none of the regression lines names `SessionScheduler.h`, `SessionScheduler.cpp` or `SessionSchedulerTest.cpp`. The complexity table (`gate-r2-16-complexity.log:71`) lists `lmms::SessionScheduler::evaluateFollow` at CCN 11 in `src/core/SessionFollow.cpp`, a file this branch does not change, and it is not reported as a regression.

## Self-check

`submit_check.py` (log `submit-check-1.log`, EXIT 1 before this section was written) reported PASS for commits on the branch, a clean working tree, this report, and the tests changed, and FAIL for the gates row. The gates row fails for an environment reason, not a code reason: `submit_check.py` runs `feature_check.py` as a subprocess, and that script creates its log folder at `<worktree>/../<name>-feature-check`, which is outside this session's write-jail, so it stops with `PermissionError` before any check runs (`gate-1-feature-check.log`). I did not change the script.

To check the gates anyway, I ran a copy of `feature_check.py` from the run folder (`feature_check_logdir.py`) with only the log folder moved into the run folder, and kept its comparison logic unchanged. It prints PASS on all eight rows and exits 0 (`gate-r4-feature-check-logdir.log`, `FC_EXIT=0`). The direct runs listed under Gates are the same checks. The ratchet rows pass by the script's own rule: no new item in a file this branch changed.

- **Regression test fails with the change reverted and passes with it.** RED-1: ctest exit 8, `aDrainStopsAtItsBudgetAndLeavesTheRestQueued` fails on `processedCommands() == kBudget` (10 consumed, budget 3). Proof on the final code: bound removed, build exit 0, ctest exit 8 with the same line. Restored: build exit 0, ctest exit 0. Commands and logs are in the table above.
- **Deterministic.** The test uses no sleeps, threads, wall-clock time or races. It injects the budget (3) through the `LMMS_TESTING` friend seam, queues ten launches with `requestLaunch`, and drives the scheduler with a fixed `clock( 0 )`. The concurrent case is covered by the existing `commandsCrossThreadsWithoutLoss`, which is not changed.
- **Fix at the cause, and complete.** The only caller of `drainCommands` is `processAudio` (`src/core/SessionScheduler.cpp:437`), and the only caller of `applyCommand` is the drain loop (`:311`). Both were checked with `rg`. The producer side (`enqueue`, `:235-243`) is unchanged. Commands past the budget stay in the same FIFO ring, so ordering is preserved. The `m_processed`, `m_dropped` and launch counters keep their meanings.
- **No collateral damage.** No save format, no control command and no snapshot changed. The budget is `CommandQueueCapacity`, so a legitimate caller cannot exceed it within one period in normal use. Only a producer that refills the queue while the audio thread is still draining sees commands deferred, and only by one period; quantised launches still land on their grid line unless the backlog itself crosses it.
- **Verdict is FIXED,** not ALREADY-FIXED or FALSE-POSITIVE, so no "already fixed" evidence is needed beyond the file:line references above.
