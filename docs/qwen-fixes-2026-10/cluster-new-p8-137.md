# Cluster new-p8-137

## Verdict

**FIXED**

<code>ItemListPanel::selectionEdited()</code> in <code>src/gui/ItemListPanel.cpp:363-369</code> now synchronizes only the table's actually selected rows. Clearing the last selected row therefore sends an empty selection to the canvas. The current-row fallback remains in <code>chosenRows()</code> for actions such as Delete.

<code>tests/src/gui/ItemListPanelTest.cpp:201-212</code> covers Ctrl-clicking the sole selected row and verifies both the canvas note and the table selection are cleared.

The regression test is red with the production patch removed (test-4.log, exit 8) and green with the patch restored (test-5.log, exit 0). The affected CTest family executed 1/1 target: <code>ItemListPanelTest</code>.

## Command ledger

Commands below were run from the worktree root unless the row says <code>build/tests</code>. Every build and test output was redirected to the named new log file.

| Stage | Command | Exit |
|---|---|---:|
| Initial regression build | <code>cmake --build build --target ItemListPanelTest &gt; '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-137.r1/logs/build-1.log' 2&gt;&amp;1; result=$?; printf 'EXIT=%s\n' "$result"</code> | 0 |
| Initial RED run (<code>build/tests</code>) | <code>ctest -R '^ItemListPanelTest$' --output-on-failure &gt; '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-137.r1/logs/test-1.log' 2&gt;&amp;1; result=$?; printf 'EXIT=%s\n' "$result"</code> | 8 |
| First fixed build | <code>cmake --build build --target ItemListPanelTest &gt; '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-137.r1/logs/build-2.log' 2&gt;&amp;1; result=$?; printf 'EXIT=%s\n' "$result"</code> | 0 |
| First fixed run (<code>build/tests</code>; test setup precondition failed) | <code>ctest -R '^ItemListPanelTest$' --output-on-failure &gt; '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-137.r1/logs/test-2.log' 2&gt;&amp;1; result=$?; printf 'EXIT=%s\n' "$result"</code> | 8 |
| Corrected test setup build | <code>cmake --build build --target ItemListPanelTest &gt; '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-137.r1/logs/build-3.log' 2&gt;&amp;1; result=$?; printf 'EXIT=%s\n' "$result"</code> | 0 |
| Corrected test setup run (<code>build/tests</code>) | <code>ctest -R '^ItemListPanelTest$' --output-on-failure &gt; '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-137.r1/logs/test-3.log' 2&gt;&amp;1; result=$?; printf 'EXIT=%s\n' "$result"</code> | 0 |
| Remove only the production patch | <code>git diff -- src/gui/ItemListPanel.cpp &gt; '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-137.r1/logs/fix.patch' &amp;&amp; git apply -R '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-137.r1/logs/fix.patch'; result=$?; printf 'EXIT=%s\n' "$result"</code> | 0 |
| Final RED proof build | <code>cmake --build build --target ItemListPanelTest &gt; '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-137.r1/logs/build-4.log' 2&gt;&amp;1; result=$?; printf 'EXIT=%s\n' "$result"</code> | 0 |
| Final RED proof (<code>build/tests</code>) | <code>ctest -R '^ItemListPanelTest$' --output-on-failure &gt; '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-137.r1/logs/test-4.log' 2&gt;&amp;1; result=$?; printf 'EXIT=%s\n' "$result"</code> | 8 |
| Restore the production patch | <code>git apply '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-137.r1/logs/fix.patch'; result=$?; printf 'EXIT=%s\n' "$result"</code> | 0 |
| Final GREEN proof build | <code>cmake --build build --target ItemListPanelTest &gt; '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-137.r1/logs/build-5.log' 2&gt;&amp;1; result=$?; printf 'EXIT=%s\n' "$result"</code> | 0 |
| Final GREEN proof (<code>build/tests</code>) | <code>ctest -R '^ItemListPanelTest$' --output-on-failure &gt; '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-137.r1/logs/test-5.log' 2&gt;&amp;1; result=$?; printf 'EXIT=%s\n' "$result"</code> | 0 |
| Rebuild app | <code>cmake --build build --target zene &gt; '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-137.r1/logs/build-6.log' 2&gt;&amp;1; result=$?; printf 'EXIT=%s\n' "$result"</code> | 0 |
| Complexity gate | <code>bash tests/complexity-gate.sh --check &gt; '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-137.r1/logs/complexity-gate-1.log' 2&gt;&amp;1; result=$?; printf 'EXIT=%s\n' "$result"</code> | 0 |
| File-length gate | <code>bash tests/file-length-gate.sh --check &gt; '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-137.r1/logs/file-length-gate-1.log' 2&gt;&amp;1; result=$?; printf 'EXIT=%s\n' "$result"</code> | 0 |

The changed production file is already registered in <code>tests/fork-sources.txt</code>. The test file is fork-authored and was not present at fork point <code>4e677cb6c6ab</code>; no upstream-modifications entry applies.

## Files committed

- <code>src/gui/ItemListPanel.cpp</code>
- <code>tests/src/gui/ItemListPanelTest.cpp</code>
- <code>docs/qwen-fixes-2026-10/cluster-new-p8-137.md</code>
