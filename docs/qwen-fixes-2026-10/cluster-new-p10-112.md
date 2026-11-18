# Cluster new-p10-112: Draggable zero-width ranges

Verdict: **FIXED**

## Evidence

`Draggable::mouseMoveEvent()` now ignores movement when `m_pointA == m_pointB` (`src/gui/widgets/Draggable.cpp:90`). `Draggable::handleMovement()` returns before mapping when `maxValue() - minValue()` is zero (`src/gui/widgets/Draggable.cpp:118-124`). The regressions at `tests/src/gui/KnobKeyboardTest.cpp:94-119` cover both cases.

Before the fix, the constant-range regression moved the handle from `(23,29)` to `(23,76)`, and the coincident-endpoint drag changed the model from `50` to `100`. Both failures reproduced after reversing the production patch. The same test passed after restoring it.

The requested `SlewDistortionControlDialog` ctest is not registered in this configured tree: `ctest -N` from `build/tests` exited 0 and listed 402 tests without that name. The dialog source is a reference plugin module, so the existing widget-focused `KnobKeyboardTest` was used. The affected ctest family ran **1 registered test**, and passed 1/1 after the fix.

## Commands and exits

All builds ran from the worktree root. All ctest commands ran from `build/tests`. The commands below include the output capture and exit-code recording that were used.

| Check | Command | Exit |
|---|---|---:|
| Test inventory | `ctest -N > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-112.r1/logs/test-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-112.r1/logs/test-1.log; exit "$rc"` | 0 |
| Build original code and regression | `cmake --build build --target zene KnobKeyboardTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-112.r1/logs/build-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-112.r1/logs/build-1.log; exit "$rc"` | 0 |
| RED before fix | `ctest -R '^KnobKeyboardTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-112.r1/logs/test-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-112.r1/logs/test-2.log; exit "$rc"` | 8 |
| Build with fix | `cmake --build build --target zene KnobKeyboardTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-112.r1/logs/build-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-112.r1/logs/build-2.log; exit "$rc"` | 0 |
| GREEN with fix | `ctest -R '^KnobKeyboardTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-112.r1/logs/test-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-112.r1/logs/test-3.log; exit "$rc"` | 0 |
| Remove product patch | `git diff -- src/gui/widgets/Draggable.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-112.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-112.r1/logs/fix.patch` | 0 |
| Build without fix | `cmake --build build --target zene KnobKeyboardTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-112.r1/logs/build-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-112.r1/logs/build-3.log; exit "$rc"` | 0 |
| Inverse RED | `ctest -R '^KnobKeyboardTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-112.r1/logs/test-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-112.r1/logs/test-4.log; exit "$rc"` | 8 |
| Restore product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-112.r1/logs/fix.patch` | 0 |
| Final build | `cmake --build build --target zene KnobKeyboardTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-112.r1/logs/build-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-112.r1/logs/build-4.log; exit "$rc"` | 0 |
| Final GREEN | `ctest -R '^KnobKeyboardTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-112.r1/logs/test-5.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-112.r1/logs/test-5.log; exit "$rc"` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-112.r1/logs/gate-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-112.r1/logs/gate-3.log; exit "$rc"` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-112.r1/logs/gate-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-112.r1/logs/gate-4.log; exit "$rc"` | 0 |

## Files committed

- `src/gui/widgets/Draggable.cpp`
- `tests/src/gui/KnobKeyboardTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-112.md`
