ALREADY-FIXED

gui-1112-menu-edit-undo-port: the reported defect was real, and the product fix is already present on this branch. I re-proved the existing fix by reversing it, observing the regression fail, restoring it, and observing the same regression pass. I also added a core-level regression to this port branch so the contract is covered without the GUI test.

## Decision and evidence

The defect was caused by `Song::clearProject()` forcing journalling back on when it returned inside `Song::loadProject()`'s journalling-off window. The fix is already committed on this branch as `68cef118c091bcb31355eb890fa1d5d316ca4325`; `git merge-base --is-ancestor 68cef118c091bcb31355eb890fa1d5d316ca4325 HEAD` exited 0.

The current code shows the fixed contract:

* `Song::loadProject()` turns journalling off before the load (`src/core/Song.cpp:2031`), calls `clearProject()` inside that window (`src/core/Song.cpp:2105`), and turns journalling on only when the load is complete (`src/core/Song.cpp:2340`).
* `Song::clearProject()` saves the state it found (`src/core/Song.cpp:1745`), keeps journalling off while clearing (`src/core/Song.cpp:1746`), and restores the saved state on the way out (`src/core/Song.cpp:1885`).
* `Song::createNewProject()` still finds journalling on when it calls `clearProject()` (`src/core/Song.cpp:1915`), gets it back on, then turns it off for its own track construction (`src/core/Song.cpp:1922`) and back on at the end (`src/core/Song.cpp:1949`).
* `Engine::destroy()` calls `stopAllJournalling()` (`src/core/Engine.cpp:99`) before `clearProject()` (`src/core/Engine.cpp:114`), so it now leaves journalling off instead of re-enabling it during teardown.

The existing GUI regression is `tests/src/gui/MainWindowTest.cpp:278`, `aFreshProjectOpensWithAnEmptyUndoHistory`.

## Regression test added to this port

`tests/src/core/ProjectOpenIntegrityTest.cpp:430`, `aFreshProjectLoadRecordsNoUndoStep`, with the fixture at `tests/src/core/ProjectOpenIntegrityTest.cpp:168`. The fixture writes a master volume in element form (`<volume value="0.5"/>`), which is read through `AutomatableModel::setValue()` - the same call an edit makes. The test clears the journal, loads the project, asserts the volume reader ran (`0.5f`), asserts `undoDepth() == 0` and `canUndo() == false`, then proves journalling is back on by making one real edit (`undoDepth() == 1`), undoing it, and checking the volume returns to `0.5f` and the depth returns to `0`.

The test is deterministic: it uses fixed XML values and synchronous model writes; it contains no clock read, sleep, timer, thread or race.

## RED/GREEN evidence

All commands were run in this worktree. `fix.patch` is the historical product hunk only:

```sh
git diff 68cef118c091bcb31355eb890fa1d5d316ca4325^ 68cef118c091bcb31355eb890fa1d5d316ca4325 -- src/core/Song.cpp > /home/kruzzzzy/.config/kads/zene/state/runs/gui-1112-menu-edit-undo-port.r1/logs/fix.patch
# DIFF_EXIT=0
```

| Step | Command | Exit | Log |
|---|---|---:|---|
| Build core test with fix | `cmake --build build --target ProjectOpenIntegrityTest -j4` | 0 | `017-build-core-test-green.log` |
| Core test green | `cd build/tests && QT_QPA_PLATFORM=offscreen ./ProjectOpenIntegrityTest aFreshProjectLoadRecordsNoUndoStep` | 0 | `018-test-core-green.log` |
| Reverse product hunk | `git apply --check -R .../logs/fix.patch` | 0 | `019-apply-reverse-core.log` |
| Rebuild core test without fix | `cmake --build build --target ProjectOpenIntegrityTest -j4` | 0 | `020-build-core-red.log` |
| Core test RED | `cd build/tests && QT_QPA_PLATFORM=offscreen ./ProjectOpenIntegrityTest aFreshProjectLoadRecordsNoUndoStep` | 1 | `021-test-core-red.log` |
| Restore product hunk | `git apply .../logs/fix.patch` | 0 | `022-apply-restore-core.log` |
| Rebuild core test with fix | `cmake --build build --target ProjectOpenIntegrityTest -j4` | 0 | `023-build-core-green-restored.log` |
| Core test green restored | `cd build/tests && QT_QPA_PLATFORM=offscreen ./ProjectOpenIntegrityTest aFreshProjectLoadRecordsNoUndoStep` | 0 | `024-test-core-green-restored.log` |
| Core family green | `cd build/tests && ctest -R '^ProjectOpenIntegrityTest$' --output-on-failure` | 0 | `025-ctest-core-family-green.log` |
| Build GUI test | `cmake --build build --target MainWindowTest -j4` | 0 | `006-build-mainwindowtest.log` |
| GUI test green | `cd build/tests && QT_QPA_PLATFORM=offscreen ./MainWindowTest aFreshProjectOpensWithAnEmptyUndoHistory` | 0 | `008-test-freshundo-green.log` |
| GUI test RED | same command after reversing `fix.patch` | 1 | `011-test-freshundo-red.log` |
| GUI test green restored | same command after restoring `fix.patch` | 0 | `014-test-freshundo-green-restored.log` |
| GUI family green | `cd build/tests && ctest -R '^MainWindowTest$' --output-on-failure` | 0 | `015-ctest-family-green-restored.log` |

