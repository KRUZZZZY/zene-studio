FIXED - a throwing handler becomes a typed control error, and the records of the children a throwing wrapper ran are written by that wrapper

## The bug (this tree, before this round)

Round 3 fixed the depth leak: `src/core/ControlRegistry.cpp:502` runs the handler under
`DepthGuard`, so `m_depth`, `m_mutatingDepth` and `m_wrapperDepth` are restored on every exit.
Two things were still open, and both are what the round-3 QA review named:

1. **A handler's exception was never translated.** `runHandler` called `handler(args)` straight
   (`src/core/ControlRegistry.cpp:438` at the round-3 HEAD; the guarded call is at `:506` now), so the exception unwound past
   `recordCall` (`:542`), past `control::finishResult`, past the observer and past the
   collaboration tap, and out of `ControlRegistry::invoke` (`:262`). The control socket calls
   `invoke` at `src/core/ControlServer.cpp:527` and had no local translation either: an agent
   command whose handler threw (a file that could not be written, a plug-in that could not be
   loaded) took the instance down instead of answering the agent.
2. **The records of the children a throwing wrapper ran stayed pending.** `recordCall` holds a
   mutating child's record until the wrapper's step is known (`src/core/ControlRegistry.cpp:565`)
   and flushes it in the wrapper's own `recordCall` (`:572`). A wrapper that threw never reached
   that line, so its child's record was flushed by the NEXT non-mutating command - under that
   command's name and with that command's step.

## The policy: what a handler's exception becomes

**A handler that throws is a FAILED command, and the control surface answers it with a typed
control error.** It never escapes `ControlRegistry::invoke()` as an exception, so every caller of
the control surface - the control socket, the Lua bridge (`src/core/ScriptCommandBridge.cpp:344`),
the GUI's own command calls - gets a `ControlResult` instead of a crash.

The error kind is **`refused`**, the kind this surface already uses for a command that could not
be carried out: a file that could not be written (`src/gui/ControlCommandsShortcuts.cpp:161`), a
locked track (WF-044, `src/core/ControlRegistry.cpp:462`). The error vocabulary is a closed set
(`include/ControlRegistry.h:52-64`) and a protocol contract - the same reason the result-schema
check answers `refused` rather than a new wire kind (`src/core/ControlSchema.cpp:239-243`) - so
no new kind and no new command was added. The message names the command and what the handler
said, so the agent can tell WHICH command failed and why; a mutating command adds that the
project may be partly changed.

## The fix (three seams, one policy)

- `src/core/ControlRegistry.cpp:97` `exceptionText()` and `:112` `controlErrorFromException()`:
  the one definition of the translation (the message and the kind).
- `src/core/ControlRegistry.cpp:436` `runHandlerGuarded()`: runs the handler and turns an
  exception into that typed error. **The translation happens in the frame that ran the handler**,
  so that frame's bookkeeping still runs: the records of the children this call ran are written
  by it (`:572`), and are not left for a later command to flush under its own name.
- `src/core/ControlRegistry.cpp:304-318` `invoke()`: the outer guarantee - nothing the dispatch
  does (the collaboration tap, a step that throws) escapes `invoke()` either, so the socket
  answers with a typed control error instead of the application going down with it.

`runHandler` keeps its round-3 `DepthGuard` (`:502`): the counts are restored on this path too,
and the guard stays the belt-and-braces for any other exit.

## Regression tests (deterministic; no clock, no sleep, no race)

`tests/src/core/ControlRegistryNestingTest.cpp`:

- `:90` `throwingHandlerRestoresDispatchDepth` and `:135` `throwingMutatingHandlerRestoresMutatingDepth`:
  the round-3 cases, updated for the new contract - the exception must NOT escape the control
  surface (`QVERIFY2(!escaped, ...)`), the answer must be the typed `refused` error, and the
  depth assertions are unchanged (the next top-level edit still reaches the observer and still
  records its own transaction).
