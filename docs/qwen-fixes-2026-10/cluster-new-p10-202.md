# Cluster new-p10-202

**Verdict: FIXED**

`src/gui/ItemListPanel.cpp:85-92` now treats a percent sign as a suffix only when parsing a percent column, and removes at most one trailing `%`. `ItemListSource::parse()` dispatches `ItemColumn::Kind::Percent` to that mode at line 111. Number and key parsing leave `%` intact, so `toInt()` rejects embedded or otherwise misplaced percent signs.

The regression in `tests/src/gui/ItemListPanelTest.cpp:171-181` exercises the note velocity edit path with `1%2`, verifies no edit occurs, accepts `42 %` for a percent column, and rejects a percent sign on a number column, repeated suffixes, and an embedded percent sign.

## Evidence

| Evidence | Result |
|---|---|
| RED | `ItemListPanelTest` failed on `numericEditsRejectPercentCharacters`: `!edit(..., "1%2")` returned false (the invalid edit was accepted); exact ctest command exit 8 below. |
| GREEN | The same ctest command passed after the fix; exit 0 below. |
| Patch reversal proof | Rebuilt with the product fix removed and reproduced the same failure (exit 8), then restored the fix and passed (exit 0). |
| Affected family count | 1 registered CTest test matched and executed: `ItemListPanelTest` (`1/1`). |
| Complexity gate | `bash tests/complexity-gate.sh --check` — exit 0. |
| File length gate | `bash tests/file-length-gate.sh --check` — exit 0. |
| Files committed | `src/gui/ItemListPanel.cpp`; `tests/src/gui/ItemListPanelTest.cpp`; `docs/qwen-fixes-2026-10/cluster-new-p10-202.md`. |

## Commands and exit codes

Build and test commands were run from the worktree root, except `ctest`, which was run from `build/tests`. All build and test output was captured under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-202.r1/logs`.

Initial regression (RED):

```text
cmake --build build --target ItemListPanelTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-202.r1/logs/build-1.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-202.r1/logs/build-1.log; exit "$rc"
EXIT=0
ctest -R '^ItemListPanelTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-202.r1/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-202.r1/logs/test-1.log; exit "$rc"
EXIT=8
```

After the fix (GREEN):

```text
cmake --build build --target zene ItemListPanelTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-202.r1/logs/build-2.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-202.r1/logs/build-2.log; exit "$rc"
EXIT=0
ctest -R '^ItemListPanelTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-202.r1/logs/test-2.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-202.r1/logs/test-2.log; exit "$rc"
EXIT=0
```

Patch reversal proof (`fix.patch` contains only the product change):

```text
git diff -- src/gui/ItemListPanel.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-202.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-202.r1/logs/fix.patch; rc=$?; echo EXIT=$rc; exit "$rc"
EXIT=0
cmake --build build --target zene ItemListPanelTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-202.r1/logs/build-3.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-202.r1/logs/build-3.log; exit "$rc"
EXIT=0
ctest -R '^ItemListPanelTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-202.r1/logs/test-3.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-202.r1/logs/test-3.log; exit "$rc"
EXIT=8
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-202.r1/logs/fix.patch; rc=$?; echo EXIT=$rc; exit "$rc"
EXIT=0
cmake --build build --target zene ItemListPanelTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-202.r1/logs/build-4.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-202.r1/logs/build-4.log; exit "$rc"
EXIT=0
ctest -R '^ItemListPanelTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-202.r1/logs/test-4.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-202.r1/logs/test-4.log; exit "$rc"
EXIT=0
```

Gates:

```text
bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-202.r1/logs/gate-1.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-202.r1/logs/gate-1.log; exit "$rc"
EXIT=0
bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-202.r1/logs/gate-2.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-202.r1/logs/gate-2.log; exit "$rc"
EXIT=0
```
