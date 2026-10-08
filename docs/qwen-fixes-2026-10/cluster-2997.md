# Cluster 2997: NaN bypasses the VCA gain range check

Verdict: **FIXED**

## Evidence

`src/core/ControlCommandsVcaMix.cpp:96` now rejects a non-finite gain before the existing `0..2` bounds check. The regression at `tests/src/core/ControlVcaCommandsTest.cpp:399` passes a NaN through the in-process registry and checks that the group fader remains unchanged.

The RED run failed at the regression's `refused` assertion: 10 Qt cases passed and the new case failed. The restored GREEN run passed all 11 Qt cases. The affected CTest family contains one registered suite, `ControlVcaCommandsTest`.

All build, test, and gate output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/2997.r1/logs`. The commands below are the command portions run from the worktree root unless a different working directory is stated.

| Phase | Exact command | Exit | Log / evidence |
|---|---|---:|---|
| Initial test build | `cmake --build build --target ControlVcaCommandsTest` | 0 | `build-1.log` |
| Initial RED | From `build/tests`: `ctest -R '^ControlVcaCommandsTest$' --output-on-failure` | 8 | `test-1.log`; the new `refused` assertion failed |
| Refreshed pre-fix build | `cmake --build build --target zene ControlVcaCommandsTest` | 0 | `build-2.log` |
| RED after refreshed build | From `build/tests`: `ctest -R '^ControlVcaCommandsTest$' --output-on-failure` | 8 | `test-2.log`; 10 passed, 1 failed at `refused` |
| Build with fix | `cmake --build build --target zene ControlVcaCommandsTest` | 0 | `build-3.log` |
| GREEN | From `build/tests`: `ctest -R '^ControlVcaCommandsTest$' --output-on-failure` | 0 | `test-3.log`; 1/1 CTest suites passed |
| Save and reverse production patch | `git diff -- src/core/ControlCommandsVcaMix.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/2997.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/2997.r1/logs/fix.patch` | 0 | Patch removed for the rollback proof |
| Rebuild without fix | `cmake --build build --target zene ControlVcaCommandsTest` | 0 | `build-4.log` |
| Rollback RED | From `build/tests`: `ctest -R '^ControlVcaCommandsTest$' --output-on-failure` | 8 | `test-4.log`; same `refused` assertion failed |
| Restore production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/2997.r1/logs/fix.patch` | 0 | Fix restored |
| Rebuild after restore | `cmake --build build --target zene ControlVcaCommandsTest` | 0 | `build-5.log` |
| GREEN after restore | From `build/tests`: `ctest -R '^ControlVcaCommandsTest$' --output-on-failure` | 0 | `test-5.log`; 1/1 CTest suites passed |
| Qt case count | From `build/tests`: `ctest -R '^ControlVcaCommandsTest$' --output-on-failure --verbose` | 0 | `test-6.log`; 11 passed, 0 failed |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 0 | `gate-1.log` |
| File length gate | `bash tests/file-length-gate.sh --check` | 0 | `gate-2.log` |
| Whitespace check | `git diff --check` | 0 | No issues reported |

## Files committed

- `src/core/ControlCommandsVcaMix.cpp`
- `tests/src/core/ControlVcaCommandsTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-2997.md`
