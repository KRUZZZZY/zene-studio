# Cluster new-p10-250: send model names after channel moves

**Verdict: FIXED**

## Evidence

- `src/core/Mixer.cpp:80`: `MixerRoute::updateName()` now refreshes both amount and pan display names using the route's current endpoint indices.
- `src/core/Mixer.cpp:1308`: `Mixer::moveChannelLeft()` refreshes regular and sidechain routes connected to either swapped index after updating the channel indices.
- `tests/src/core/MixingRoutingTest.cpp:313`: `movingAChannelRefreshesRouteModelNames()` checks outgoing and incoming regular sends, regular-send pan labels, and a sidechain amount label.
- `tests/upstream-modifications.txt:695`: records why the inherited `Mixer.cpp` file changed.

## Regression and verification

The pre-fix run failed because the outgoing send amount still named channel 1 after its sender moved to index 2. The QtTest output reported 10 passed and 1 failed. Removing the production patch reproduced that same failure. Both green runs passed the single registered CTest target, `MixingRoutingTest` (1/1).

| Phase | Exact command | Exit |
|---|---|---:|
| Initial red build | `cmake --build build --target MixingRoutingTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-250.r1/logs/build-1.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Initial red test | `ctest -R '^MixingRoutingTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-250.r1/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc` | 8 |
| Green build | `cmake --build build --target zene MixingRoutingTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-250.r1/logs/build-2.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Green test | `ctest -R '^MixingRoutingTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-250.r1/logs/test-2.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Save production patch | `git diff -- src/core/Mixer.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-250.r1/logs/fix.patch; rc=$?; echo EXIT=$rc` | 0 |
| Remove production patch | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-250.r1/logs/fix.patch; rc=$?; echo EXIT=$rc` | 0 |
| Reverted build | `cmake --build build --target zene MixingRoutingTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-250.r1/logs/build-3.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Reverted test (expected red) | `ctest -R '^MixingRoutingTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-250.r1/logs/test-3.log 2>&1; rc=$?; echo EXIT=$rc` | 8 |
| Reapply production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-250.r1/logs/fix.patch; rc=$?; echo EXIT=$rc` | 0 |
| Restored build | `cmake --build build --target zene MixingRoutingTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-250.r1/logs/build-4.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Restored test | `ctest -R '^MixingRoutingTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-250.r1/logs/test-4.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-250.r1/logs/complexity-gate-1.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-250.r1/logs/file-length-gate-1.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |

## Files committed

- `src/core/Mixer.cpp`
- `tests/src/core/MixingRoutingTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-250.md`
