# Cluster new-p10-149: FastBassBoost frequency floor

**Verdict: FIXED**

`FastBassBoost` clamps the constructor frequency to 10, but its setter previously stored negative values directly. For `setFrequency(-1)`, the feedback denominator became zero and the next sample was non-finite. The setter now applies the same 10 Hz minimum as the constructor.

## Evidence

- [include/DspEffectLibrary.h:230](../../include/DspEffectLibrary.h) applies the lower bound in `FastBassBoost::setFrequency`.
- [tests/src/plugins/PluginPortsMigrationTest.cpp:222](../../tests/src/plugins/PluginPortsMigrationTest.cpp) sets `-1`, checks the output is finite, and compares the sample sequence with a 10 Hz instance.
- [tests/upstream-modifications.txt:1054](../../tests/upstream-modifications.txt) records the inherited production and test file changes.

The suggested `ctest -R '^Lb302$'` selector has no registered test in this configured tree. The existing `PluginPortsMigrationTest` executable covers the Lb302 slice and was used for the regression.

## RED/GREEN and revert proof

Commands below were run in the worktree; ctest commands were run from `build/tests`. Build and test output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-149.r1/logs`.

| Run | Exact command | Exit | Result |
| --- | --- | ---: | --- |
| RED build, before production fix | `cmake --build build --target PluginPortsMigrationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-149.r1/logs/build-1.log 2>&1; echo EXIT=$?` | 0 | Test target rebuilt with regression and original setter. |
| RED ctest | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-149.r1/logs/test-1.log 2>&1; echo EXIT=$?` | 8 | `std::isfinite(actual)` failed; QtTest totals: 8 passed, 1 failed, 0 skipped. CTest ran 1 test target. |
| GREEN build | `cmake --build build --target PluginPortsMigrationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-149.r1/logs/build-2.log 2>&1; echo EXIT=$?` | 0 | Rebuilt after the setter fix. |
| GREEN ctest | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-149.r1/logs/test-2.log 2>&1; echo EXIT=$?` | 0 | CTest passed 1/1 target. |
| Direct family run | `./PluginPortsMigrationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-149.r1/logs/test-direct-1.log 2>&1; echo EXIT=$?` | 0 | QtTest totals: 9 passed, 0 failed, 0 skipped. |
| Reverse production patch | `git diff -- include/DspEffectLibrary.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-149.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-149.r1/logs/fix.patch; echo EXIT=$?` | 0 | Temporarily removed only the production fix. |
| Revert-proof build | `cmake --build build --target PluginPortsMigrationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-149.r1/logs/build-3.log 2>&1; echo EXIT=$?` | 0 | Rebuilt with original setter. |
| Revert-proof ctest | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-149.r1/logs/test-3.log 2>&1; echo EXIT=$?` | 8 | The same `std::isfinite(actual)` assertion failed; totals: 8 passed, 1 failed, 0 skipped. |
| Restore production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-149.r1/logs/fix.patch; echo EXIT=$?` | 0 | Restored the 10 Hz setter floor. |
| Final build | `cmake --build build --target PluginPortsMigrationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-149.r1/logs/build-4.log 2>&1; echo EXIT=$?` | 0 | Rebuilt with the fix restored. |
| Final ctest | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-149.r1/logs/test-4.log 2>&1; echo EXIT=$?` | 0 | CTest passed 1/1 target. |

## Ratchets

- `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-149.r1/logs/complexity-gate-1.log 2>&1; echo EXIT=$?` — **EXIT=0**.
- `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-149.r1/logs/file-length-gate-1.log 2>&1; echo EXIT=$?` — **EXIT=0**.

Files to commit: `include/DspEffectLibrary.h`, `tests/src/plugins/PluginPortsMigrationTest.cpp`, `tests/upstream-modifications.txt`, and this report.
