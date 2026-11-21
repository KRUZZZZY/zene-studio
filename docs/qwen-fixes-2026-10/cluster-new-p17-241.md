FIXED - cluster new-p17-241: a failed index save no longer replaces the in-memory index

# Cluster new-p17-241: a failed index save still replaces the in-memory index

**Verdict: FIXED**

## Evidence

The finding is real on the base tree. `BrowserIndex::scan()` computed `saved` and then published the new entries unconditionally:

- Before the fix: `src/core/BrowserIndex.cpp` assigned `m_entries = std::move(walk.found)` and `m_status = walk.status` whatever `saved` was. A failed write only set `walk.status.error` (the old line 309), so the new set went live in the session while the store kept the old file.
- After the fix: `src/core/BrowserIndex.cpp:301` computes `saved`; `:309-314` returns without publishing when the save failed (`running` cleared, the error reported, the previous `m_entries` and status fields kept, the same pattern as the cancel branch at `:304-308`); `:316-317` publishes only after a successful save.

Callers checked (all go through `scan()`, so one publish point):
- `BrowserIndex::start()` runs `scan()` on a worker thread and `reportScanEnd()` reports `Failed` through the task area when `status().error` is set.
- `BrowserIndex::scanNow()` is used by `reindex` with `wait` (`src/core/ControlCommandsBrowserIndex.cpp:99`, its response is `statusJson()`, which carries `error` at `:85-87`), and by `tests/src/core/LibraryBrowserTest.cpp:520` and `tests/src/core/BrowserCatalogQueryTest.cpp:505,546,552`.
- No code compares the error text. `grep "could not be written"` over `src include tests` finds only the one line in `BrowserIndex.cpp`.

The contract is the one stated in the header: `include/BrowserIndex.h:85-86` now says a scan replaces the index "when it finishes and its store is written", and a scan whose store cannot be written "keeps the previous index and reports the error". The fix chooses the first option the finding offered (publish only after a successful save) over a separate volatile index. The volatile option would need a new in-memory state that no caller asks for.

## Regression test

`tests/src/core/BrowserCatalogQueryTest.cpp:522-562`, slot `failedIndexSaveKeepsThePreviousIndex` (function at `:524`):
- Injected failure, deterministic: the store path is `<temp>/blocker/index.json` where `<temp>/blocker` is a regular file, so `QDir().mkpath` and `QSaveFile::open` fail with no dependence on permissions or user id. The store is injected through `ZENE_BROWSER_INDEX`, as the sibling tests do.
- Scan 1 (good store) publishes `first.wav`. Then a second file is added, the store is pointed at the unwritable path, and `scanNow` runs synchronously. The test asserts that the error is reported, `running` is false, and `entries()` still holds only `first.wav`.
- "Restart": the store is pointed back at the good file and `reload()` is called. The entries must again be only `first.wav`, which is what is on disk.
- No sleeps, no timing, no threads: `scanNow` is synchronous.

## Commands and exit codes

Environment note: the first build failed inside ccache (`ccache: error: failed to create temporary file for /run/user/1000/ccache-tmp/...: Permission denied`, `logs/build-1.log`, EXIT=2). Every later build used `CCACHE_TEMPDIR=/tmp/ccache-p17-tmp`, which is outside the product code.

