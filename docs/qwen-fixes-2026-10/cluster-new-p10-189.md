# Cluster new-p10-189: Float LCD spin box ignores inverted wheel direction

**Verdict: FIXED**

The finding was real. `LcdFloatSpinBox::wheelEvent` in `src/gui/widgets/LcdFloatSpinBox.cpp:202` chose a direction from the angle delta sign and ignored `QWheelEvent::inverted()`. The handler now multiplies the delta sign by `-1` for inverted events, matching `LcdSpinBox`. The regression at `tests/src/gui/KnobKeyboardTest.cpp:124` checks an ordinary positive delta increments and an inverted positive delta decrements.

## Evidence

| Stage | Exact command | Exit | Result |
|---|---|---:|---|
| RED, original handler | `ctest -R '^KnobKeyboardTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-189.r1/logs/test-1.log 2>&1` | 8 | 1 CTest test ran; the regression reported actual `7`, expected `5`. |
| GREEN, fixed handler | `ctest -R '^KnobKeyboardTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-189.r1/logs/test-2.log 2>&1` | 0 | 1/1 CTest tests passed. |
| Reverse-patch proof, handler reverted | `ctest -R '^KnobKeyboardTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-189.r1/logs/test-3.log 2>&1` | 8 | After rebuilding with the production patch reversed, the same regression failed again. |
| Reverse-patch proof, patch restored | `ctest -R '^KnobKeyboardTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-189.r1/logs/test-4.log 2>&1` | 0 | After restoring and rebuilding, 1/1 CTest tests passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-189.r1/logs/gate-3.log 2>&1` | 0 | Passed. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-189.r1/logs/gate-4.log 2>&1` | 0 | Passed. |

The affected test family is `KnobKeyboardTest`: CTest executed 1 test in each run. Each build used `cmake --build build --target zene KnobKeyboardTest`; builds 1 through 4 all exited 0. Reverse proof commands `git diff -- src/gui/widgets/LcdFloatSpinBox.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-189.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-189.r1/logs/fix.patch` and `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-189.r1/logs/fix.patch` both exited 0.

Build commands (all `EXIT=0`):

- `cmake --build build --target zene KnobKeyboardTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-189.r1/logs/build-1.log 2>&1`
- `cmake --build build --target zene KnobKeyboardTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-189.r1/logs/build-2.log 2>&1`
- `cmake --build build --target zene KnobKeyboardTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-189.r1/logs/build-3.log 2>&1`
- `cmake --build build --target zene KnobKeyboardTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-189.r1/logs/build-4.log 2>&1`

The requested `fff grep` helper was not exposed in this environment. I searched the build, test, and gate logs with `rg -n -i` for `error`, `FAILED`, and `Assertion` instead.

## Files committed

- `src/gui/widgets/LcdFloatSpinBox.cpp`
- `tests/src/gui/KnobKeyboardTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-189.md`