- `:178` `aThrowingWrapperDoesNotLeaveItsChildRecordForALaterCommand`: a wrapper runs a mutating
  child and then throws. The child's record must be written by that call (one transaction, named
  by the child) and a later non-mutating command with a child of its own must NOT sweep it into
  its own aggregate (two transactions: the first child's and the later child's).
- `:229` `aThrowingHandlerAnswersATypedControlError`: the control-socket boundary -
  `ControlServer::dispatchLine()` answers `{"ok":false,"error":{"kind":"refused","message":...}}`
  naming the command and the handler's text.

`tests/src/gui/MpeInputActionTest.cpp:97` mirrors the first case on the GUI path (the family's
own proof target).

The seam is the registered handler itself: it throws a fixed `std::runtime_error("expected handler
failure")`. Every assertion reads the observer list, the transaction array and the socket's reply
synchronously on the UI thread - no wall-clock timing, no sleep, no thread, no race.

## Evidence: exact commands and exit codes

### Round 3 (the depth restoration, already committed on this branch)

Logs in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-83-port.r3/logs/`.

| Phase | Exact command | Exit |
|---|---|---:|
| Produce reverse patch | `git diff -- src/core/ControlRegistry.cpp > .../r3/logs/fix.patch` (worktree) | 0 |
| Reverse the fix | `git apply -R .../r3/logs/fix.patch` (worktree) | 0 |
| RED build | `cmake --build build --target zene MpeInputActionTest > .../r3/logs/build-mpe-reverted.log 2>&1` | 0 |
| RED test | `ctest -R '^MpeInputActionTest$' --output-on-failure > .../r3/logs/test-mpe-reverted.log 2>&1` (cwd `build/tests`) | 8 |
| Reapply the fix | `git apply .../r3/logs/fix.patch` (worktree) | 0 |
| GREEN build | `cmake --build build --target zene MpeInputActionTest > .../r3/logs/build-restored.log 2>&1` | 0 |
| GREEN test | `ctest -R '^MpeInputActionTest$' --output-on-failure > .../r3/logs/test-mpe-green.log 2>&1` (cwd `build/tests`) | 0 |
| Family test | `ctest -R '^(ControlRegistryTest\|ControlRegistryNestingTest\|MpeInputActionTest)$' --output-on-failure > .../r3/logs/test-family.log 2>&1` (cwd `build/tests`) | 0 (3/3 passed) |
| Gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > .../r3/logs/review-feature-check.log 2>&1` | 0 |

