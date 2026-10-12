# Cluster new-p12-36: No-op setters create undo entries

**Verdict: FIXED**

The selector, macro set, and macro-pair handlers now record undo state only when the rack selection, macro scalar, or a resolved parameter value changed. They omit the internal transaction payload for a true no-op, so the registry does not record a reversible transaction without a journal step. Equal parameter writes no longer leave model checkpoints behind.

## Source evidence

- `src/core/ControlCommandsRack.cpp:343-369` compares the rack selection before and after the setter; it records an undo action and transaction payload only when the selection changed.
- `include/ControlRackMacroWrites.h:48-49` declares the shared change check; `src/core/ControlCommandsRackMacroPair.cpp:73-82` compares the macro scalar and each captured parameter value.
- `src/core/ControlCommandsRackMacros.cpp:430-465` records undo metadata for `rack.macro_set` only when that shared check reports a change.
- `src/core/ControlCommandsRackMacroPair.cpp:124-140` records one pair step only when either macro or one of its parameter writes changed.
- `src/core/RackMacros.cpp:225-239` avoids a model checkpoint and write when the current parameter already equals the requested value; the write remains represented in the command result.
- `tests/src/core/RackMacrosTest.cpp:407-505` covers repeated selector, macro, and pair values, plus a same-scalar macro call that must still undo a changed parameter.

The finding's layer-mix location is not present in this tree: `src/core/ControlCommandsRackLayers.cpp` ends at line 384 and registers layers, layer add/remove, and zone add/remove at lines 375-382. It has no layer-mix setter to change.

## Evidence table

All builds ran from the worktree root. All CTest commands ran from `build/tests`. Build and test logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-36.r1/logs`.

| Run | Exact command | Exit | Result |
|---|---|---:|---|
| Original-code RED build | `cmake --build build --target zene RackMacrosTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-36.r1/logs/build-1.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | Test target built before the production fix. |
| Original-code RED test | `ctest -R '^RackMacrosTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-36.r1/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc` | 8 | 1/1 CTest target ran; QtTest reported 10 passed, 3 failed. The failures showed selection 1 instead of -1, macro 1.0 instead of 0.0, and pair X 0.25 instead of 0.0 after one undo. |
| Intermediate build | `cmake --build build --target zene RackMacrosTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-36.r1/logs/build-2.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | Built the first implementation attempt. |
| Intermediate test | `ctest -R '^RackMacrosTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-36.r1/logs/test-2.log 2>&1; rc=$?; echo EXIT=$rc` | 8 | Exposed the no-step transaction-payload case and remaining macro failures. |
| Intermediate build | `cmake --build build --target zene RackMacrosTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-36.r1/logs/build-3.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | Rebuilt after conditioning transaction payloads. |
| Intermediate test | `ctest -R '^RackMacrosTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-36.r1/logs/test-3.log 2>&1; rc=$?; echo EXIT=$rc` | 8 | Selector regression passed; macro no-op cases exposed equal-value model checkpoints. |
| Final GREEN build | `cmake --build build --target zene RackMacrosTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-36.r1/logs/build-4.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | Rebuilt after suppressing equal-value parameter checkpoints. |
| Final GREEN test | `ctest -R '^RackMacrosTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-36.r1/logs/test-4.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | 1/1 CTest target passed. |
| Remove-fix proof | `git diff -- include/ControlRackMacroWrites.h src/core/ControlCommandsRack.cpp src/core/ControlCommandsRackMacros.cpp src/core/ControlCommandsRackMacroPair.cpp src/core/RackMacros.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-36.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-36.r1/logs/fix.patch; rc=$?; echo EXIT=$rc` | 0 | Reverted product files while leaving the regression test in place. |
| Reverted build | `cmake --build build --target zene RackMacrosTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-36.r1/logs/build-5.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | Rebuilt the unfixed product. |
| Reverted RED test | `ctest -R '^RackMacrosTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-36.r1/logs/test-5.log 2>&1; rc=$?; echo EXIT=$rc` | 8 | Reproduced all three no-op failures (10 passed, 3 failed). |
| Restore-fix proof | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-36.r1/logs/fix.patch; rc=$?; echo EXIT=$rc` | 0 | Reapplied the product patch. |
| Restored build | `cmake --build build --target zene RackMacrosTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-36.r1/logs/build-6.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | Rebuilt the fixed product. |
| Restored GREEN test | `ctest -R '^RackMacrosTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-36.r1/logs/test-6.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | 1/1 CTest target passed after patch reapplication. |
| Existing macro-pair family build | `cmake --build build --target zene UndoHonestyTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-36.r1/logs/build-7.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | Built the existing undo-contract test. |
| Existing macro-pair family test | `ctest -R '^UndoHonestyTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-36.r1/logs/test-7.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | 1/1 CTest target passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-36.r1/logs/gate-1.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | Check mode passed; no baseline changed. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-36.r1/logs/gate-2.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | Check mode passed; no baseline changed. |

The affected regression family is `RackMacrosTest` (one CTest target, 13 QtTest cases including setup and cleanup). The related existing macro-pair family, `UndoHonestyTest`, also passed (one CTest target). No test was run zero times.

No callable fff grep tool was exposed in this session; the numbered logs were searched with `rg` separately for `error`, `FAILED`, `Assertion`, and CTest summaries.

## Files committed

- `include/ControlRackMacroWrites.h`
- `src/core/ControlCommandsRack.cpp`
- `src/core/ControlCommandsRackMacroPair.cpp`
- `src/core/ControlCommandsRackMacros.cpp`
- `src/core/RackMacros.cpp`
- `tests/src/core/RackMacrosTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p12-36.md`
