# Cluster new-p10-34: send pan lost on route removal

**Verdict: FIXED**

## Finding and evidence

- `removeRegularSend` captures the send amount, pre-fader flag, pan, enabled state, pan automation mode, and attached automation clips. Its journal undo closure recreates the send and restores those captured values in [ControlCommandsMixerRoutes.cpp](../../src/core/ControlCommandsMixerRoutes.cpp#L300).
- The transaction now reports `control.undo` with empty args at [ControlCommandsMixerRoutes.cpp:341](../../src/core/ControlCommandsMixerRoutes.cpp#L341). Its mechanism explains that the journal restores the captured send state and pan automation; it no longer reports unsupported `mixer.route_to` arguments.
- [MixingRoutingTest.cpp:202](../../tests/src/core/MixingRoutingTest.cpp#L202) checks the before-state, invokes the reported inverse, verifies the empty args and mechanism, and checks restored pan/enabled state. [MixingRoutingTest.cpp:242](../../tests/src/core/MixingRoutingTest.cpp#L242) retains the regression for reattaching automated pan and preserving its value.
- The changed source and test files were already registered in `tests/fork-sources.txt`; no source registry edit was needed.

## Verification

Commands with `ctest` ran from `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-34/build/tests`; builds and gates ran from the worktree root. All build, test, and gate output was redirected to new files in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-34.r3/logs`.

| Purpose | Exact command | Exit | Evidence |
|---|---|---:|---|
| Initial test-only build | `cmake --build build --target zene MixingRoutingTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-34.r3/logs/build-1.log' 2>&1; status=$?; printf 'EXIT=%s\n' "$status"` | 0 | Test target rebuilt. |
| Initial test draft RED | `ctest -R MixingRoutingTest --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-34.r3/logs/test-1.log' 2>&1; status=$?; printf 'EXIT=%s\n' "$status"` | 8 | 11 passed, 1 failed at the inverse-op assertion; this draft was strengthened to invoke the reported inverse and check its mechanism. |
| Final regression RED build | `cmake --build build --target zene MixingRoutingTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-34.r3/logs/build-2.log' 2>&1; status=$?; printf 'EXIT=%s\n' "$status"` | 0 | Rebuilt before the final pre-fix run. |
| **Final regression RED** | `ctest -R MixingRoutingTest --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-34.r3/logs/test-2.log' 2>&1; status=$?; printf 'EXIT=%s\n' "$status"` | **8** | 11 passed, 1 failed because the reported inverse was rejected with `unexpected property 'amount'`. |
| Fixed build | `cmake --build build --target zene MixingRoutingTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-34.r3/logs/build-3.log' 2>&1; status=$?; printf 'EXIT=%s\n' "$status"` | 0 | Rebuilt `zene` and `MixingRoutingTest`. |
| **GREEN regression** | `ctest -R MixingRoutingTest --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-34.r3/logs/test-3.log' 2>&1; status=$?; printf 'EXIT=%s\n' "$status"` | **0** | 1/1 CTest target passed. |
| Reverse-source patch proof | `git diff -- src/core/ControlCommandsMixerRoutes.cpp > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-34.r3/logs/fix.patch' && git apply -R '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-34.r3/logs/fix.patch'; status=$?; printf 'EXIT=%s\n' "$status"` | 0 | Reversed only the product diff; test changes remained. |
| Build with product diff reversed | `cmake --build build --target zene MixingRoutingTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-34.r3/logs/reverse-build-1.log' 2>&1; status=$?; printf 'EXIT=%s\n' "$status"` | 0 | Rebuilt before the reverse-source check. |
| Reverse-source RED | `ctest -R MixingRoutingTest --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-34.r3/logs/reverse-test-1.log' 2>&1; status=$?; printf 'EXIT=%s\n' "$status"` | 8 | 11 passed, 1 failed on the same schema rejection, proving the regression depends on the product diff. |
| Restore product diff | `git apply '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-34.r3/logs/fix.patch'; status=$?; printf 'EXIT=%s\n' "$status"` | 0 | Restored the production change. |
| Restored build | `cmake --build build --target zene MixingRoutingTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-34.r3/logs/restore-build-1.log' 2>&1; status=$?; printf 'EXIT=%s\n' "$status"` | 0 | Rebuilt before the restored regression run. |
| Restored GREEN | `ctest -R MixingRoutingTest --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-34.r3/logs/restore-test-1.log' 2>&1; status=$?; printf 'EXIT=%s\n' "$status"` | 0 | 1/1 CTest target passed. |
| Affected-family case count | `./MixingRoutingTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-34.r3/logs/family-1.log' 2>&1; status=$?; printf 'EXIT=%s\n' "$status"` | 0 | **12 passed, 0 failed.** |
| Complexity gate | `bash tests/complexity-gate.sh --check > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-34.r3/logs/complexity-gate-final-1.log' 2>&1; status=$?; printf 'EXIT=%s\n' "$status"` | 0 | Passed. |
| File-length gate | `bash tests/file-length-gate.sh --check > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-34.r3/logs/file-length-gate-final-1.log' 2>&1; status=$?; printf 'EXIT=%s\n' "$status"` | 0 | Passed. |

The `fff grep` tool/command was unavailable in this runtime, so log checks used `rg` against the required terms and the test summaries.

## Files committed

- `src/core/ControlCommandsMixerRoutes.cpp`
- `tests/src/core/MixingRoutingTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p10-34.md`
