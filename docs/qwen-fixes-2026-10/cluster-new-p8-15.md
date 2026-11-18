# Cluster new-p8-15 — project.switch reference failure

**Verdict: FIXED**

The round 1 fix reported failures to open a saved previous project as a reference, but still omitted `reference_error` when the previous session had no filename. In [ControlCommandsDocuments.cpp:253](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p8-15/src/core/ControlCommandsDocuments.cpp:253), `project.switch` now returns "the previous session has no project file to keep as a reference" when `keep_as_reference` is true and the old filename is empty. The successful `project.open` result remains successful and names the new active project.

The regression at [ArrangingCatchUpTest.cpp:400](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p8-15/tests/src/core/ArrangingCatchUpTest.cpp:400) creates a valid target, returns to a clean untitled session, and checks the switch succeeds, the target is active, the reference is empty, and the explicit error is present. It failed before the fix and failed again with the production hunk reverted. The existing saved-project parse-failure regression also passed with the restored hunk.

## Evidence

| Check | Exact command | Exit |
|---|---|---:|
| Clean pre-fix RED | `QT_QPA_PLATFORM=offscreen ctest -R ArrangingCatchUpTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-15.r2/logs/test-1.log 2>&1; echo EXIT=$?` | 8 |
| Fixed GREEN | `QT_QPA_PLATFORM=offscreen ctest -R ArrangingCatchUpTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-15.r2/logs/test-2.log 2>&1; echo EXIT=$?` | 0 |
| Inverse-patch RED after rebuild | `QT_QPA_PLATFORM=offscreen ctest -R ArrangingCatchUpTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-15.r2/logs/test-3.log 2>&1; echo EXIT=$?` | 8 |
| Restored-patch GREEN | `QT_QPA_PLATFORM=offscreen ctest -R ArrangingCatchUpTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-15.r2/logs/test-4.log 2>&1; echo EXIT=$?` | 0 |
| Affected-family count | `QT_QPA_PLATFORM=offscreen ctest -R ArrangingCatchUpTest --output-on-failure -V > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-15.r2/logs/test-5.log 2>&1; echo EXIT=$?` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-15.r2/logs/gate-1.log 2>&1; echo EXIT=$?` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-15.r2/logs/gate-2.log 2>&1; echo EXIT=$?` | 0 |
| `git diff --check` | `git diff --check` | 0 |

The affected family ran **1/1 CTest targets**. QtTest reported **19 passed, 0 failed, 1 skipped**. The RED runs failed at the new `reference_error` assertion; the restored run passed.

## Build and inverse-patch commands

```text
cmake --build build --target ArrangingCatchUpTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-15.r2/logs/build-1.log 2>&1; echo EXIT=$?  # 0
cmake --build build --target zene ArrangingCatchUpTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-15.r2/logs/build-2.log 2>&1; echo EXIT=$?  # 0
git diff -- src/core/ControlCommandsDocuments.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-15.r2/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-15.r2/logs/fix.patch; echo EXIT=$?  # 0
cmake --build build --target zene ArrangingCatchUpTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-15.r2/logs/build-3.log 2>&1; echo EXIT=$?  # 0
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-15.r2/logs/fix.patch; echo EXIT=$?  # 0
cmake --build build --target zene ArrangingCatchUpTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-15.r2/logs/build-4.log 2>&1; echo EXIT=$?  # 0
```

## Files committed

- `src/core/ControlCommandsDocuments.cpp`
- `tests/src/core/ArrangingCatchUpTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-15.md`
