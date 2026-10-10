# Cluster new-p10-188

**Verdict: FIXED**

## Finding and fix

Both handlers treated `angleDelta().y() == 0` as a negative wheel step. The integer LCD handler now ignores and returns for a zero vertical delta at [LcdSpinBox.cpp:151](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-188/src/gui/widgets/LcdSpinBox.cpp:151); the float LCD handler does the same before selecting a cursor-dependent step at [LcdFloatSpinBox.cpp:197](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-188/src/gui/widgets/LcdFloatSpinBox.cpp:197). Vertical wheel handling is unchanged.

The regression sends horizontal-only wheel events and checks unchanged values and ignored events in [KnobKeyboardTest.cpp:191](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-188/tests/src/gui/KnobKeyboardTest.cpp:191) and [KnobKeyboardTest.cpp:203](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-188/tests/src/gui/KnobKeyboardTest.cpp:203). Before the fix, both models changed from 50 to 49.

## Regression and build evidence

Commands below ran from the worktree root for builds and `build/tests` for ctest. Each redirected log ends with its captured `EXIT` code. The affected ctest family is one CTest test, `KnobKeyboardTest`.

| Phase | Exact command | EXIT | Result |
|---|---|---:|---|
| RED, original handlers | `ctest -R '^KnobKeyboardTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-188.r1/logs/test-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-188.r1/logs/test-2.log; printf 'EXIT=%s LOG=%s\n' "$rc" /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-188.r1/logs/test-2.log; exit "$rc"` | 8 | Integer and float assertions failed at 49 (expected 50); 15 passed, 2 failed. |
| GREEN, fixed handlers | `ctest -R '^KnobKeyboardTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-188.r1/logs/test-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-188.r1/logs/test-3.log; printf 'EXIT=%s LOG=%s\n' "$rc" /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-188.r1/logs/test-3.log; exit "$rc"` | 0 | 1/1 CTest test passed. |

An earlier combined RED run (`ctest -R '^KnobKeyboardTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-188.r1/logs/test-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-188.r1/logs/test-1.log; printf 'EXIT=%s LOG=%s\n' "$rc" /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-188.r1/logs/test-1.log; exit "$rc"`) exited 8 and caught the integer case. It was superseded by test-2 after splitting the integer and float assertions. Each run was preceded by `cmake --build build --target zene KnobKeyboardTest`, which exited 0 and wrote build-1.log through build-5.log.

The required reverse-patch proof also reproduced RED, then passed after restoring the patch:

| Phase | Exact command | EXIT |
|---|---|---:|
| Save and reverse production patch | `git diff -- src/gui/widgets/LcdSpinBox.cpp src/gui/widgets/LcdFloatSpinBox.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-188.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-188.r1/logs/fix.patch` | 0 |
| Rebuild without fix | `cmake --build build --target zene KnobKeyboardTest` (log: `build-4.log`) | 0 |
| RED without fix | `ctest -R '^KnobKeyboardTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-188.r1/logs/test-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-188.r1/logs/test-4.log; printf 'EXIT=%s LOG=%s\n' "$rc" /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-188.r1/logs/test-4.log; exit "$rc"` | 8 |
| Restore production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-188.r1/logs/fix.patch` | 0 |
| Rebuild with fix restored | `cmake --build build --target zene KnobKeyboardTest` (log: `build-5.log`) | 0 |
| GREEN with fix restored | `ctest -R '^KnobKeyboardTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-188.r1/logs/test-5.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-188.r1/logs/test-5.log; printf 'EXIT=%s LOG=%s\n' "$rc" /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-188.r1/logs/test-5.log; exit "$rc"` | 0 |

The `zene` and `KnobKeyboardTest` targets were rebuilt before each ctest run. The other recorded build commands (`cmake --build build --target zene KnobKeyboardTest`) exited 0: `build-1.log`, `build-2.log`, and `build-3.log`.

## Required gates

| Gate | Exact command | EXIT |
|---|---|---:|
| Complexity | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-188.r1/logs/gate-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-188.r1/logs/gate-3.log; printf 'EXIT=%s LOG=%s\n' "$rc" /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-188.r1/logs/gate-3.log; exit "$rc"` | 0 |
| File length | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-188.r1/logs/gate-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-188.r1/logs/gate-4.log; printf 'EXIT=%s LOG=%s\n' "$rc" /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-188.r1/logs/gate-4.log; exit "$rc"` | 0 |

Log searches for `error`, `FAILED`, and `Assertion` used `rg`; the fff grep tool was not exposed in this session.

## Files committed

- `src/gui/widgets/LcdSpinBox.cpp`
- `src/gui/widgets/LcdFloatSpinBox.cpp`
- `tests/src/gui/KnobKeyboardTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-188.md`
