# Cluster new-p10-186: LFO WhiteNoise initial sample

Verdict: **FIXED**

`m_heldSample` was read on the first WhiteNoise buffer before any phase wrap assigned it. The constructor now initializes it to `0.0f` in [LfoController.cpp](../../src/core/LfoController.cpp:53). The regression in [UndoHonestyTest.cpp](../../tests/src/core/UndoHonestyTest.cpp:386) poisons the controller's storage, selects WhiteNoise, advances one period, and checks the first output is finite and clamped.

## RED/GREEN and gates

All ctest commands below ran from `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-186/build/tests`.

| Check | Exact command | Exit | Evidence |
|---|---|---:|---|
| RED, original code | `ctest -R '^UndoHonestyTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-186.r1/logs/test-1.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 8 | `whiteNoiseStartsWithAValidHeldSample`: `std::isfinite(firstSample)` returned false. |
| GREEN, fixed code | `ctest -R '^UndoHonestyTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-186.r1/logs/test-2.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 | 100% passed; 1 ctest test executed. |
| Reverse-patch RED | `ctest -R '^UndoHonestyTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-186.r1/logs/test-3.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 8 | Removing the production patch reproduces the same non-finite assertion. |
| Restored-patch GREEN | `ctest -R '^UndoHonestyTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-186.r1/logs/test-4.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 | 100% passed; 1 ctest test executed. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-186.r1/logs/gate-3.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 | Passed. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-186.r1/logs/gate-4.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 | Passed. |

The affected ctest family contains **1 ctest test** (`UndoHonestyTest`); it passed after the fix and after restoring the patch in the reverse-patch proof.

## Build commands

| Exact command | Exit |
|---|---:|
| `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-186.r1/logs/build-1.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| `cmake --build build --target UndoHonestyTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-186.r1/logs/build-2.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 2 |
| `cmake --build build --target UndoHonestyTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-186.r1/logs/build-3.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-186.r1/logs/build-4.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| `cmake --build build --target UndoHonestyTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-186.r1/logs/build-5.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-186.r1/logs/build-6.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| `cmake --build build --target UndoHonestyTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-186.r1/logs/build-7.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-186.r1/logs/build-8.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| `cmake --build build --target UndoHonestyTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-186.r1/logs/build-9.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |

Build 2 exposed a test compile error: `ValueBuffer::values()` returns a pointer. The regression was corrected to use `length()` and `value(0)`; the subsequent build and test runs succeeded.

## Reverse-patch proof

| Exact command | Exit |
|---|---:|
| `git diff -- src/core/LfoController.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-186.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-186.r1/logs/fix.patch; result=$?; echo EXIT=$result; exit $result` | 0 |
| `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-186.r1/logs/fix.patch; result=$?; echo EXIT=$result; exit $result` | 0 |

## Explicit commit files

- `src/core/LfoController.cpp`
- `tests/src/core/UndoHonestyTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-186.md`
