# Cluster new-p8-15 — project.switch reference failure

**Verdict: FIXED**

`ReferenceProject::open()` returns `false` and fills `why` when the old project file cannot be parsed as a project with song tracks ([ReferenceProject.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p8-15/src/core/ReferenceProject.cpp:104)). `switchProject()` previously discarded that result and returned an empty `reference` even when `keep_as_reference` was requested ([ControlCommandsDocuments.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p8-15/src/core/ControlCommandsDocuments.cpp:240)). It now returns the failure text in `reference_error` while preserving the successful project switch, and its command description documents that response.

The regression test opens a valid old project, replaces its on-disk content with a project document without song tracks, then switches to another valid project with `keep_as_reference:true`. It verifies that the switch succeeds, the target is active, and `reference_error` describes why the old project could not be kept ([ArrangingCatchUpTest.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p8-15/tests/src/core/ArrangingCatchUpTest.cpp:375)).

## Evidence

| Check | Exact command | Exit |
|---|---|---:|
| Clean pre-fix RED | `QT_QPA_PLATFORM=offscreen ctest -R ArrangingCatchUpTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-15.r1/logs/test-2.log 2>&1; echo EXIT=$?` | 8 |
| Fixed GREEN | `QT_QPA_PLATFORM=offscreen ctest -R ArrangingCatchUpTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-15.r1/logs/test-3.log 2>&1; echo EXIT=$?` | 0 |
| Inverse-patch RED (after rebuilding) | `QT_QPA_PLATFORM=offscreen ctest -R ArrangingCatchUpTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-15.r1/logs/test-4.log 2>&1; echo EXIT=$?` | 8 |
| Restored-patch GREEN | `QT_QPA_PLATFORM=offscreen ctest -R ArrangingCatchUpTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-15.r1/logs/test-5.log 2>&1; echo EXIT=$?` | 0 |
| Verbose affected-family count | `QT_QPA_PLATFORM=offscreen ctest -R ArrangingCatchUpTest --output-on-failure -V > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-15.r1/logs/test-6.log 2>&1; echo EXIT=$?` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-15.r1/logs/gate-3.log 2>&1; echo EXIT=$?` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-15.r1/logs/gate-4.log 2>&1; echo EXIT=$?` | 0 |

The affected family ran **1/1 CTest targets**. Its final QtTest summary was **18 passed, 0 failed, 1 skipped**. The clean RED run failed only at the new assertion that `reference_error` contains the no-song-tracks reason; the same assertion failed when the production patch was removed.

The first pre-fix family run also exposed an unrelated missing `tripleoscillator` test module. After building that target, the clean RED run above reproduced only the intended failure. The initial run and all build/test invocations are recorded below with their observed exit codes.

## Build, reversion, and test commands

```text
cmake --build build --target zene ArrangingCatchUpTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-15.r1/logs/build-1.log 2>&1; echo EXIT=$?  # 0
QT_QPA_PLATFORM=offscreen ctest -R ArrangingCatchUpTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-15.r1/logs/test-1.log 2>&1; echo EXIT=$?  # 8; regression plus missing test module
cmake --build build --target tripleoscillator -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-15.r1/logs/build-2.log 2>&1; echo EXIT=$?  # 0
QT_QPA_PLATFORM=offscreen ctest -R ArrangingCatchUpTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-15.r1/logs/test-2.log 2>&1; echo EXIT=$?  # 8; clean RED
cmake --build build --target zene ArrangingCatchUpTest tripleoscillator -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-15.r1/logs/build-3.log 2>&1; echo EXIT=$?  # 0
QT_QPA_PLATFORM=offscreen ctest -R ArrangingCatchUpTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-15.r1/logs/test-3.log 2>&1; echo EXIT=$?  # 0
git diff -- src/core/ControlCommandsDocuments.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-15.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-15.r1/logs/fix.patch; echo EXIT=$?  # 0
cmake --build build --target zene ArrangingCatchUpTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-15.r1/logs/build-4.log 2>&1; echo EXIT=$?  # 0
QT_QPA_PLATFORM=offscreen ctest -R ArrangingCatchUpTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-15.r1/logs/test-4.log 2>&1; echo EXIT=$?  # 8; inverse-patch RED
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-15.r1/logs/fix.patch; echo EXIT=$?  # 0
cmake --build build --target zene ArrangingCatchUpTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-15.r1/logs/build-5.log 2>&1; echo EXIT=$?  # 0
QT_QPA_PLATFORM=offscreen ctest -R ArrangingCatchUpTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-15.r1/logs/test-5.log 2>&1; echo EXIT=$?  # 0; restored-patch GREEN
QT_QPA_PLATFORM=offscreen ctest -R ArrangingCatchUpTest --output-on-failure -V > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-15.r1/logs/test-6.log 2>&1; echo EXIT=$?  # 0
bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-15.r1/logs/complexity-gate.log 2>&1; echo EXIT=$?  # 0
bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-15.r1/logs/file-length-gate.log 2>&1; echo EXIT=$?  # 0
bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-15.r1/logs/gate-3.log 2>&1; echo EXIT=$?  # 0; final post-report gate
bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-15.r1/logs/gate-4.log 2>&1; echo EXIT=$?  # 0; final post-report gate
```

## Commit contents

- `src/core/ControlCommandsDocuments.cpp`
- `tests/src/core/ArrangingCatchUpTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-15.md`
