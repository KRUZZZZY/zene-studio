# Cluster new-p9-14

**Verdict: FIXED**

`include/ColorChooser.h:58-62` now checks `parentWidget()` before forwarding a key-release event. The regression in `tests/src/gui/KnobKeyboardTest.cpp:135-145` sends a key release to a parentless chooser and fails on Qt warnings or critical messages.

The new test failed against the original handler with SIGSEGV (signal 11). Reversing only the production header patch reproduced the same failure; with the patch restored, it passed.

## Validation evidence

All build commands ran from the worktree root. All CTest commands ran from `build/tests`. Each CTest run selected one registered test, `KnobKeyboardTest`.

| Check | Exact command | Result |
| --- | --- | --- |
| RED, original handler | `ctest -R '^KnobKeyboardTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-14.r1/logs/test-1.log 2>&1; echo EXIT=$?` | `EXIT=8`; 1 test ran and failed with SIGSEGV in the parentless key-release case |
| GREEN, fix applied | `ctest -R '^KnobKeyboardTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-14.r1/logs/test-2.log 2>&1; echo EXIT=$?` | `EXIT=0`; 1/1 passed |
| RED, fix reversed for sensitivity proof | `ctest -R '^KnobKeyboardTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-14.r1/logs/test-3.log 2>&1; echo EXIT=$?` | `EXIT=8`; the same test failed with SIGSEGV |
| GREEN, fix restored | `ctest -R '^KnobKeyboardTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-14.r1/logs/test-4.log 2>&1; echo EXIT=$?` | `EXIT=0`; 1/1 passed |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-14.r1/logs/gate-1.log 2>&1; gate_exit=$?; echo EXIT=$gate_exit` | `EXIT=0` |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-14.r1/logs/gate-2.log 2>&1; gate_exit=$?; echo EXIT=$gate_exit` | `EXIT=0` |

## Build and sensitivity commands

| Exact command | Result |
| --- | --- |
| `cmake --build build --target zene KnobKeyboardTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-14.r1/logs/build-1.log 2>&1; build_exit=$?; echo EXIT=$build_exit` | `EXIT=0` |
| `cmake --build build --target zene KnobKeyboardTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-14.r1/logs/build-2.log 2>&1; build_exit=$?; echo EXIT=$build_exit` | `EXIT=0` |
| `git diff -- include/ColorChooser.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-14.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-14.r1/logs/fix.patch; echo EXIT=$?` | `EXIT=0` |
| `cmake --build build --target zene KnobKeyboardTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-14.r1/logs/build-3.log 2>&1; build_exit=$?; echo EXIT=$build_exit` | `EXIT=0` |
| `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-14.r1/logs/fix.patch; echo EXIT=$?` | `EXIT=0` |
| `cmake --build build --target zene KnobKeyboardTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-14.r1/logs/build-4.log 2>&1; build_exit=$?; echo EXIT=$build_exit` | `EXIT=0` |

## Files committed

- `include/ColorChooser.h`
- `tests/src/gui/KnobKeyboardTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-14.md`
