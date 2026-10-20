# Cluster new-p17-245: collection removal failures are silently ignored

**Verdict: FIXED**

The browser's collection context menu called `browser.collection_remove`, discarded its `ControlResult`, and refreshed the list. When the store refused the removal, the collection stayed listed with no explanation. The removal now lives in `BrowserSearchView::removeCollection`, which shows a refusal's `errorMessage` on the status line (`m_status`), as `saveCollection` already does for `browser.collection_save`. The list is still re-read afterwards, so a refused removal leaves its collection listed, which matches the store.

The menu's `QMenu::exec` is modal, so the regression calls `removeCollection` directly. Extracting that body was the only change made before the fix, and it is behaviour-preserving.

## Evidence

- Bug site, original code: `src/gui/BrowserSearchView.cpp` (historical 220-223): the menu lambda ran `run("browser.collection_remove", ...)` and `refreshCollections()` with no result check. The `saveCollection` path (`src/gui/BrowserSearchView.cpp:432-443`, error check at `:441`) shows its error.
- Store refusals that reach the view: `src/core/ControlCommandsBrowserCollections.cpp:134-143` (`remove`) returns `NotFound` when the name is missing (`:140`) and `Refused` from `readForEdit` when the file is damaged (`:85-91`, message "is not a JSON object ... left as it is").
- Fix: `src/gui/BrowserSearchView.cpp:445-450`. The menu now calls it at `:220`. Declaration: `include/BrowserSearchView.h:84-85`.
- Regression: `tests/src/gui/LibraryMenusTest.cpp:354-371` (`aRefusedCollectionRemovalSaysWhyOnTheStatusLine`). Scenario 1 is a stale list: "Snares" is removed behind the view's back, then `removeCollection` is refused with "no collection 'Snares'" (`:363`). Scenario 2 is a damaged store, refused with "is not a JSON object" (`:370`). `labelShows` helper at `:90`.

## Commands and exit codes

Run from the worktree unless stated. Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-245.r1/logs/`.

| Step | Command | EXIT | Result |
| --- | --- | --- | --- |
| build (first try) | `cmake --build build --target zene LibraryMenusTest -j4 > .../build-1.log 2>&1` | 2 | ccache could not create its temp file in `/run/user/1000/ccache-tmp` (permission denied). Not a code error. |
| build | `CCACHE_TEMPDIR=/tmp/ccache-tmp-new-p17-245 cmake --build build --target zene LibraryMenusTest -j4 > .../build-2.log 2>&1` | 0 | Builds with the seam, pre-fix behaviour. |
| **RED** | `cd build/tests && ctest -R '^LibraryMenusTest$' --output-on-failure > .../test-1-RED.log 2>&1` | **8** | `FAIL! : aRefusedCollectionRemovalSaysWhyOnTheStatusLine() ... returned FALSE. (a refused removal showed nothing)`, `Loc: [LibraryMenusTest.cpp(363)]`. Totals: 16 passed, 1 failed. |
| fix patch | `git diff -- src/gui/BrowserSearchView.cpp include/BrowserSearchView.h > .../fix.patch` | 0 | Unstaged diff is the fix only; the seam is staged. |
| build | `CCACHE_TEMPDIR=... cmake --build build --target zene LibraryMenusTest -j4 > .../build-3-GREEN.log 2>&1` | 0 | |
| **GREEN** | `cd build/tests && ctest -R '^LibraryMenusTest$' --output-on-failure > .../test-2-GREEN.log 2>&1` | **0** | `100% tests passed out of 1`. |
| **Proof: revert** | `git apply -R .../fix.patch` | 0 | |
| build | `... cmake --build build --target zene LibraryMenusTest -j4 > .../build-4-reverted.log 2>&1` | 0 | |
| **Proof: RED** | `cd build/tests && ctest -R '^LibraryMenusTest$' --output-on-failure > .../test-3-REVERTED-RED.log 2>&1` | **8** | Same assertion, `LibraryMenusTest.cpp(363)`. Totals: 16 passed, 1 failed. |
| **Proof: restore** | `git apply .../fix.patch` | 0 | |
| build | `... cmake --build build --target zene LibraryMenusTest -j4 > .../build-5-regreen.log 2>&1` | 0 | |
| **Proof: GREEN** | `cd build/tests && ctest -R '^LibraryMenusTest$' --output-on-failure > .../test-4-REGREEN.log 2>&1` | **0** | `100% tests passed out of 1`. |
| family build | `cmake --build build --target zene HelpAccessTest LibraryMenusTest -j4 > .../build-6-family.log 2>&1` | 0 | Rebuilds the header's includers: `HelpAccessTest` (own target), and `src/gui/shell/BrowserPanel.cpp` and `src/gui/BrowserResults.cpp` (in the `zene` target). |
| **Family** | `cd build/tests && ctest -R '^(HelpAccessTest\|LibraryMenusTest)$' --output-on-failure > .../test-5-family.log 2>&1` | **0** | `100% tests passed out of 2` (`ctest -N` lists 2 tests). |
| **Gate (direct)** | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > .../gates-1.log 2>&1` | 1 | Crashed before any check: `PermissionError` creating the log folder `zene-qwen-new-p17-245-feature-check` beside the worktree, which is outside the write-jail. The script was not changed. |
| **Gate (relocated copy)** | `python3 -I /tmp/fc-new-p17-245/feature_check_relocated.py <worktree> > .../gates-2-relocated.log 2>&1` | **0** | A copy of the same script with only its log folder (now under the run folder) and its `ROOT` (now the qwen folder, so `FIX-BASE` and the gate baseline cache are found) changed. Output: `RESULT: PASS`. All 8 rows PASS: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity ("no new item in your files"), file-length ("no new item in your files"). |

