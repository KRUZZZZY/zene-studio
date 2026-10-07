# Cluster new-p8-125

Verdict: **FIXED**

## Evidence

- `src/core/HardwareIo.cpp:85-87`: channel cue loading now checks `QString::toInt(&validIndex)` and skips the element when conversion fails or the index is out of range.
- `src/core/HardwareIo.cpp:278-280`: cue master loading applies the same validation before indexing `s_cueMasters`.
- `tests/src/core/HardwareIoTest.cpp:364-403`: regressions exercise both missing and nonnumeric indices for channel cue sends and cue masters. With the production change reversed, both assertions fail because cue A is modified; with it restored, the family passes.

## Build and test evidence

Build commands ran from the worktree root. CTest commands ran from `build/tests`.

| Phase | Exact command | Exit | Result |
| --- | --- | ---: | --- |
| Test-first build | `cmake --build build --target zene HardwareIoTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-125.r1/logs/build-1.log 2>&1; echo EXIT=$?` | 0 | Built `zene` and `HardwareIoTest` before evaluating the regression. |
| Initial RED | `ctest -R '^HardwareIoTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-125.r1/logs/test-1.log 2>&1; echo EXIT=$?` | 8 | CTest ran 1/1 family test; both new slots reported `ignored` false. |
| Fixed build | `cmake --build build --target zene HardwareIoTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-125.r1/logs/build-2.log 2>&1; echo EXIT=$?` | 0 | Rebuilt production and test targets. |
| Initial GREEN | `ctest -R '^HardwareIoTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-125.r1/logs/test-2.log 2>&1; echo EXIT=$?` | 0 | CTest ran 1/1 family test successfully. |
| Reverse production patch | `git diff -- src/core/HardwareIo.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-125.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-125.r1/logs/fix.patch; echo EXIT=$?` | 0 | Removed only the production change for the required catch proof. |
| Reversion build | `cmake --build build --target zene HardwareIoTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-125.r1/logs/build-3.log 2>&1; echo EXIT=$?` | 0 | Rebuilt against the original buggy loader. |
| Reversion RED | `ctest -R '^HardwareIoTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-125.r1/logs/test-3.log 2>&1; echo EXIT=$?` | 8 | CTest ran 1/1 family test; both new slots failed again. |
| Restore production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-125.r1/logs/fix.patch; echo EXIT=$?` | 0 | Restored the exact production patch. |
| Restored build | `cmake --build build --target zene HardwareIoTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-125.r1/logs/build-4.log 2>&1; echo EXIT=$?` | 0 | Rebuilt `zene` and the affected test target. |
| Restored GREEN | `ctest -R '^HardwareIoTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-125.r1/logs/test-4.log 2>&1; echo EXIT=$?` | 0 | CTest ran 1/1 family test successfully. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-125.r1/logs/gate-1.log 2>&1; echo EXIT=$?` | 0 | Passed. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-125.r1/logs/gate-2.log 2>&1; echo EXIT=$?` | 0 | Passed. |

Affected family count: **1 CTest test (`HardwareIoTest`), 1/1 executed**.

## Files committed

- `src/core/HardwareIo.cpp`
- `tests/src/core/HardwareIoTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-125.md`