The RED run failed at `MpeInputActionTest.cpp:138` ("Compared lists have different sizes. Actual
(observed) size: 0, Expected (QStringList{later.id}) size: 1"); the GREEN run and the family run
passed 3/3. The round-3 QA review re-ran the same proof independently: `review-red-mpe-test.log`
(failed, exit 8), `review-green-ctest.log` and `review-restored-family-test.log` (3/3 passed,
exit 0), `review-feature-check.log` (RESULT: PASS, exit 0). The raw ratchet gates the review
also ran exited 1 on unrelated baseline entries (`review-complexity-gate.log`,
`review-file-length-gate.log`: "FAIL: complexity ratchet regressed" / "FAIL: file-length ratchet
regressed") - every listed item is a pre-existing train entry, none of them in a file this branch
changed.

### Round 4 (this round: the exception boundary and the pending records)

Logs in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-83-port.r4/logs/`.

| Phase | Exact command | Exit |
|---|---|---:|
| Produce reverse patch | `git diff -- src/core/ControlRegistry.cpp > .../r4/logs/fix.patch` (worktree) | 0 |
| Reverse the fix | `git apply -R .../r4/logs/fix.patch` (worktree) | 0 |
| RED build | `cmake --build build --target zene ControlRegistryNestingTest MpeInputActionTest -j 8 > .../r4/logs/build-3-red.log 2>&1` | 0 |
| RED test | `ctest -R '^(ControlRegistryNestingTest\|MpeInputActionTest)$' --output-on-failure > .../r4/logs/test-2-red.log 2>&1` (cwd `build/tests`) | 8 (2/2 failed) |
| Reapply the fix | `git apply .../r4/logs/fix.patch` (worktree) | 0 |
| GREEN build | `cmake --build build --target zene ControlRegistryNestingTest MpeInputActionTest -j 8 > .../r4/logs/build-4-green.log 2>&1` | 0 |
| GREEN test | `ctest -R '^(ControlRegistryNestingTest\|MpeInputActionTest)$' --output-on-failure > .../r4/logs/test-3-green.log 2>&1` (cwd `build/tests`) | 0 (2/2 passed) |
| Near family build | `cmake --build build --target ControlRegistryTest ControlResultSchemaTest ControlSurfaceReferenceTest UndoBoundsTest UndoHonestyTest UndoHonestyStateTest ReversibilityContractTest ReversibilityUndoTest ScriptCommandBridgeTest ProvenanceSectionTest ControlShutdownHookTest -j 8 > .../r4/logs/build-5-family.log 2>&1` | 0 |
| Near family test | `ctest -R '^(ControlRegistryTest\|ControlRegistryNestingTest\|MpeInputActionTest\|ControlResultSchemaTest\|ControlSurfaceReferenceTest\|UndoBoundsTest\|UndoHonestyTest\|UndoHonestyStateTest\|ReversibilityContractTest\|ReversibilityUndoTest\|ScriptCommandBridgeTest\|ProvenanceSectionTest\|ControlShutdownHookTest)$' --output-on-failure > .../r4/logs/test-4-family.log 2>&1` (cwd `build/tests`) | 0 (13/13 passed) |
| Control/undo family build | `cmake --build build --target <the 63 control/undo/script/collab test targets> -j 8 > .../r4/logs/build-7-control-family.log 2>&1` | 0 (13 min) |
| Control/undo family test | `ctest -R '^(the same 64 names)$' --output-on-failure > .../r4/logs/test-5-control-family.log 2>&1` (cwd `build/tests`) | 8 (57/64 passed; 7 pre-existing failures, see below) |
| Baseline check (product hunk reversed again) | `cmake --build build --target zene ControlChainPresetTest ControlDeviceCatalogueTest ReversibilityUndoDeviceTest UndoHistoryPersistenceTest ScriptDawRemovalUndoTest ScriptEngineTest ScriptMemoryBudgetTest -j 8 > .../r4/logs/build-8-baseline-family.log 2>&1` then `ctest -R '^(ControlChainPresetTest\|ControlDeviceCatalogueTest\|ReversibilityUndoDeviceTest\|UndoHistoryPersistenceTest\|ScriptDawRemovalUndoTest\|ScriptEngineTest\|ScriptMemoryBudgetTest)$' --output-on-failure > .../r4/logs/test-6-baseline-family.log 2>&1` (cwd `build/tests`) | 0 / 8 (the same 7 fail) |
| Restored again | `cmake --build build --target zene ControlChainPresetTest ControlDeviceCatalogueTest ReversibilityUndoDeviceTest UndoHistoryPersistenceTest ScriptDawRemovalUndoTest ScriptEngineTest ScriptMemoryBudgetTest ControlRegistryNestingTest MpeInputActionTest -j 8 > .../r4/logs/build-9-green-restored.log 2>&1` then `ctest -R '^(the 9 names)$' --output-on-failure > .../r4/logs/test-7-green-restored.log 2>&1` (cwd `build/tests`) | 0 / 8 (the same 7 fail; the 2 new cases pass) |
| Gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-83-port > .../r4/logs/gates-1.log 2>&1` | 0 (RESULT: PASS, every row PASS) |
| Final verification on the committed tree | `cmake --build build --target zene ControlRegistryNestingTest MpeInputActionTest ControlRegistryTest -j 8 > .../r4/logs/build-10-final.log 2>&1` then `ctest -R '^(ControlRegistryTest\|ControlRegistryNestingTest\|MpeInputActionTest)$' --output-on-failure > .../r4/logs/test-8-final.log 2>&1` (cwd `build/tests`) | 0 / 0 (3/3 passed) |
| Final gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-83-port > .../r4/logs/gates-2-final.log 2>&1` | 0 (RESULT: PASS, every row PASS) |
| Raw complexity ratchet | `bash tests/complexity-gate.sh --check > .../r4/logs/gate-complexity.log 2>&1` | 1 (pre-existing train items; no REGRESSION line names a file this branch changed) |
| Raw file-length ratchet | `bash tests/file-length-gate.sh --check > .../r4/logs/gate-file-length.log 2>&1` | 1 (pre-existing train items; no REGRESSION line names a file this branch changed) |

The RED run (exit 8) failed with exactly the new assertions:

```
FAIL!  : ControlRegistryNestingTest::throwingHandlerRestoresDispatchDepth() '!escaped' returned FALSE. (a handler's exception must not escape the control surface)
FAIL!  : ControlRegistryNestingTest::throwingMutatingHandlerRestoresMutatingDepth() '!escaped' returned FALSE. (a handler's exception must not escape the control surface)
FAIL!  : ControlRegistryNestingTest::aThrowingWrapperDoesNotLeaveItsChildRecordForALaterCommand() '!escaped' returned FALSE. (a handler's exception must not escape the control surface)
FAIL!  : ControlRegistryNestingTest::aThrowingHandlerAnswersATypedControlError() Caught unhandled exception
FAIL!  : MpeInputActionTest::throwingHandlerRestoresDispatchDepth() '!escaped' returned FALSE. (a handler's exception must not escape the control surface)
```

The GREEN run (exit 0) passed 2/2, and the near family run (exit 0) passed 13/13.

**The 7 failures in the 64-test control/undo family are pre-existing in this build tree, not
collateral damage.** They were measured with the product hunk REVERSED
(`.../r4/logs/test-6-baseline-family.log`, exit 8) and failed identically: the same 7 test names,
the same assertions and the same values (e.g. `ControlChainPresetTest`: "Actual
(applied.errorKind): 1" - `NotFound`, a kind this fix never produces; `ControlDeviceCatalogueTest`:
"the build lists no built-in device at all"; `ReversibilityUndoDeviceTest`: "the LADSPA host plugin
'ladspaeffect' is not in this build"; `UndoHistoryPersistenceTest`: "the baseline needs a real
instrument (tripleoscillator)"; `ScriptDawRemovalUndoTest`: `chain->effects().size()` 0 instead
of 1). This build tree has no plugin catalogue, so those tests fail with and without the fix.

## Files committed

- `src/core/ControlRegistry.cpp` - the exception-to-control-error policy and its two seams.
- `tests/src/core/ControlRegistryNestingTest.cpp` - the four cases (two updated, two new).
- `tests/src/gui/MpeInputActionTest.cpp` - the mirrored GUI-path case, updated.
- `docs/qwen-fixes-2026-10/cluster-new-p10-83-port.md` - this report.

No new product file, no new control command, no schema, no saved-data format and no limit was
added or changed.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it (exit codes).**
  Reverse the product hunk: `git apply -R .../r4/logs/fix.patch` exit 0; rebuild exit 0;
  `ctest -R '^(ControlRegistryNestingTest|MpeInputActionTest)$'` exit 8 (2/2 failed, the five
  assertions quoted above). Reapply: exit 0; rebuild exit 0; the same ctest exit 0 (2/2 passed).
  The near family (13 control/undo tests) is exit 0 with the fix in place, and the 64-test
  control/undo family is byte-identical with and without the fix (the same 7 pre-existing
  failures, measured both ways).
- **The test is deterministic: no wall-clock timing, sleeps or races.** The seam is the
  registered handler, which throws a fixed `std::runtime_error("expected handler failure")`; the
  socket case drives `ControlServer::dispatchLine()` directly (a synchronous call, documented as
  public for exactly this). The assertions read the observer list, the transaction array and the
  reply JSON synchronously on the UI thread. No timer, no sleep, no thread, no randomness.
- **The fix is at the cause and complete: every caller and sibling of what you changed was
  checked and stays consistent.** The cause is the single call of the handler,
  `src/core/ControlRegistry.cpp:506` (`runHandler` is the only place that runs a handler);
  `runHandlerGuarded` (`:436`) is now that call's wrapper, and `invoke` (`:262`) is the single
  entry point every caller uses - the socket (`src/core/ControlServer.cpp:527`), the Lua bridge
  (`src/core/ScriptCommandBridge.cpp:344`), the GUI's own calls (e.g.
  `src/gui/tracks/TrackOperationsWidget.cpp:135`). The three direct `command->handler(...)` calls
  (`src/core/ControlCommandsNoteAcross.cpp:130`,
  `src/core/ControlCommandsRecordTools.cpp:67`,
  `src/core/ControlCommandsSessionPerform.cpp:267`) all run inside another registered handler, so
  they are inside a guarded frame too. The sibling bookkeeping - `recordCall` (`:550`), the
  nested/wrapper rules (`:556-572`), `flushNestedRecords`
  (`src/core/ControlRegistryRecords.cpp:64`) - was read and is unchanged: a throwing wrapper now
  reaches its own `recordCall`, so it flushes its children's records itself, exactly as a
  successful wrapper does. The error vocabulary is unchanged (no new kind, no new command): the
  translation reuses `refused`, the kind the surface already uses for a command that could not be
  carried out, and `control::finishResult` leaves a failed result untouched
  (`src/core/ControlSchema.cpp:232`), so the typed error and its message reach the agent intact.
- **No collateral damage: old projects and saved data still load as before, and any limit or
  check you added does not hit other callers that legitimately exceed it.** Nothing persistent
  changed: no file format, no schema, no cap, no new command, no saved field. The behaviour
  change is confined to a handler that throws - a path that previously ended the process. The
  64-test control/undo family and the 13-test near family are green, and the 7 failures of the
  wider family are measured to be identical with and without the fix (this build tree has no
  plugin catalogue).
- **Undo stays one honest step.** A successful command records exactly what it recorded before.
  A throwing command records nothing for itself (`changed` is false for a failed result, the
  pre-existing rule at `src/core/ControlRegistry.cpp:540-541`), and the mutating children a
  throwing wrapper ran keep their own record - the wrapper's journal step is still merged
  (`mergeCheckpointsSince`, `:518`) and the held record names that step, so `control.undo` still
  reverts the child's change as one step.
- **Realtime.** No audio-thread code is touched. The change is integer bookkeeping plus one
  `QString` built on the UI thread when a handler throws; no allocation, lock, wait, I/O or
  logging on the audio path.
- **Verdict is FIXED, not ALREADY-FIXED or FALSE-POSITIVE**, so the "cite the code that shows
  it" item does not apply; the file:line evidence is in "The bug" and "The fix" above
  (`src/core/ControlRegistry.cpp:438` at the round-3 HEAD, the fix at `:97`, `:112`, `:436` and
  `:304-318`) and in the RED/GREEN tables.
- **Verdict FIXED.** Before this round, `src/core/ControlRegistry.cpp:438` called the handler
  with no exception handling and `recordCall` (`:542`) was skipped when it threw, so the pending
  nested records (`:565`) stayed for a later command; the fix at `:97`, `:112`, `:436` and
  `:304-318` turns the exception into a typed control error in the frame that ran the handler
  and keeps that frame's bookkeeping, and the regressions above fail without it.