## Self-check

- **Reverted-fix RED, restored-fix GREEN (exit codes).** Reverted: `test-3-REVERTED-RED.log`, **EXIT=8**, fails at `LibraryMenusTest.cpp:363`. Restored: `test-4-REGREEN.log`, **EXIT=0**. The reverted patch is `fix.patch`, which holds the fix hunk only. The seam is staged and stays in both states.
- **Acceptance criteria.** None: this is a bug fix, not a feature row (`submit_check` marks the feature items N/A).
- **Undo and reversibility.** Unchanged. `browser.collection_remove` keeps its declared reversibility and "One undo step" (`src/core/ControlCommandsBrowserCollections.cpp:192-193`). The fix adds no command and no undo step, and the list is still re-read after every attempt.
- **Audio thread.** Nothing touched. Both changed functions are UI-thread code on the collections list (`BrowserSearchView.cpp:220`, `:445-450`); the store I/O is the existing command.
- **Old projects and saved data.** Not affected. The store format and its read and write paths are unchanged (`ControlCommandsBrowserCollections.cpp:61-96`). The project file is not touched.
- **Qt 5.15 and portability.** No new Qt API. The fix uses `QString`, `QStringLiteral` and a status `QLabel` that is already there. The test adds `QFile` and `QLabel`, which both exist in Qt 5.15.
- **Sibling commands.** The only remove path is the context menu (`BrowserSearchView.cpp:220`). `browser.collection_save` already shows its refusal (`BrowserSearchView.cpp:432-443`, check at `:441`). `rg` finds no other `browser.collection_remove` call in `src/`.
- **`submit_check.py` gate row.** Its line 104 runs `feature_check.py` in place, which fails for the same sandbox reason as above (`gates-1.log`, exit 1). The gates were run as the relocated copy (`gates-2-relocated.log`, exit 0).
- **Determinism (no wall-clock, sleeps or races).** The test has no timers, sleeps or waits. The seam is `BrowserSearchView::removeCollection` (`include/BrowserSearchView.h:84-85`), which the test calls directly, since the context menu's `QMenu::exec` is modal. The store is a fresh file in the test's `QTemporaryDir`, pointed at by `ZENE_BROWSER_COLLECTIONS` (`src/core/ControlCommandsBrowserCollections.cpp:53-57`).
- **Every caller and sibling checked.** Callers of the changed function: the menu (`BrowserSearchView.cpp:220`) and the test. Siblings: `saveCollection` (already checks, `:441`), and no other `browser.collection_remove` call in `src/`. The sibling commands in the command files were not changed, so none needed a change.
- **No collateral damage.** The fix adds no limit or check and changes no stored format. The change affects only the status line on a refused removal. A successful removal behaves as before, and the list is re-read in both cases, as before.
- **Verdict.** REAL, so the ALREADY-FIXED / FALSE-POSITIVE evidence does not apply.
