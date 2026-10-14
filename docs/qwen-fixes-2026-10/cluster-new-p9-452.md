# Cluster new-p9-452

Verdict: **FIXED**

## Finding and fix

`Sf2Instrument::openFile()` in [plugins/Sf2Player/Sf2Player.cpp:382](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-452/plugins/Sf2Player/Sf2Player.cpp:382) transactionally retains the selected SoundFont and source unless a candidate validates. The round-two UI defect was in `Sf2InstrumentView::invalidateFile()` at [plugins/Sf2Player/Sf2Player.cpp:1184](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-452/plugins/Sf2Player/Sf2Player.cpp:1184): it unconditionally disabled the patch button on `fileLoading()`. It now keeps that button enabled when the retained instrument filename is nonempty.

The live-view regression at [tests/src/plugins/PluginPortsMigrationTest.cpp:733](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-452/tests/src/plugins/PluginPortsMigrationTest.cpp:733) loads a valid font, creates the plugin view, checks the patch button before and after a failed replacement, and continues to verify the saved source and audible retained font.

## Evidence

| Check | Exact command | Exit | Result |
|---|---|---:|---|
| Regression RED before the view fix | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-452.r2/logs/test-2.log 2>&1; rc=$?; echo EXIT=$rc` (run from `build/tests`) | 8 | `patchButton->isEnabled()` returned false; QtTest: 8 passed, 1 failed. |
| GREEN with the fix | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-452.r2/logs/test-3.log 2>&1; rc=$?; echo EXIT=$rc` (run from `build/tests`) | 0 | 1/1 CTest tests passed. |
| Reverse only the product hunk | `git diff -- plugins/Sf2Player/Sf2Player.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-452.r2/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-452.r2/logs/fix.patch; rc=$?; echo EXIT=$rc` | 0 | Reversed the sole product change. |
| RED with product hunk reversed | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-452.r2/logs/test-4.log 2>&1; rc=$?; echo EXIT=$rc` (run from `build/tests`) | 8 | The same patch-button assertion failed; QtTest: 8 passed, 1 failed. |
| Restore product hunk | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-452.r2/logs/fix.patch; rc=$?; echo EXIT=$rc` | 0 | Restored the fix. |
| GREEN after restore | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-452.r2/logs/test-5.log 2>&1; rc=$?; echo EXIT=$rc` (run from `build/tests`) | 0 | 1/1 CTest tests passed. |
| Affected family count | CTest filter `^PluginPortsMigrationTest$` | — | 1 CTest test executed (the QtTest executable contains 9 test cases). |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-452.r2/logs/gate-1.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | Passed. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-452.r2/logs/gate-2.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | Passed. |

## Build command ledger

All commands below ran from the worktree root. The first two build entries and first test entry record fixture development; the final product, reverse-proof, and restored builds all succeeded.

- `cmake --build build --target zene PluginPortsMigrationTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-452.r2/logs/build-1.log 2>&1; rc=$?; echo EXIT=$rc` — **2** (the initial test draft called the protected `instantiateView()` method).
- `cmake --build build --target zene PluginPortsMigrationTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-452.r2/logs/build-2.log 2>&1; rc=$?; echo EXIT=$rc` — **0**.
- `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-452.r2/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc` (from `build/tests`) — **8** (initial view fixture had a null parent chain; corrected before the regression RED run).
- `cmake --build build --target zene PluginPortsMigrationTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-452.r2/logs/build-3.log 2>&1; rc=$?; echo EXIT=$rc` — **0**.
- `cmake --build build --target zene sf2player PluginPortsMigrationTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-452.r2/logs/build-4.log 2>&1; rc=$?; echo EXIT=$rc` — **0** (fixed product).
- `cmake --build build --target zene sf2player PluginPortsMigrationTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-452.r2/logs/build-5.log 2>&1; rc=$?; echo EXIT=$rc` — **0** (product hunk reversed).
- `cmake --build build --target zene sf2player PluginPortsMigrationTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-452.r2/logs/build-6.log 2>&1; rc=$?; echo EXIT=$rc` — **0** (product hunk restored).

The `fff` grep tool is not exposed in this environment and no `fff` executable is installed. I searched the saved logs with `rg -n` instead.

## Files committed

- `plugins/Sf2Player/Sf2Player.cpp`
- `tests/src/plugins/PluginPortsMigrationTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-452.md`