| Step | Command (run from the worktree or `build/tests` as noted) | Log | EXIT |
| --- | --- | --- | --- |
| build (env failure, not code) | `cmake --build build --target BrowserCatalogQueryTest -j4` | `build-1.log` | 2 |
| RED build (test added, product unchanged) | `CCACHE_TEMPDIR=/tmp/ccache-p17-tmp cmake --build build --target BrowserCatalogQueryTest -j4` | `build-2.log` | 0 |
| RED test | `cd build/tests && ctest -R '^BrowserCatalogQueryTest$' --output-on-failure` | `test-red-1.log` | 8 |
| GREEN build (fix + header) | `CCACHE_TEMPDIR=/tmp/ccache-p17-tmp cmake --build build --target zene BrowserCatalogQueryTest BrowserFeaturesWindowTest LibraryBrowserTest -j4` | `build-3.log` | 0 |
| GREEN family | `cd build/tests && ctest -R '^(BrowserCatalogQueryTest\|BrowserFeaturesWindowTest\|LibraryBrowserTest)$' --output-on-failure` | `test-green-1.log` | 8 (see below) |
| 4b save patch | `git diff -- include/BrowserIndex.h src/core/BrowserIndex.cpp > logs/fix.patch` | `fix.patch` | 0 |
| 4b reverse | `git apply -R logs/fix.patch` | - | 0 |
| 4b reversed build | `CCACHE_TEMPDIR=... cmake --build build --target zene BrowserCatalogQueryTest -j4` | `build-4-reversed.log` | 0 |
| 4b reversed test | `cd build/tests && ctest -R '^BrowserCatalogQueryTest$' --output-on-failure` | `test-4b-reversed.log` | 8 (new test FAILS at line 555) |
| 4b re-apply | `git apply logs/fix.patch` | - | 0 |
| 4b re-applied build | `CCACHE_TEMPDIR=... cmake --build build --target zene BrowserCatalogQueryTest -j4` | `build-5-reapplied.log` | 0 |
| 4b re-applied test | `cd build/tests && ctest -R '^BrowserCatalogQueryTest$' --output-on-failure` | `test-5-reapplied.log` | 8 (new test PASSES; the two baseline cases below still fail) |

Test results, from the logs:
- RED (`test-red-1.log`): `FAIL! BrowserCatalogQueryTest::failedIndexSaveKeepsThePreviousIndex() Compared lists have different sizes. Actual (names()) size: 2, Expected (published) size: 1, Loc: BrowserCatalogQueryTest.cpp(555)`. Totals: 13 passed, 3 failed.
- Reversed (`test-4b-reversed.log`): the same assertion at the same line.
- GREEN (`test-green-1.log`): `PASS : failedIndexSaveKeepsThePreviousIndex()`. `LibraryBrowserTest` and `BrowserFeaturesWindowTest` pass (`Test #280` and `#281`). Totals for `BrowserCatalogQueryTest`: 14 passed, 2 failed.
- Re-applied (`test-5-reapplied.log`): `PASS : failedIndexSaveKeepsThePreviousIndex()`, the same 14 passed and 2 failed.

Family test count: 3 test executables (`BrowserCatalogQueryTest`, `BrowserFeaturesWindowTest`, `LibraryBrowserTest`), and 1 new slot in the first of them. `ctest` reported `67% tests passed, 1 tests failed out of 3`, where the one failure is `BrowserCatalogQueryTest` (its two baseline cases below).

Pre-existing failures, not caused by this change, not fixed here:
- `BrowserCatalogQueryTest::theQueryMatchesNameTagAndMetadata` (`BrowserCatalogQueryTest.cpp:146`, `matched` 0, expected 1) and `categoryNarrowsTheQueryAndEmptyMeansAny` (`:181`, `setCategory` returned false). They fail in the RED run as well (`test-red-1.log:10,12`), where the product code was unchanged, so they fail on this tree without the change. The recorded baseline for the same tag-store cases is `docs/qwen-fixes-2026-10/cluster-new-p8-286-port.md:54`, whose cause it did not confirm. They use `BrowserTagStore`, not the index.

## Gates

The gate script `feature_check.py` could not finish in this sandbox. It writes its logs to `<worktree>/../<name>-feature-check/` (`feature_check.py:71`, `logs.mkdir`), which is outside the write-jail, and failed with `PermissionError` (`logs/gates-1.log`, EXIT=1). I did not change the script or its baselines. I ran each gate command the script lists, from the worktree, with its output in the LOG FOLDER:

