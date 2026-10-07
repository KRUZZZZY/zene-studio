# Cluster new-p8-61

**Verdict: FIXED**

`src/gui/ControlCommandsShortcuts.cpp:160` now returns `invalid_args` when `shortcuts.import` receives both `file` and `overrides`, following the existing `theme.import` contract. The regression at `tests/src/gui/ShortcutsPageTest.cpp:112` invokes the registered command with a valid shortcut file and a disagreeing overrides object, and checks the error kind and message.

## Evidence

Commands ran in the worktree unless marked `build/tests`. Build and test logs are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-61.r1/logs`.

| Check | Exact command | Exit | Result |
|---|---|---:|---|
| Pre-fix build | `cmake --build build --target zene ShortcutsPageTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-61.r1/logs/build-2.log 2>&1; echo EXIT=$?` | 0 | `zene` and the affected test target built. |
| RED, original code (`build/tests`) | `ctest -R ShortcutsPageTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-61.r1/logs/test-1.log 2>&1; echo EXIT=$?` | 8 | The regression failed: `shortcuts.import accepted both sources`. 1/1 CTest target failed. |
| Post-fix build | `cmake --build build --target zene ShortcutsPageTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-61.r1/logs/build-3.log 2>&1; echo EXIT=$?` | 0 | Both targets rebuilt. |
| GREEN, fixed code (`build/tests`) | `ctest -R ShortcutsPageTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-61.r1/logs/test-2.log 2>&1; echo EXIT=$?` | 0 | 1/1 CTest target passed. |
| Save production patch | `git diff -- src/gui/ControlCommandsShortcuts.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-61.r1/logs/fix.patch; echo EXIT=$?` | 0 | Captured only the production change. |
| Remove fix | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-61.r1/logs/fix.patch; echo EXIT=$?` | 0 | Restored original handler for the reversal check. |
| Reversal build | `cmake --build build --target zene ShortcutsPageTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-61.r1/logs/build-4.log 2>&1; echo EXIT=$?` | 0 | Rebuilt original handler and test. |
| Reversal RED (`build/tests`) | `ctest -R ShortcutsPageTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-61.r1/logs/test-3.log 2>&1; echo EXIT=$?` | 8 | The same assertion failed again; 1/1 CTest target failed. |
| Restore fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-61.r1/logs/fix.patch; echo EXIT=$?` | 0 | Reapplied the saved production change. |
| Restored build | `cmake --build build --target zene ShortcutsPageTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-61.r1/logs/build-5.log 2>&1; echo EXIT=$?` | 0 | Rebuilt fixed handler and test. |
| Reversal GREEN (`build/tests`) | `ctest -R ShortcutsPageTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-61.r1/logs/test-4.log 2>&1; echo EXIT=$?` | 0 | 1/1 CTest target passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-61.r1/logs/complexity-gate-1.log 2>&1; echo EXIT=$?` | 0 | Passed. |
| File length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-61.r1/logs/file-length-gate-1.log 2>&1; echo EXIT=$?` | 0 | Passed. |

An earlier test build, logged as `build-1.log`, exposed missing `lmms` namespace qualification in the new test setup. I corrected that before the RED run. The first short tool poll did not retain that command's `EXIT` line, so I do not report an exit code for that preliminary attempt; the clean build-2 command above exited 0 before ctest ran.

`src/gui/ControlCommandsShortcuts.cpp` was already registered in `tests/fork-sources.txt`; no new product file or manifest entry was needed.

## Files committed

- `src/gui/ControlCommandsShortcuts.cpp`
- `tests/src/gui/ShortcutsPageTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-61.md`
