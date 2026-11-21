FIXED

# Cluster new-p20-31: a throwing command observer cannot change a committed reply

Docs-only follow-up to commit 94390b7c11 (the fix). This is a further docs-only correction after 84e01086e. It corrects the file-length claims and the exact gate commands in this file only. No product or test change.

## Verdict evidence

- `src/core/ControlRegistry.cpp:441` starts `runHandler`. Inside it, the edit is recorded at 528 (`recordCall`), its result is finished at 529 (`control::finishResult`), `notifyObserver` runs at 530, the collab tap `collab::CommandTap::observed` runs at 531, and `control::ControlEventHub::commandFinished` runs at 532.
- `include/ControlRegistry.h:316-326` defines `notifyObserver`, the single definition both callers use. Lines 323-324 are `try { m_observer(commandId, args); }` and `catch (...) { }`.
- Before the fix, an exception from the observer escaped `runHandler` into `dispatch`. `src/core/ControlRegistry.cpp:282-296` catches it there (`catch (const std::exception&)` at 289, `catch (...)` at 293) and returns `controlErrorFromException(...)`. `src/core/ControlRegistry.cpp:77-83` turns that into `ControlErrorKind::Refused` with the text "command '<id>' failed: <what>" and, for a mutating command, "(the project may be partly changed)". So a committed edit was reported to the caller as a refused command.
- Second caller: `src/core/ControlTransactions.cpp:114` starts `ControlRegistry::recordCompletedEdit`. Its call `notifyObserver(commandId, args, true, true);` is at line 122. It is unqualified, so it reaches the same guarded definition.

## Scope of the fix

- Commit 94390b7c11 (`git show --stat`) changes `include/ControlRegistry.h` (9 changed lines), the new test file `tests/src/core/ControlRegistryObserverTest.cpp` (181 lines), and the registration lines in `tests/CMakeLists.txt`, `tests/fork-sources.txt`, `tests/all-sources.txt`, `tests/upstream-modifications.txt`. It also adds this report. No other source file changed.
- No save/load, project schema, undo-step, or command-id change.
- `notifyObserver` is documented as UI-thread (`m_depth`, "UI thread"). Nothing is added on the audio thread.

## File-length history

- The two observer tests (`observerExceptionDoesNotChangeACommittedReply`, `observerExceptionDoesNotChangeACompletedAsyncEdit`) were first added to `tests/src/core/ControlRegistryNestingTest.cpp`.
- `gates-1.log` failed with `FAIL file-length NEW: new file over 500 lines: tests/src/core/ControlRegistryNestingTest.cpp (583)`, base fa4a5f2e7, 3 changed files, `RESULT: FAIL`.
- The split moved the two tests, unchanged, to `tests/src/core/ControlRegistryObserverTest.cpp` (181 lines).
- `tests/src/core/ControlRegistryNestingTest.cpp` was then restored byte-identical to HEAD: `git diff HEAD` on it is empty.
- `gates-2.log` (pre-commit) and `gates-3.log` (post-commit) pass. No assertion was removed.
- Line counts: the new observer test file `tests/src/core/ControlRegistryObserverTest.cpp` is 181 physical lines. The file-length gate passes in `gates-2.log` and `gates-3.log`.

## Registration

- `tests/CMakeLists.txt:313`, `tests/fork-sources.txt:2782`, `tests/all-sources.txt:2926`: the new test file `tests/src/core/ControlRegistryObserverTest.cpp` is registered.
- `tests/upstream-modifications.txt:2225`: the row for `include/ControlRegistry.h` names the two tests as its regression (`ControlRegistryObserverTest::observerExceptionDoesNotChangeACommittedReply` / `...ACompletedAsyncEdit`). It does not register the test file.

## Test matrix

| Case | Covered by |
|---|---|
| normal (committed edit answered ok) | `observerExceptionDoesNotChangeACommittedReply` |
| failure (observer throws) | `observerExceptionDoesNotChangeACommittedReply` (RED: `answer.ok` false); `observerExceptionDoesNotChangeACompletedAsyncEdit` (lambda throws at line 156) |
| reconfiguration (observer installed, replaced, cleared) | test 1: install at 103, replace at 129, clear at 136; test 2: install at 154, cleared by the `ClearObserverOnExit` destructor at 149 |
| completed async edit (second caller) | `observerExceptionDoesNotChangeACompletedAsyncEdit` |
| exhaustion | N/A: no journal step is added and the transaction caps are untouched |
| audio thread | N/A: dispatch is UI-thread |
| platform | pure C++/Qt, no platform code |

## Commands and exits

The logs record only program output and an `EXIT=` line; they do not record command lines. The commands below are the ones this correction records. All logs are in `/home/kruzzzzy/.config/kads/zene/state/runs/new-p20-31.r1/logs/`.

