# Cluster new-p10-252

Verdict: **FIXED**

`src/core/MixerOptions.cpp:81-83` now checks `QString::toFloat`'s parse result and rejects non-finite values before calling `setCrossfader`, using the center position (`0.f`) for invalid input. Finite inputs still pass through the existing clamp. The regression in `tests/src/core/MixerToolsTest.cpp:349-363` loads `crossfader="nan"` and asserts that the result is finite and centered.

## Evidence

| Stage | Exact command | Exit | Result |
|---|---|---:|---|
| Original-code RED | `ctest -R '^MixerToolsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-252.r1/logs/test-1.log 2>&1; result=$?; echo EXIT=$result; exit $result` (run from `build/tests`) | 8 | `loadingNanCrossfaderUsesCentre` failed: `std::isfinite(loadedCrossfader)` returned false. |
| GREEN | `ctest -R '^MixerToolsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-252.r1/logs/test-2.log 2>&1; result=$?; echo EXIT=$result; exit $result` (run from `build/tests`) | 0 | 1/1 affected-family CTest tests passed. |
| Remove production fix | `git diff -- src/core/MixerOptions.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-252.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-252.r1/logs/fix.patch; result=$?; echo EXIT=$result; exit $result` | 0 | Reversed only the production change. |
| Reversal RED | `ctest -R '^MixerToolsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-252.r1/logs/test-3.log 2>&1; result=$?; echo EXIT=$result; exit $result` (run from `build/tests`) | 8 | The same `std::isfinite(loadedCrossfader)` assertion failed. |
| Restore production fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-252.r1/logs/fix.patch; result=$?; echo EXIT=$result; exit $result` | 0 | Reapplied the production change. |
| Restored GREEN | `ctest -R '^MixerToolsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-252.r1/logs/test-4.log 2>&1; result=$?; echo EXIT=$result; exit $result` (run from `build/tests`) | 0 | 1/1 affected-family CTest tests passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-252.r1/logs/complexity-gate-1.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 | Passed. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-252.r1/logs/file-length-gate-1.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 | Passed. |

Builds, run from the worktree root:

- `cmake --build build --target zene MixerToolsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-252.r1/logs/build-1.log 2>&1; result=$?; echo EXIT=$result; exit $result` — EXIT=0.
- `cmake --build build --target zene MixerToolsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-252.r1/logs/build-2.log 2>&1; result=$?; echo EXIT=$result; exit $result` — EXIT=0.
- `cmake --build build --target zene MixerToolsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-252.r1/logs/build-3.log 2>&1; result=$?; echo EXIT=$result; exit $result` — EXIT=0.
- `cmake --build build --target zene MixerToolsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-252.r1/logs/build-4.log 2>&1; result=$?; echo EXIT=$result; exit $result` — EXIT=0.

The initial build preceded the original-code RED; the remaining builds preceded their corresponding test runs.

The `fff` grep tool was not exposed in this environment. I searched the build, test, and gate logs with `rg -n` for each of `error`, `FAILED`, and `Assertion`; the only `FAILED` matches were the two expected RED runs.

Files committed:

- `src/core/MixerOptions.cpp`
- `tests/src/core/MixerToolsTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p10-252.md`
