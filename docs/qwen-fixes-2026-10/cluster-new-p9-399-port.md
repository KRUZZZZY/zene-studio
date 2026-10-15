# Cluster new-p9-399-port — RowTableView delegate for rows inserted after setModel

**Verdict: FIXED** (on `bugs/qwen-new-p9-399-port`, ported by hand onto the `050/m0-train-5` line; not merged, cherry-picked or rebased).

ALREADY-FIXED was ruled out: the newest line still had the per-row loop, so the bug was present.

## Finding

`src/gui/RowTableView.cpp` `setModel()` assigned `m_rowDelegate` only to the rows that existed when the model was set (`setItemDelegateForRow` for `i < rowCount()`). A row inserted later fell through to the view's default `QStyledItemDelegate` and lost the row-specific painting and focus behaviour.

## Port of accepted fix `51841b369`

The accepted commit replaced the loop with a single default-delegate install. The same change is made here, against the current code:

- `src/gui/RowTableView.cpp:121` — `setItemDelegate( m_rowDelegate );` replaces the `for` loop of `setItemDelegateForRow`. The `model == nullptr` early return above it (added by cluster new-p10-338, `e0ae3f7d8`) is kept.
- `tests/src/gui/RowTableViewTest.cpp:48` — regression slot `insertedRowsUseTheRowDelegate`. It is added to the existing `RowTableViewTest` source, which is already registered in `tests/CMakeLists.txt` (line 947), so no new test file is registered.
- `tests/upstream-modifications.txt:53` — ledger entry for the inherited file `src/gui/RowTableView.cpp`, with a reason, as the no-upstream-regression gate requires.

The regression test asserts that the delegate of row 0 (set when the model is assigned) equals the delegate of row 1, inserted afterwards.

## Evidence

Build note: the first build failed with `ccache: failed to create temporary file ... /run/user/1000/ccache-tmp ... Permission denied`. The fix was to set `CCACHE_TEMPDIR=/tmp/ccache-tmp-qwen-p9` for the build commands. No CMake configuration was changed.

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-399-port.r1/logs/`.

| Step | Command (run from the worktree / `build/tests`) | EXIT | Result |
| --- | --- | --- | --- |
| Build, first try (failed) | `cmake --build build --target zene RowTableViewTest -j2 > build-1.log 2>&1` | 2 | ccache permission error |
| Build, RED baseline | `CCACHE_TEMPDIR=/tmp/ccache-tmp-qwen-p9 cmake --build build --target zene RowTableViewTest -j2 > build-2.log 2>&1` | 0 | test binary built, fix not yet applied |
| **RED** | `cd build/tests && ctest -R '^RowTableViewTest$' --output-on-failure > test-red-1.log 2>&1` | **8** | `FAIL! : RowTableViewTest::insertedRowsUseTheRowDelegate() Compared QObject pointers are not the same` (`RowTableViewTest.cpp:57`); `Totals: 3 passed, 1 failed`; `0% tests passed, 1 tests failed out of 1` |
| Fix applied, build | `CCACHE_TEMPDIR=... cmake --build build --target zene RowTableViewTest -j2 > build-3.log 2>&1` | 0 | |
| **GREEN** | `cd build/tests && ctest -R '^RowTableViewTest$' --output-on-failure > test-green-1.log 2>&1` | **0** | `100% tests passed, 0 tests failed out of 1` |
| Family binary, GREEN | `cd build/tests && QT_QPA_PLATFORM=offscreen ./RowTableViewTest > family-run-1.log 2>&1` | 0 | `Totals: 4 passed, 0 failed, 0 skipped` |
| Proof step 1: save fix | `git diff -- src/gui/RowTableView.cpp > fix.patch` | 0 | product diff saved |
| Proof step 2: revert fix | `git apply -R fix.patch` | 0 | |
| Proof step 3: rebuild reverted | `CCACHE_TEMPDIR=... cmake --build build --target zene RowTableViewTest -j2 > build-4.log 2>&1` | 0 | |
| **Proof: RED without fix** | `cd build/tests && ctest -R '^RowTableViewTest$' --output-on-failure > test-proof-revert-1.log 2>&1` | **8** | same failure as the first RED run (`RowTableViewTest.cpp:57`) |
| Proof step 4: re-apply fix | `git apply fix.patch` | 0 | |
| Proof step 5: rebuild | `CCACHE_TEMPDIR=... cmake --build build --target zene RowTableViewTest -j2 > build-5.log 2>&1` | 0 | |
| **Proof: GREEN with fix** | `cd build/tests && ctest -R '^RowTableViewTest$' --output-on-failure > test-proof-reapply-1.log 2>&1` | **0** | `100% tests passed, 0 tests failed out of 1` |

The affected family is `RowTableViewTest`: one ctest entry, 4 QTest results in the GREEN binary run. Its other slot, `clearingModelIsSafe` (new-p10-338), still passes.

RowTableView has no other user in this tree: `rg RowTableView` finds only `src/gui/CMakeLists.txt`, `src/gui/RowTableView.cpp` and `include/RowTableView.h`.

### Ratchet gates

| Gate | Command | EXIT with fix | EXIT on reverted baseline |
| --- | --- | --- | --- |
| Complexity | `bash tests/complexity-gate.sh --check > gate-complexity-1.log 2>&1` | **1** | 1 (`gate-complexity-base.log`) |
| File length | `bash tests/file-length-gate.sh --check > gate-filelength-1.log 2>&1` | **1** | 1 (`gate-filelength-base.log`) |

Both gates are **red on the branch base as well, and the failures are not caused by this change.** I ran them with the three changed files reverted (`full-change.patch`, then re-applied). The REGRESSION lines are identical in both runs: 6 complexity and 14 file-length lines, none naming `RowTableView`. They name other files, for example `tests/src/gui/XyPadPairTest.cpp` (CCN 16), `src/core/DawProjectReadTracks.cpp` (CCN 13), `src/core/ControlCommandsMixerTools.cpp` (CCN 12), `tests/src/gui/ArrangerTimelineTest.cpp` (689 lines) and others. Fixing them is outside this one-bug port, and the gate limits and baselines were not touched. These gates need an owner-level pass, which STATUS.md already lists as queued.

## Notes on the change

- The delegate is installed on the view as the default, which persists across `setModel`. Rows added later, and rows of any later model, use it.
- `~RowTableView` deletes `m_rowDelegate` while the base `QAbstractItemView` still records it as the default delegate. The test destroys the view with that default set and passes under offscreen Qt, so no crash was observed here. Row delegates had the same lifetime before this change. I did not run ASan.

## Files committed

- `src/gui/RowTableView.cpp`
- `tests/src/gui/RowTableViewTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-399-port.md`