| # | Command | EXIT | Log | Result |
|---|---|---|---|---|
| 1 | `git apply -R /home/kruzzzzy/.config/kads/zene/state/runs/new-p20-31.r1/logs/fix.patch` (RED: reverse the fix) | 0 | apply-5.log | log contains only the EXIT line |
| 2 | `cmake --build build --target zene ControlRegistryObserverTest -j12` | 0 | build-9-red.log | builds |
| 3 | `cd build/tests && ./ControlRegistryObserverTest observerExceptionDoesNotChangeACommittedReply observerExceptionDoesNotChangeACompletedAsyncEdit` | 2 | test-14-red.log | Totals: 2 passed, 2 failed. FAIL `observerExceptionDoesNotChangeACommittedReply`: `'answer.ok' returned FALSE` (ObserverTest.cpp:121). FAIL `observerExceptionDoesNotChangeACompletedAsyncEdit`: `'!escaped' returned FALSE` (ObserverTest.cpp:171). |
| 4 | `git apply /home/kruzzzzy/.config/kads/zene/state/runs/new-p20-31.r1/logs/fix.patch` (GREEN: restore the fix) | 0 | apply-6.log | log contains only the EXIT line |
| 5 | `cmake --build build --target zene ControlRegistryObserverTest -j12` | 0 | build-10-green.log | builds |
| 6 | `cd build/tests && ./ControlRegistryObserverTest observerExceptionDoesNotChangeACommittedReply observerExceptionDoesNotChangeACompletedAsyncEdit` | 0 | test-16-green.log | Totals: 4 passed, 0 failed (initTestCase, cleanupTestCase and the two tests) |
| 7 | (cwd build/tests) `ctest -R "ControlRegistryNestingTest\|ControlRegistryObserverTest\|ControlEventHubTest\|ControlCommandsMacrosTest" --output-on-failure` | 0 | test-13.log | 100% tests passed, out of 4 |
| 8 | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p20-31 > /home/kruzzzzy/.config/kads/zene/state/runs/new-p20-31.r1/logs/gates-2.log 2>&1` (pre-commit) | 0 | gates-2.log | RESULT: PASS (base fa4a5f2e7, 6 changed files) |
| 9 | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p20-31 > /home/kruzzzzy/.config/kads/zene/state/runs/new-p20-31.r1/logs/gates-3.log 2>&1` (post-commit) | 0 | gates-3.log | RESULT: PASS (base fa4a5f2e7, 7 changed files) |

History only: the earlier nesting-file cycle logs (`build-1` to `build-8`, `test-1` to `test-11`) and `test-12.log` are in the same folder. This table does not use them, and I did not audit them for this report.

## Other findings (out of this finding's scope)

- `src/core/ControlRegistry.cpp:531` calls `collab::CommandTap::observed` without a guard. Its definition is `src/core/CollabCommandTap.cpp:67`. I read that definition's signature only; its body was not audited. This finding does not claim it is safe or unsafe. If it throws, the exception would escape `runHandler` the same way the observer exception did.
- `src/core/ControlEventHub.cpp:132` (`ControlEventHub::commandFinished`) is called at `src/core/ControlRegistry.cpp:532`. I read its signature only. I did not check whether it can throw. Not verified.

## Self-check

1. **Regression fails reversed, passes restored.** Reversed (commands 1-3): apply-5.log EXIT=0, build-9-red.log EXIT=0, test-14-red.log EXIT=2 with FAILs at ObserverTest.cpp:121 and :171. Restored (commands 4-6): apply-6.log EXIT=0, build-10-green.log EXIT=0, test-16-green.log EXIT=0 with 4 passed.
2. **Determinism.** The test file has no sleep, timer, wait or qWait calls (grep: no matches). `setCommandObserver` calls in the test file: test 1 (lines 83-143) installs at 103 and 129, clears at 136, and `ClearObserverOnExit` at 88 clears on exit. Test 2 (lines 144-181) installs at 154, and `ClearObserverOnExit` at 149 clears on exit.
3. **Cause and complete.** One guarded definition (`include/ControlRegistry.h:316-326`) covers both callers: `ControlRegistry.cpp:530` and `ControlTransactions.cpp:122`. The peers `CollabCommandTap.cpp:67` and `ControlEventHub.cpp:132` are listed under other findings and are not claimed safe.
4. **Collateral.** No save/load, schema, undo-step, or command-id change. The product diff is the header guard; the rest is the new test file and registration lines. Nothing runs on the audio thread.
5. **Registration.** The test file is registered in `tests/CMakeLists.txt`, `tests/fork-sources.txt` and `tests/all-sources.txt`. `tests/upstream-modifications.txt` has no row for the test file; its row for `include/ControlRegistry.h` only names the two tests as regression.
6. **Verdict.** FIXED. Evidence: `ControlRegistry.cpp:530` (the observer call), `include/ControlRegistry.h:323-324` (the guard), `ControlRegistry.cpp:282-296` and `:77-83` (what an escaping exception used to become).