| Gate | Command | Log | EXIT | Result |
| --- | --- | --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | `gate-fork-sources.log` | 0 | PASS |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | `gate-all-sources.log` | 0 | PASS |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | `gate-no-upstream.log` | 0 | PASS |
| check-namespace | `tests/scripted/check-namespace` | `gate-check-namespace.log` | 0 | PASS |
| no-tautology | `bash tests/no-tautology-gate.sh` | `gate-no-tautology.log` | 0 | PASS |
| doc-figures | `python3 tests/doc-figures-gate.py` | `gate-doc-figures.log` | 0 | PASS |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | `gate-complexity.log` | 1 | PASS by the script's rule: the 5 `REGRESSION` lines name other files |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | `gate-file-length.log` | 1 | PASS by the script's rule: the 15 `REGRESSION` lines name other files |

The ratchet rule is "fail only on items NEW in files the branch changed". The changed files are `include/BrowserIndex.h`, `src/core/BrowserIndex.cpp` and `tests/src/core/BrowserCatalogQueryTest.cpp`. `rg "BrowserIndex|BrowserCatalogQueryTest" gate-complexity.log gate-file-length.log` finds no match (EXIT=1). The listed items are in `TakeLane.cpp`, `XyPadPairTest.cpp`, `DawProjectReadTracks.cpp` and the other files the loop called red on purpose. The ratchet baselines were not touched.

## Self-check

1. **The regression test fails with the product change reverted and passes with it.** Yes. RED: `test-red-1.log` (EXIT 8, line 555). Reverted with `git apply -R` on the saved `fix.patch`: `test-4b-reversed.log` (EXIT 8, line 555). Re-applied: `test-5-reapplied.log` shows PASS for the new slot.
2. **Deterministic.** No wall clock, sleep or thread. The failing save is made by a regular file where the store folder would be, so it does not depend on permissions or root. The store is injected through `ZENE_BROWSER_INDEX`. The scan is synchronous (`scanNow`). The restart is simulated with `reload()`.
3. **At the cause, complete.** The publish step is the only place entries become visible; the change is there (`src/core/BrowserIndex.cpp:309-317`). Every caller goes through `scan()` (see Evidence). The cancel branch already kept the previous index, and the failed-save branch now follows the same rule. The error reaches the `reindex` response and the task area. One sibling is not restored: `m_status.dirs` is set by `claim()` (`src/core/BrowserIndex.cpp:250`) to the folders of the scan that failed, so after a failed save `status().dirs` names those folders while `entries()` still holds the older index. The error string says the previous index is kept. I left this alone because `dirs` is also the running scan's progress display; this is an open point, not a hidden change.
4. **No collateral damage.** The store file format and `loadLocked()` are untouched, so old stores load exactly as before. A successful save goes through the same assignments as before (the `walk.status.error` line that could only have been empty on that path is gone). Behaviour changes only when the save fails: queries keep the index the store holds instead of entries that are not on disk. No limit or check was added. The error text gains `; the previous index is kept`, and nothing matches the old text.
5. **Realtime, undo, commands.** The scan runs on the worker pool or synchronously in the caller, never on the audio thread. No new control command, so `ControlCommandsSnapshot` and its canary are unchanged. Undo is not involved.
6. **Registration and gates.** No new file, so `tests/fork-sources.txt` and `tests/upstream-modifications.txt` need no entry. The gate table above; `feature_check.py` itself could not write its logs in this sandbox (`PermissionError`), so its single table was not produced.

## Files committed

- `src/core/BrowserIndex.cpp`
- `include/BrowserIndex.h` (comment only: the contract sentence on the failed save)
- `tests/src/core/BrowserCatalogQueryTest.cpp` (new slot `failedIndexSaveKeepsThePreviousIndex`)
- `docs/qwen-fixes-2026-10/cluster-new-p17-241.md` (this report)
