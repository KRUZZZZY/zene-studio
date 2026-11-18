# Cluster new-p9-391 — FIXED

**Verdict:** FIXED. `RmsHelper` now deep-copies its sample buffer and rolling RMS state in both copy construction and copy assignment. Assignment allocates before releasing the old buffer, and handles self-assignment.

**Evidence:** [include/RmsHelper.h](../../include/RmsHelper.h#L46) implements the copy operations. [PluginPortsMigrationTest.cpp](../../tests/src/plugins/PluginPortsMigrationTest.cpp#L732) verifies independent state for copy construction and assignment. The inherited header is declared in [tests/upstream-modifications.txt](../../tests/upstream-modifications.txt#L1054).

The suggested filter `^DynamicsProcessor$` is not registered in this configured tree: `ctest -N` reported zero matches. The existing registered family that builds and renders the DynamicsProcessor is `PluginPortsMigrationTest`; its CTest run is recorded below.

## Evidence table

Commands ran from the stated working directory. Build and test output is in the numbered log shown.

| Phase | Command | Exit / result |
|---|---|---|
| Baseline build (`cwd`: worktree) | `cmake --build build --target PluginPortsMigrationTest -j12 > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-391.r1/logs/build-1.log" 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| RED on original header (`cwd`: `build/tests`) | `./PluginPortsMigrationTest rmsHelperCopiesHaveIndependentBuffers > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-391.r1/logs/red-1.log" 2>&1; rc=$?; echo EXIT=$rc` | 134; log reports `free(): double free detected in tcache 2` |
| Fixed build (`cwd`: worktree) | `cmake --build build --target zene PluginPortsMigrationTest -j12 > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-391.r1/logs/build-2.log" 2>&1; rc=$?; echo EXIT=$rc` | 0; rebuilt `dynamicsprocessor`, its reference module, and the test target |
| GREEN (`cwd`: `build/tests`) | `./PluginPortsMigrationTest rmsHelperCopiesHaveIndependentBuffers > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-391.r1/logs/green-1.log" 2>&1; rc=$?; echo EXIT=$rc` | 0; QtTest totals: 3 passed, 0 failed (init, slot, cleanup) |
| Remove-fix command (`cwd`: worktree) | `git diff -- include/RmsHelper.h > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-391.r1/logs/fix.patch" && git apply -R "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-391.r1/logs/fix.patch"; rc=$?; echo EXIT=$rc` | 0 |
| Rebuild without fix (`cwd`: worktree) | `cmake --build build --target zene PluginPortsMigrationTest -j12 > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-391.r1/logs/build-3.log" 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| RED after removing fix (`cwd`: `build/tests`) | `./PluginPortsMigrationTest rmsHelperCopiesHaveIndependentBuffers > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-391.r1/logs/red-2.log" 2>&1; rc=$?; echo EXIT=$rc` | 134; double-free detected again |
| Restore-fix command (`cwd`: worktree) | `git apply "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-391.r1/logs/fix.patch"; rc=$?; echo EXIT=$rc` | 0 |
| Rebuild restored fix (`cwd`: worktree) | `cmake --build build --target zene PluginPortsMigrationTest -j12 > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-391.r1/logs/build-4.log" 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| GREEN after restore (`cwd`: `build/tests`) | `./PluginPortsMigrationTest rmsHelperCopiesHaveIndependentBuffers > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-391.r1/logs/green-2.log" 2>&1; rc=$?; echo EXIT=$rc` | 0; QtTest totals: 3 passed, 0 failed |
| Affected CTest family (`cwd`: `build/tests`) | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-391.r1/logs/ctest-1.log" 2>&1; rc=$?; echo EXIT=$rc` | 0; 1/1 CTest passed |
| Complexity gate (`cwd`: worktree) | `bash tests/complexity-gate.sh --check > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-391.r1/logs/gate-1.log" 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| File-length gate (`cwd`: worktree) | `bash tests/file-length-gate.sh --check > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-391.r1/logs/gate-2.log" 2>&1; rc=$?; echo EXIT=$rc` | 0 |

The initial filter check was `ctest -N -R '^DynamicsProcessor$' > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-391.r1/logs/test-1.log" 2>&1; rc=$?; echo EXIT=$rc` from `build/tests`; it exited 0 but reported `Total Tests: 0`, so it was not counted as a test pass.

## Committed files

- `include/RmsHelper.h`
- `tests/src/plugins/PluginPortsMigrationTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-391.md`
