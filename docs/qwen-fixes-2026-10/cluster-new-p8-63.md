# Cluster new-p8-63

**Verdict: FIXED**

The stale-ID removal from round 1 remains correct. `removeEditTrack` now reports `control.undo` when the removed membership has no live `Track`; that inverse applies the action checkpoint recorded by this command and restores the missing stable ID. Live-track removal keeps the `vca.track_add` inverse. See [ControlCommandsVcaEditSet.cpp:181](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p8-63/src/core/ControlCommandsVcaEditSet.cpp:181) and [ControlCommandsVcaEditSet.cpp:192](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p8-63/src/core/ControlCommandsVcaEditSet.cpp:192).

The regression at [ControlVcaCommandsTest.cpp:285](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p8-63/tests/src/core/ControlVcaCommandsTest.cpp:285) retains direct `control.undo` coverage, then removes the stale membership again, reads the latest `control.transactions` inverse, replays it through `ControlRegistry`, and verifies the missing membership returns.

## Evidence

| Check | Exact command | Result |
|---|---|---|
| RED with round 1 production code | `ctest -R '^ControlVcaCommandsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-63.r2/logs/test-red-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` (from `build/tests`) | EXIT=8; 1/1 ctest failed. The replayed inverse was `vca.track_add`, which returned `no track trk-6 (the song has 0)`. |
| GREEN after inverse fix | `ctest -R '^ControlVcaCommandsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-63.r2/logs/test-green-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` (from `build/tests`) | EXIT=0; 1/1 ctest passed. |
| Reverse production patch | `git diff -- src/core/ControlCommandsVcaEditSet.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-63.r2/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-63.r2/logs/fix.patch; rc=$?; printf 'EXIT=%s\n' "$rc"` | EXIT=0. |
| RED with production patch reversed | `ctest -R '^ControlVcaCommandsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-63.r2/logs/test-reverse-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` (from `build/tests`) | EXIT=8; 1/1 ctest failed on the same `vca.track_add` / missing-track replay. |
| Reapply production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-63.r2/logs/fix.patch; rc=$?; printf 'EXIT=%s\n' "$rc"` | EXIT=0. |
| Final GREEN | `ctest -R '^ControlVcaCommandsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-63.r2/logs/test-green-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` (from `build/tests`) | EXIT=0; 1/1 ctest passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-63.r2/logs/gate-complexity-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` | EXIT=0. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-63.r2/logs/gate-file-length-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` | EXIT=0. |

Affected-family count: `ControlVcaCommandsTest` ran as 1/1 registered ctest in each targeted invocation; none reported zero tests.

All builds exited 0:

- `cmake --build build --parallel 12 --target zene ControlVcaCommandsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-63.r2/logs/build-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` — EXIT=0 (regression added, round 1 production code).
- `cmake --build build --parallel 12 --target zene ControlVcaCommandsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-63.r2/logs/build-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` — EXIT=0 (inverse fix).
- `cmake --build build --parallel 12 --target zene ControlVcaCommandsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-63.r2/logs/build-reverse-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` — EXIT=0 (production patch reversed for the negative control).
- `cmake --build build --parallel 12 --target zene ControlVcaCommandsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-63.r2/logs/build-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` — EXIT=0 (patch restored).

The `fff grep` tool was not available in this session; build, test, and gate logs were searched with targeted `rg` queries. Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-63.r2/logs`.

## Files committed

- `src/core/ControlCommandsVcaEditSet.cpp`
- `tests/src/core/ControlVcaCommandsTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-63.md`
