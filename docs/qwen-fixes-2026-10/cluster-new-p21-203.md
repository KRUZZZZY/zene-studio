Verdict: FIXED

new-p21-203: `CompSource::setValue` did not reject a negative row

Evidence: the finding is real. Before the fix, `CompSource::setValue` checked only `row >= rowCount()` ([src/gui/ItemListSurfaceSources.cpp:139](src/gui/ItemListSurfaceSources.cpp:139) in the fixed tree; the original line was 141), then called `segment(row)`, which is `m_strip->segments().at(static_cast<std::size_t>(row))` ([src/gui/ItemListSurfaceSources.cpp:195](src/gui/ItemListSurfaceSources.cpp:195)). A row of -1 becomes a huge `size_t`, so `std::vector::at` throws `std::out_of_range`. The fix adds the lower bound through a private helper, `hasRow(row)` ([src/gui/ItemListSurfaceSources.cpp:195](src/gui/ItemListSurfaceSources.cpp:195)), and `setValue` now returns `false` before any lookup ([src/gui/ItemListSurfaceSources.cpp:141](src/gui/ItemListSurfaceSources.cpp:141)). The helper keeps `setValue` at its original branch count; an inline `row < 0 ||` pushed it to CCN 11 and the complexity ratchet flagged it, so the check moved into the helper.

Reachability: latent. The only callers are `ItemListPanel::setData` ([src/gui/ItemListPanel.cpp:214](src/gui/ItemListPanel.cpp:214) guards `index.isValid()`, which rejects negative rows, and calls setValue at line 218) and `ItemListPanel::stepCurrent` (guards `current.isValid()`, calls setValue at [src/gui/ItemListPanel.cpp:416](src/gui/ItemListPanel.cpp:416)). No control command or other path reaches `CompSource::setValue` with a negative row today. The defect is in the function's own contract, so it was fixed there.

Regression test: `ItemListPanelTest::aNegativeCompRowIsRefusedNotIndexed` ([tests/src/gui/ItemListPanelTest.cpp:309](tests/src/gui/ItemListPanelTest.cpp:309)). It builds a real comp with one segment (track, two lanes, `comp.select` 10..11 on lane 0), calls `compSegments(&strip, &strip)->setValue(-1, 0, 5)` inside a `try`, and fails with `QFAIL` if `std::out_of_range` escapes ([tests/src/gui/ItemListPanelTest.cpp:328](tests/src/gui/ItemListPanelTest.cpp:328)). It then checks the call returned `false`, the row count is unchanged, and `comp.get_state` is identical to the snapshot taken before the call. The test file already existed and is already registered in `tests/CMakeLists.txt`; no new file was added.

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-203.r1/logs` (abbreviated below as `LOGS`).

## Evidence

| Step | Command (run from the worktree or `build/tests` as noted) | EXIT | Result |
| --- | --- | --- | --- |
| Build, first try | `cmake --build build --target ItemListPanelTest -j4 > LOGS/build-1.log 2>&1` | 2 | ccache could not create its temp file under the shared dsh path (`Permission denied`, `build-1.log`); not a code error |
| Build, ccache tempdir on disk | `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp cmake --build build --target ItemListPanelTest -j4 > LOGS/build-2.log 2>&1` | 0 | test built with the RED test present, product code unfixed |
| RED | `ctest -R '^ItemListPanelTest$' --output-on-failure > LOGS/test-red-1.log 2>&1` (in `build/tests`) | 8 | `FAIL! : ...aNegativeCompRowIsRefusedNotIndexed() a negative row reached the segment list and threw out_of_range`; `Totals: 18 passed, 1 failed` (`test-red-1.log`) |
| Fix applied, rebuild | `CCACHE_TEMPDIR=... cmake --build build --target zene ItemListPanelTest -j4 > LOGS/build-3.log 2>&1` | 0 | |
| GREEN, ctest | `ctest -R '^ItemListPanelTest$' --output-on-failure > LOGS/test-green-1.log 2>&1` | 0 | `100% tests passed out of 1` |
| GREEN, binary | `./ItemListPanelTest > LOGS/test-green-2.log 2>&1` (in `build/tests`) | 0 | `Totals: 19 passed, 0 failed` (18 existing + new slot) |
| 4b, patch captured | `git diff -- src/gui/ItemListSurfaceSources.cpp > LOGS/fix.patch` | 0 | |
| 4b, fix reverted | `git apply -R LOGS/fix.patch` | 0 | |
| 4b, rebuild unfixed | `CCACHE_TEMPDIR=... cmake --build build --target zene ItemListPanelTest -j4 > LOGS/build-8-unfixed.log 2>&1` | 0 | |
| 4b, RED with fix reverted | `./ItemListPanelTest > LOGS/test-4b-final-unfixed.log 2>&1` (in `build/tests`) | 1 | `FAIL! : ...aNegativeCompRowIsRefusedNotIndexed() a negative row reached the segment list and threw out_of_range`; `Totals: 18 passed, 1 failed` |
| 4b, fix re-applied | `git apply LOGS/fix.patch` | 0 | |
| 4b, rebuild fixed | `CCACHE_TEMPDIR=... cmake --build build --target zene ItemListPanelTest -j4 > LOGS/build-9-refixed.log 2>&1` | 0 | |
| 4b, GREEN with fix | `./ItemListPanelTest > LOGS/test-4b-final-fixed.log 2>&1` (in `build/tests`) | 0 | `Totals: 19 passed, 0 failed` |
| Affected family build | `CCACHE_TEMPDIR=... cmake --build build --target TakeLanesPanelTest ItemListButtonsTest -j4 > LOGS/build-6-family.log 2>&1` | 0 | |
| Affected family, final tree | `ctest -R 'ItemListPanelTest\|ItemListButtonsTest\|TakeLanesPanelTest' --output-on-failure > LOGS/test-final-family.log 2>&1` (in `build/tests`) | 0 | `100% tests passed out of 3` (ItemListPanelTest, ItemListButtonsTest, TakeLanesPanelTest) |

The 4b sequence was run twice: once on the first version of the fix (one-line change, `test-4b-unfixed.log` / `test-4b-fixed.log`, EXIT 1 / 0) and again on the final version with the helper (`test-4b-final-unfixed.log` / `test-4b-final-fixed.log`, EXIT 1 / 0). The table cites the final run.

### Gates

`python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` could not be run as written: it writes its logs to `<worktree>-feature-check/`, a sibling of the worktree outside the write-jail. The attempt (`LOGS/gates-1.log`) exited 1 with `PermissionError` at `feature_check.py:71`, before any gate ran. Its logic was not changed. Instead, each gate command it lists was run from the worktree with its log in `LOGS`:

| Gate | Command | EXIT | Note |
| --- | --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh > LOGS/gate-fork-sources-2.log` | 0 | |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh > LOGS/gate-all-sources-2.log` | 0 | |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh > LOGS/gate-no-upstream-3.log` | 0 | after the registry line below |
| check-namespace | `tests/scripted/check-namespace > LOGS/gate-check-namespace-2.log` | 0 | |
| no-tautology | `bash tests/no-tautology-gate.sh > LOGS/gate-no-tautology-2.log` | 0 | |
| doc-figures | `python3 tests/doc-figures-gate.py > LOGS/gate-doc-figures-2.log` | 0 | |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check > LOGS/gate-complexity-2.log` | 1 | 7 `REGRESSION` lines, all in files I did not touch; none mention `ItemListSurfaceSources`, `CompSource` or `ItemListPanelTest` (grep). Before the helper change the same run had 8 lines, one of them `CompSource::setValue` at CCN 11 (`LOGS/gate-complexity.log`); that item is gone now. |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check > LOGS/gate-file-length-2.log` | 1 | 19 `REGRESSION` lines, none in files I touched (grep for `ItemList` and `CompSource` returns nothing) |

