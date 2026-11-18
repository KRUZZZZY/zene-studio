# Cluster new-p10-54: metronome level bounds

**Verdict: FIXED**

`src/core/ControlCommandsRecordTools.cpp:73-75` now checks the requested `double` against `0..2` before converting it to `float`. The regression at `tests/src/core/RecordToolsTest.cpp:310-311` sends `2.00000001` through `transport.metronome_options` and expects `InvalidArgs`.

The finding is real. On the original code the new assertion returned `errorKind` 0 (success), not `InvalidArgs` (3). The fresh-build red run and the reversal proof produced the same failure. With the patch applied, the focused test passed.

## Build and test evidence

All build and CTest output was redirected to a new file in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-54.r1/logs`. CTest commands ran from `build/tests`; build commands ran from the worktree root. The `EXIT` value is the captured exit code of the build or test command.

| Phase | Command | EXIT | Log / result |
|---|---|---:|---|
| Initial regression build | `cmake --build build --target RecordToolsTest` | 0 | `build-1.log` |
| Initial RED | `ctest -R '^RecordToolsTest$' --output-on-failure` | 8 | `test-1.log`; 1/1 CTest target, Qt: 15 passed, 1 failed |
| Fresh original app build | `cmake --build build --target zene` | 0 | `build-2.log` |
| Fresh original test build | `cmake --build build --target RecordToolsTest` | 0 | `build-3.log` |
| Fresh-build RED | `ctest -R '^RecordToolsTest$' --output-on-failure` | 8 | `test-2.log`; 1/1 CTest target, Qt: 15 passed, 1 failed |
| Fixed app build | `cmake --build build --target zene` | 0 | `build-4.log` |
| Fixed test build | `cmake --build build --target RecordToolsTest` | 0 | `build-5.log` |
| GREEN | `ctest -R '^RecordToolsTest$' --output-on-failure` | 0 | `test-3.log`; 1/1 CTest target passed |
| Remove product patch | `git diff -- src/core/ControlCommandsRecordTools.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-54.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-54.r1/logs/fix.patch` | 0 | Reversal proof |
| Reversal build | `cmake --build build --target zene RecordToolsTest` | 0 | `build-6.log` |
| Reversal RED | `ctest -R '^RecordToolsTest$' --output-on-failure` | 8 | `test-4.log`; 1/1 CTest target, Qt: 15 passed, 1 failed |
| Reapply product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-54.r1/logs/fix.patch` | 0 | Reversal proof |
| Restored build | `cmake --build build --target zene RecordToolsTest` | 0 | `build-7.log` |
| Restored GREEN | `ctest -R '^RecordToolsTest$' --output-on-failure` | 0 | `test-5.log`; 1/1 CTest target passed |

## Gates

| Command | EXIT | Log |
|---|---:|---|
| `bash tests/complexity-gate.sh --check` | 0 | `gate-1.log` |
| `bash tests/file-length-gate.sh --check` | 0 | `gate-2.log` |

The `fff` grep tool was not available in this environment, so log scans used `rg -n -i` separately for `error`, `FAILED`, and `Assertion`.

## Files in the commit

- `src/core/ControlCommandsRecordTools.cpp`
- `tests/src/core/RecordToolsTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p10-54.md`