The RED core run failed at `tests/src/core/ProjectOpenIntegrityTest.cpp:470` with `loading the project recorded 1 undo step(s): a fresh load must leave nothing to undo`. The RED GUI run failed at `tests/src/gui/MainWindowTest.cpp:287` with `Actual (depth) 1`, `Expected 0`. Both GREEN runs passed all their test slots. `git status --porcelain` was empty after restoring the product hunk.

## Gates

`python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-gui-1112-menu-edit-undo-port` exited 0 (`026-feature-check.log`):

```text
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
```

## Files committed

* `tests/src/core/ProjectOpenIntegrityTest.cpp` - the added core regression test.
* `docs/qwen-fixes-2026-10/cluster-gui-1112-menu-edit-undo-port.md` - this report.

## Self-check

* **Regression test FAILS with the product change reverted and PASSES with it.** Core: RED `021-test-core-red.log` EXIT=1 (`undoDepth` 1, expected 0); GREEN `024-test-core-green-restored.log` EXIT=0. GUI: RED `011-test-freshundo-red.log` EXIT=1 (`depth` 1, expected 0); GREEN `014-test-freshundo-green-restored.log` EXIT=0. The reversal used only `src/core/Song.cpp` and was restored before the green runs.
* **The test is deterministic.** It uses fixed XML values and synchronous `setValue()` calls. There is no wall-clock timing, sleep, timer, thread or race. The seam is the project journal's journalling flag itself.
* **The fix is at the cause and complete.** The cause is the unconditional re-enable in `clearProject()`; the current code restores the caller's state (`src/core/Song.cpp:1745`, `:1885`). I checked all three product callers: `loadProject()` (`:2105`) gets its `false` back until `:2340`; `createNewProject()` (`:1915`) gets `true` and owns its own off/on window at `:1922`/`:1949`; `Engine::destroy()` (`src/core/Engine.cpp:114`) gets the `false` set by `stopAllJournalling()` at `:99`. Test helpers call `clearProject()` with journalling on and get it back on.
* **No collateral damage.** This port changes only the test file and this report; no product, save-format, command or registration file changed. Old projects load exactly as before. No limit or validation check was added, so no other caller can be rejected. The fix is UI-thread project-replacement code and touches only a `bool` read and write; it adds nothing to the audio thread.
* **ALREADY-FIXED evidence.** `68cef118c091bcb31355eb890fa1d5d316ca4325` is an ancestor of this branch (`git merge-base --is-ancestor` EXIT=0), and the current code and tests at `src/core/Song.cpp:1745`, `:1885`, `tests/src/gui/MainWindowTest.cpp:278` and `tests/src/core/ProjectOpenIntegrityTest.cpp:430` show the fixed contract and its coverage.
* **Sibling commands and snapshot.** `submit_check.py` lists `app.*` and `arrangement.*` ids because it diffs the whole branch against the train merge-base; those changes come from the branch's merged history, not from this cluster. `git show --stat --oneline HEAD` for the commit holding this report shows exactly two paths: this report and `tests/src/core/ProjectOpenIntegrityTest.cpp`. No command file, command id, reversibility table or `commands_snapshot.json` changed, so none of those sibling handlers is affected.

## Completion check

`python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-gui-1112-menu-edit-undo-port gui-1112-menu-edit-undo-port` exited 0 (`032-submit-check-final.log`). Every mechanical row is PASS, including the report, clean working tree, tests and gates; every ASK item is answered in the Self-check above.

## Other findings

None.
