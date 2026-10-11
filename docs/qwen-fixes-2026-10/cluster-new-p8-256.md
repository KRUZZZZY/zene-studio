# Cluster new-p8-256

Verdict: **FIXED**

## Evidence

- `src/core/AafImportBuild.cpp:138-141` compares the requested fader value with the sample track model's range, counts any clamp in `AafBuildReport`, then applies the value.
- `include/AafImport.h:230,262` carries that count through the loss and build reports. `src/core/AafImportModel.cpp:83` serializes it as `loss.track_gain_clamps`; `src/core/ControlCommandsAafImport.cpp:280` copies the build count into the import reply.
- `tests/src/core/AafImportTest.cpp:241-258` builds a synthetic track at 250%, verifies its clamped 200% fader and one reported clamp, and checks the loss JSON key.
- RED failed at `reportedTrackGainClamps(report)`: actual `-1`, expected `1`. Removing the production patch reproduced that failure; restoring it passed.

Affected family: `AafImportTest`, one registered CTest test (`Test #211`). The red QtTest run reported 11 passed and the new regression failed; the green CTest run passed 1/1.

## Commands and results

Build and test commands ran from the worktree root unless a CTest command says otherwise. Their output was redirected to the numbered log shown.

| Stage | Exact command | Exit |
|---|---|---:|
| RED build | `cmake --build build --target zene AafImportTest -- -j4 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-256.r1/logs/build-1.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-256.r1/logs/build-1.log; echo EXIT=$rc` | 0 |
| RED test (cwd `build/tests`) | `ctest -R AafImportTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-256.r1/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-256.r1/logs/test-1.log; echo EXIT=$rc` | 8 |
| GREEN build | `cmake --build build --target zene AafImportTest -- -j4 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-256.r1/logs/build-2.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-256.r1/logs/build-2.log; echo EXIT=$rc` | 0 |
| GREEN test (cwd `build/tests`) | `ctest -R AafImportTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-256.r1/logs/test-2.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-256.r1/logs/test-2.log; echo EXIT=$rc` | 0 |
| Save production patch | `git diff -- include/AafImport.h src/core/AafImportBuild.cpp src/core/AafImportModel.cpp src/core/ControlCommandsAafImport.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-256.r1/logs/fix.patch` | 0 |
| Reverse patch | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-256.r1/logs/fix.patch` | 0 |
| Reverse-proof build | `cmake --build build --target zene AafImportTest -- -j4 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-256.r1/logs/build-3.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-256.r1/logs/build-3.log; echo EXIT=$rc` | 0 |
| Reverse-proof test (cwd `build/tests`) | `ctest -R AafImportTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-256.r1/logs/test-3.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-256.r1/logs/test-3.log; echo EXIT=$rc` | 8 |
| Restore production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-256.r1/logs/fix.patch` | 0 |
| Final build | `cmake --build build --target zene AafImportTest -- -j4 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-256.r1/logs/build-4.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-256.r1/logs/build-4.log; echo EXIT=$rc` | 0 |
| Final test (cwd `build/tests`) | `ctest -R AafImportTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-256.r1/logs/test-4.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-256.r1/logs/test-4.log; echo EXIT=$rc` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-256.r1/logs/gate-1.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-256.r1/logs/gate-1.log; echo EXIT=$rc` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-256.r1/logs/gate-2.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-256.r1/logs/gate-2.log; echo EXIT=$rc` | 0 |

Each build/test/gate command appended and printed its captured `EXIT` code. The RED and reverse-proof test logs show the failing assertion; the GREEN logs show `1/1` passed. Both gate logs report `PASS (check mode: no regressions; baseline not written)`.

## Files committed

- `include/AafImport.h`
- `src/core/AafImportBuild.cpp`
- `src/core/AafImportModel.cpp`
- `src/core/ControlCommandsAafImport.cpp`
- `tests/src/core/AafImportTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-256.md`
