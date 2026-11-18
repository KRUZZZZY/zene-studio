# Cluster new-p8-75

**Verdict: FIXED**

The event filter previously installed through a queued callback, while `CueFilter::eventFilter` prepared controls only on `QEvent::Polish` (`src/gui/widgets/ControlCues.cpp:76-86`). A control shown before the queued installation received its polish event too early. `startCues` now calls `install()` synchronously from the `Q_COREAPP_STARTUP_FUNCTION` hook (`src/gui/widgets/ControlCues.cpp:103-109`), before application code creates and shows controls.

The regression at `tests/src/gui/KnobKeyboardTest.cpp:86-97` shows a knob before processing the event loop and checks its hover attribute and interaction help.

## Evidence

Build commands ran from the worktree root. CTest commands ran from `build/tests`.

| Stage | Exact command | Exit | Result |
| --- | --- | ---: | --- |
| Original RED build | `cmake --build build --target KnobKeyboardTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-75.r1/logs/build-1.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | Test target built before the fix. |
| Original RED test | `ctest -R '^KnobKeyboardTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-75.r1/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc` | 8 | Regression failed at `WA_Hover`; 15 QtTest cases passed and 1 failed. |
| Fixed GREEN build | `cmake --build build --target zene KnobKeyboardTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-75.r1/logs/build-2.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | Rebuilt app and affected test target. |
| Fixed GREEN test | `ctest -R '^KnobKeyboardTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-75.r1/logs/test-2.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | `KnobKeyboardTest` passed, 1/1 CTest case. |
| Remove fix for proof | `git diff -- src/gui/widgets/ControlCues.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-75.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-75.r1/logs/fix.patch; rc=$?; echo EXIT=$rc` | 0 | Applied the reverse patch. |
| Proof RED rebuild | `cmake --build build --target zene KnobKeyboardTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-75.r1/logs/build-3.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | Rebuilt with the production fix removed. |
| Proof RED test | `ctest -R '^KnobKeyboardTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-75.r1/logs/test-3.log 2>&1; rc=$?; echo EXIT=$rc` | 8 | Same `WA_Hover` assertion failed; 15 passed and 1 failed. |
| Restore fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-75.r1/logs/fix.patch; rc=$?; echo EXIT=$rc` | 0 | Restored the exact production patch. |
| Restored GREEN build | `cmake --build build --target zene KnobKeyboardTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-75.r1/logs/build-4.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | Rebuilt app and test with fix restored. |
| Restored GREEN test | `ctest -R '^KnobKeyboardTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-75.r1/logs/test-4.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | `KnobKeyboardTest` passed, 1/1 CTest case. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-75.r1/logs/gate-1.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | PASS; check mode did not write the baseline. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-75.r1/logs/gate-2.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | PASS; check mode did not write the baseline. |

Affected-family count: one CTest case (`KnobKeyboardTest`) executed per run. Both GREEN runs passed 1/1; the RED runs each had 15 QtTest cases pass and the new regression fail.

## Files in the commit

- `src/gui/widgets/ControlCues.cpp`
- `tests/src/gui/KnobKeyboardTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-75.md`
