# Cluster new-p10-286

**Verdict: FIXED**

## Finding

PatchesDialog::bankChanged() previously stopped scanning later soundfonts after the first soundfont added a unique program. It now uses lmms::sf2::forEachUniquePresetForBank(), which visits all soundfonts and leaves duplicate filtering to the existing findProgItem() lookup. The first soundfont still supplies a duplicated program's name.

The shared iteration policy is in plugins/Sf2Player/PatchPresetIteration.h:9 and is called from plugins/Sf2Player/PatchesDialog.cpp:339, 375. The deterministic regression is PluginPortsMigrationTest::sf2PatchListKeepsEarlierDuplicateAndLaterUniqueProgram() at tests/src/plugins/PluginPortsMigrationTest.cpp:222-256. It feeds two fixture soundfonts: the later one contains a duplicate program and a unique program. The test checks that the unique program appears and the earlier duplicate remains selected.

## Evidence table

| Check | Exact command and result |
| --- | --- |
| PatchesDialog CTest discovery | From build/tests: <code>ctest -N -R '^PatchesDialog$' &gt; /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-286.r2/logs/test-1.log 2&gt;&amp;1; result=$?; printf 'EXIT=%s\n' "$result" &gt;&gt; /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-286.r2/logs/test-1.log; printf 'EXIT=%s\n' "$result"</code>; ctest exit **0**, Total Tests: 0. This was discovery only; the regression runs in an existing registered test target. |
| Build regression target with old behavior | From the worktree: <code>cmake --build build --target PluginPortsMigrationTest -j2 &gt; /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-286.r2/logs/build-2.log 2&gt;&amp;1; result=$?; printf 'EXIT=%s\n' "$result" &gt;&gt; /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-286.r2/logs/build-2.log; printf 'EXIT=%s\n' "$result"</code>; build exit **0**. |
| RED, before fix | From build/tests: <code>ctest -R '^PluginPortsMigrationTest$' --output-on-failure &gt; /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-286.r2/logs/test-2.log 2&gt;&amp;1; result=$?; printf 'EXIT=%s\n' "$result" &gt;&gt; /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-286.r2/logs/test-2.log; printf 'EXIT=%s\n' "$result"</code>. Captured CTest exit: **8**; the regression failed because the later unique program was absent (8 passed, 1 failed in the QtTest suite). |
| GREEN, after fix | From the worktree: <code>cmake --build build --target zene sf2player PluginPortsMigrationTest -j2 &gt; /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-286.r2/logs/build-3.log 2&gt;&amp;1; result=$?; printf 'EXIT=%s\n' "$result" &gt;&gt; /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-286.r2/logs/build-3.log; printf 'EXIT=%s\n' "$result"</code>; build exit **0**. From build/tests: <code>ctest -R '^PluginPortsMigrationTest$' --output-on-failure &gt; /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-286.r2/logs/test-3.log 2&gt;&amp;1; result=$?; printf 'EXIT=%s\n' "$result" &gt;&gt; /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-286.r2/logs/test-3.log; printf 'EXIT=%s\n' "$result"</code>. Captured CTest exit **0**, 1/1 registered CTest passed. |
| Focused regression | From build/tests: <code>./PluginPortsMigrationTest sf2PatchListKeepsEarlierDuplicateAndLaterUniqueProgram &gt; /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-286.r2/logs/test-4.log 2&gt;&amp;1; result=$?; printf 'EXIT=%s\n' "$result" &gt;&gt; /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-286.r2/logs/test-4.log; printf 'EXIT=%s\n' "$result"</code>; exit **0**, 3 passed (setup, regression slot, cleanup). |
| Affected-family count | PluginPortsMigrationTest is registered as CTest #313; **1 CTest executed** per run. The new regression slot ran in that target. |
| Inverse-patch generation | From the worktree: <code>git diff -- plugins/Sf2Player/PatchesDialog.cpp plugins/Sf2Player/PatchPresetIteration.h &gt; /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-286.r2/logs/fix.patch; result=$?; printf 'EXIT=%s\n' "$result"</code>; exit **0**. |
| Inverse proof RED | <code>git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-286.r2/logs/fix.patch; result=$?; printf 'EXIT=%s\n' "$result"</code>; exit **0**. Rebuild: <code>cmake --build build --target zene sf2player PluginPortsMigrationTest -j2 &gt; /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-286.r2/logs/build-4.log 2&gt;&amp;1; result=$?; printf 'EXIT=%s\n' "$result" &gt;&gt; /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-286.r2/logs/build-4.log; printf 'EXIT=%s\n' "$result"</code>; build exit **0**. From build/tests, <code>ctest -R '^PluginPortsMigrationTest$' --output-on-failure &gt; /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-286.r2/logs/test-5.log 2&gt;&amp;1; result=$?; printf 'EXIT=%s\n' "$result" &gt;&gt; /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-286.r2/logs/test-5.log; printf 'EXIT=%s\n' "$result"</code>; captured CTest exit **8**, same regression failure. |
| Inverse proof GREEN | <code>git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-286.r2/logs/fix.patch; result=$?; printf 'EXIT=%s\n' "$result"</code>; exit **0**. Rebuild: <code>cmake --build build --target zene sf2player PluginPortsMigrationTest -j2 &gt; /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-286.r2/logs/build-5.log 2&gt;&amp;1; result=$?; printf 'EXIT=%s\n' "$result" &gt;&gt; /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-286.r2/logs/build-5.log; printf 'EXIT=%s\n' "$result"</code>; build exit **0**. From build/tests, <code>ctest -R '^PluginPortsMigrationTest$' --output-on-failure &gt; /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-286.r2/logs/test-6.log 2&gt;&amp;1; result=$?; printf 'EXIT=%s\n' "$result" &gt;&gt; /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-286.r2/logs/test-6.log; printf 'EXIT=%s\n' "$result"</code>; captured CTest exit **0**, 1/1 passed. |
| Complexity gate | <code>bash tests/complexity-gate.sh --check &gt; /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-286.r2/logs/gate-3.log 2&gt;&amp;1; result=$?; printf 'EXIT=%s\n' "$result" &gt;&gt; /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-286.r2/logs/gate-3.log; printf 'EXIT=%s\n' "$result"</code>; exit **0**. |
| File-length gate | <code>bash tests/file-length-gate.sh --check &gt; /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-286.r2/logs/gate-4.log 2&gt;&amp;1; result=$?; printf 'EXIT=%s\n' "$result" &gt;&gt; /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-286.r2/logs/gate-4.log; printf 'EXIT=%s\n' "$result"</code>; exit **0**. |

The fff grep tool was unavailable in this session; build and test logs were searched with rg for error, FAILED, and Assertion. No configure was run.

## Files committed

- plugins/Sf2Player/PatchesDialog.cpp
- plugins/Sf2Player/PatchPresetIteration.h
- tests/src/plugins/PluginPortsMigrationTest.cpp
- tests/fork-sources.txt
- tests/upstream-modifications.txt
- docs/qwen-fixes-2026-10/cluster-new-p10-286.md
