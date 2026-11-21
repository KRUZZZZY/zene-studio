FIXED

# Cluster new-p20-31: a throwing command observer cannot change a committed reply

## Verdict evidence

- `src/core/ControlRegistry.cpp:527-531` (in `runHandler`, line 441 onward): the edit is recorded at 528 (`recordCall`) and its result is finished at 529 before `notifyObserver` runs at 530. The collab tap `collab::CommandTap::observed` runs at 531.
- `include/ControlRegistry.h:315-325`: `notifyObserver` now wraps `m_observer(commandId, args)` in `try { ... } catch (...) { }`. This is the single definition both callers use.
- Before the fix, an exception from the observer escaped `runHandler` into `dispatch`. `src/core/ControlRegistry.cpp:282-296` catches it there and returns `controlErrorFromException(...)`. `src/core/ControlRegistry.cpp:77-83` turns that into `ControlErrorKind::Refused` with the text "command '<id>' failed: <what>" and, for a mutating command, "(the project may be partly changed)". So a committed edit was reported to the caller as a refused command.
- Second caller: `src/core/ControlTransactions.cpp:122` calls `notifyObserver(commandId, args, true, true);` for a completed async edit. It is an unqualified call, so it reaches the same guarded definition. I did not separately confirm the enclosing member function beyond that call.

## Scope of the fix

- Diff in the worktree: `include/ControlRegistry.h` (9 lines changed: the try/catch and a comment). No other source file changed.
- No save/load, project schema, undo-step, or command-id change.
- `notifyObserver` is documented as UI-thread (`m_depth`, "UI thread"). Nothing is added on the audio thread.

## File-length decision (corrected to what this tree shows)

The packet's account does not match this worktree, so the facts below come from the tree:

- `tests/src/core/ControlRegistryNestingTest.cpp` is **unchanged** in this branch. `git diff HEAD` on it is empty. It is 622 lines, and 498 of them are code lines (non-blank, not comment).
- The two observer tests (`observerExceptionDoesNotChangeACommittedReply`, `observerExceptionDoesNotChangeACompletedAsyncEdit`) exist only in the new file `tests/src/core/ControlRegistryObserverTest.cpp`. That file is 181 lines, 130 of them code lines. I did not find them in the nesting file, so this report does not say they were moved out of it.
- No assertion was removed from the nesting file, because it has no diff.
- The new file is registered in `tests/CMakeLists.txt`, `tests/fork-sources.txt`, `tests/all-sources.txt`, and `tests/upstream-modifications.txt`.

## Test matrix

| Case | Covered by |
|---|---|
| normal (committed edit answered ok) | `observerExceptionDoesNotChangeACommittedReply` |
| failure (observer throws) | both tests install an observer that throws |
| reconfiguration (observer replaced / cleared) | observer installed and cleared with `setCommandObserver` in both tests |
| completed async edit (second caller) | `observerExceptionDoesNotChangeACompletedAsyncEdit` |
| exhaustion | N/A: no journal step is added and the transaction caps are untouched |
| audio thread | N/A: dispatch is UI-thread |
| platform | pure C++/Qt, no platform code |

## RED / GREEN

The log files record only the output and the exit line. They do not record the command lines. Each row below names the log file and its EXIT line, as seen in this session under `/home/kruzzzzy/.config/kads/zene/state/runs/new-p20-31.r1/logs/`.

| Step | Log | EXIT | Result |
|---|---|---|---|
| RED build (test binary built against the unguarded header) | build-9-red.log | 0 | links `ControlRegistryObserverTest` |
| RED run | test-14-red.log | 2 | Totals: 2 passed, 2 failed. FAIL `observerExceptionDoesNotChangeACommittedReply`: `'answer.ok' returned FALSE` (ObserverTest.cpp:121). FAIL `observerExceptionDoesNotChangeACompletedAsyncEdit`: `'!escaped' returned FALSE` (ObserverTest.cpp:171). |
| apply fix | apply-5.log, apply-6.log | 0, 0 | log contains only the EXIT line |
| GREEN build | build-10-green.log | 0 | builds |
| GREEN run | test-16-green.log | 0 | Totals: 4 passed, 0 failed (initTestCase/cleanupTestCase plus the two tests) |
| second GREEN run | test-12.log | 0 | Totals: 4 passed, 0 failed |
| ctest | test-13.log | 0 | 100% tests passed, out of 4; `ControlRegistryNestingTest` passed |
| feature check (pre-commit) | gates-2.log | 0 | RESULT: PASS (base fa4a5f2e7, 6 changed files) |

History only: the earlier nesting-file cycle logs (`build-1` to `build-8`, `test-1` to `test-11`) are in the same folder. I did not audit them for this report.

## Other findings (out of this finding's scope)

- `src/core/CollabCommandTap.cpp:67` (`CommandTap::observed`) is called unguarded from `src/core/ControlRegistry.cpp:531`, right after the guarded observer call. If it throws, the exception still escapes `runHandler`. I read only its signature. "Unguarded" is the packet's claim and is not confirmed by a body read in this session.
- `src/core/ControlEventHub.cpp:132` (`ControlEventHub::commandFinished`): I read only its signature and did not check whether it can throw. Not verified.

## Self-check

1. **Regression fails reversed, passes restored.** Reversed (unguarded): test-14-red.log, EXIT=2, two FAILs at ObserverTest.cpp:121 and :171. Restored (guarded): test-16-green.log EXIT=0 and test-12.log EXIT=0, both 4 passed.
2. **Determinism.** Both tests install a plain observer function (`setCommandObserver(...)` at ObserverTest.cpp:103 and :129) and clear it on exit. The file has 0 matches for sleep, timer, wait, or qWait.
3. **Cause and complete.** One guarded definition (`include/ControlRegistry.h:315-325`) covers both callers: `ControlRegistry.cpp:530` and `ControlTransactions.cpp:122`. Peers inspected: `CollabCommandTap.cpp:67` and `ControlEventHub.cpp:132`, both listed as other findings above.
4. **Collateral.** No save/load, schema, undo-step, or command-id change. The diff is the header guard and the new test file. Nothing runs on the audio thread.
5. **Verdict.** FIXED. Evidence: `ControlRegistry.cpp:530` (the observer call), `include/ControlRegistry.h:315-325` (the guard), `ControlRegistry.cpp:289-296` and `:77-83` (what an escaping exception used to become).