The ratchets exit 1 because of repo-wide items, which the task says are red on the train. The condition that matters, "no new item in your changed files", holds for both: no item in the two ratchet logs names a file this change touches.

## Files committed

- `src/gui/ItemListSurfaceSources.cpp`: `setValue` row check, `hasRow` helper (3 lines changed).
- `tests/src/gui/ItemListPanelTest.cpp`: `#include <stdexcept>` and the regression slot `aNegativeCompRowIsRefusedNotIndexed` (27 lines added). No existing assertion changed.
- `tests/upstream-modifications.txt`: one reason line for the test file, which was already listed under an earlier reason.
- `docs/qwen-fixes-2026-10/cluster-new-p21-203.md`: this report.

## Sibling and caller check

- Callers: `ItemListPanel::setData` ([src/gui/ItemListPanel.cpp:214](src/gui/ItemListPanel.cpp:214), `index.isValid()`) and `ItemListPanel::stepCurrent` ([src/gui/ItemListPanel.cpp:416](src/gui/ItemListPanel.cpp:416), `current.isValid()`). Both already pass only model rows >= 0; the change does not alter any valid-row path.
- The other `CompSource` accessors, `value` and `rowId`, also index with `segment(row)` without a lower bound. They are called by the model for valid rows only, so they were not changed.
- `ClipsSource::setValue` ([src/gui/ItemListSurfaceSources.cpp:255](src/gui/ItemListSurfaceSources.cpp:255)-257) has the same missing lower bound (`row >= rowCount() || m_clips.at(row) == nullptr`), but `QList::at` asserts instead of throwing, so a negative row is undefined behaviour rather than an exception. I did not change it: a deterministic RED test for it would need an assertion or UB-dependent check, and the finding is scoped to `CompSource`. `ClipsSource::insertAfter` at line 280 already checks `row < 0`. This is flagged for a follow-up.
- `ItemListNoteSources.cpp:131` and `:241` check only the upper bound before indexing; `ItemListAutomationSource.cpp:90` checks both. Not changed; out of this finding.
- `XySource::setValue` does no row indexing (only `row == 1` selects the macro), so no throw path.

## Self-check

1. **The regression test fails with the fix reverted and passes with it (exit codes).** Yes. Reverted: `./ItemListPanelTest` exit 1, `FAIL!` at the `QFAIL` line, `Totals: 18 passed, 1 failed` (`test-4b-final-unfixed.log`). Restored: exit 0, `Totals: 19 passed, 0 failed` (`test-4b-final-fixed.log`). The first RED run before any fix is `test-red-1.log`, ctest exit 8 with the same failure.
2. **Deterministic, no timing, sleeps or races.** The test uses fixed inputs (a track with two lanes, `comp.select` 10..11, row -1, value 5) and compares `comp.get_state` before and after. It uses no clock, no sleep, no signal, no threads and no event-loop waiting. The only dependence is on the comp state the test sets up itself.
3. **Fix is at the cause; every caller and sibling was checked.** The sibling check is in the section above. The fix is at the one unchecked lower bound in the function the finding names; the valid-row path is unchanged. The `ClipsSource` sibling is flagged, not fixed, for the reason given.
4. **No collateral damage.** The change only turns a call that used to throw into a call that returns `false`. Valid rows, upper-bound refusals, and every other branch behave as before. No limit or threshold was added, so nothing legitimate can hit a new limit. Saved projects are not read or written by this code path; no control command or undo step was added; the code runs on the UI thread and not on the audio thread.
5. **Verdict is FIXED, not ALREADY-FIXED or FALSE-POSITIVE,** so no "already fixed" evidence is needed.
