IMPLEMENTED

# Cluster Q050-170 — Filter browser results by category

Every acceptance criterion is met and proven by a test run in this session. All ctest runs
were executed from `<worktree>/build/tests`; log files are in
`~/.local/share/zene-bunny-fix/qwen/runs/Q050-170.r3/logs/`. The branch started empty at
`de5a85a96` (train 5); the work resumed from round 2's saved patch, fixed two latent bugs
in it (dangling `QJsonObject` temporaries in `BrowserTagStore::load`; test fixtures outside
every library root), and was completed and committed in this session.

## Acceptance table

| # | Acceptance criterion | Exact command | EXIT | What the run shows |
|---|----------------------|---------------|-----:|--------------------|
| 1 | The Search view exposes a category filter populated from catalog metadata beside the existing kind and source filters | `ctest -R "^LibraryMenusTest$" --output-on-failure > .../logs/test-LibraryMenusTest-final.log 2>&1; echo EXIT=$?` | 0 | slot `theCategoryFilterSitsBesideKindAndSourceAndNarrowsBothQueryModes`: `categoryFilter()` exists with accessible name "Category"; the top row's combos are exactly `Kind, Where to look, Category` in that order; the choices are `Any category` + the two categories assignments put in the catalog |
| 1 | Category assignment makes a category available | `ctest -R "^BrowserTagMenuTest$" --output-on-failure > .../logs/test-BrowserTagMenuTest-final.log 2>&1; echo EXIT=$?` | 0 | slot `theCategoryMenuAssignsOneCategoryAndClearsIt`: `New category...` assigns through `browser.category.set`, the vocabulary and the exclusive checks read back the store, `No category` clears |
| 2 | Selecting a category filters matching results in both direct and indexed search; Any category restores the full result set | `ctest -R "^ControlBrowserCommandsTest$" --output-on-failure > .../logs/test-ControlBrowserCommandsTest-final.log 2>&1; echo EXIT=$?` | 0 | slot `directAndIndexedQueriesAgreeOnTheCategoryFilter`: the same fixture subset in `browser.query` and `browser.index_query` for each category and for the absent argument; items carry `category` on the wire; slot `categoryCommandsWriteTheStoreAndUndoThroughTheRecordedInverse` proves both verbs' A16 inverses through `control.undo` |
| 2 | direct-query facet semantics (narrow, case-insensitive, empty = any, AND with tags) | `ctest -R "^BrowserCatalogQueryTest$" --output-on-failure > .../logs/test-BrowserCatalogQueryTest-final.log 2>&1; echo EXIT=$?` | 0 | slot `categoryNarrowsTheQueryAndEmptyMeansAny` |
| 2 | the GUI facet narrows the list in both query modes and Any restores it | same `ctest -R "^LibraryMenusTest$"` run as row 1 | 0 | `resultCount()` 2 → 1 → 1 → 2 direct; 2 → 1 → 2 with **Whole library** checked; 2 again on uncheck |
| 3 | A GUI regression check covers category assignment, filtering, and saved-collection round-trip | `ctest -R "^LibraryMenusTest$" --output-on-failure > .../logs/test-LibraryMenusTest-final.log 2>&1; echo EXIT=$?` | 0 | slot `aCategoryFilteredSearchSavesAndRunsAsACollection`: assigns via the Category submenu, filters to it, saves the filtered search through the view's own button (dialog answered by timer), finds the collection listed with `query.category == "Drums"`, runs it from the list and sees the same one-result set |
| 3 | saved-collection round-trip at command level (category kept, follows the library) | `ctest -R "^LibraryProjectsTest$" --output-on-failure > .../logs/test-LibraryProjectsTest-final.log 2>&1; echo EXIT=$?` | 0 | slot `aSavedCollectionKeepsItsCategoryFilter` |

## Surface and mechanics

| Step | Exact command | EXIT |
|---|---|---:|
| Build (product + all test targets above) | `cmake --build build --target zene BrowserCatalogQueryTest ControlBrowserCommandsTest BrowserTagMenuTest LibraryMenusTest LibraryProjectsTest ReversibilityContractTest ControlResultSchemaTest ControlRegistryTest -- -j4 > .../logs/build-final.log 2>&1; echo EXIT=$?` | 0 |
| Commands snapshot regenerated from a live instance (831 ids / 76 groups, `wasm.*` excluded, as train 5 keeps it) | `python3 /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/research/reviews/m1-session-2026-10-12/scratch/m0/snapshot_live.py $PWD > .../logs/snapshot-1.log 2>&1; echo EXIT=$?` | 0 |
| Snapshot equals the live binary | `ctest -R "^ControlCommandsSnapshot$" --output-on-failure > .../logs/test-ControlCommandsSnapshot-final.log 2>&1; echo EXIT=$?` | 0 |
| A16 histogram measured (part-2 probe run directly; the script's part 1 is red at base on the stale `DawProjectZipRead.cpp` TU list, pre-existing and untouched) | `... the part-2 recipe: c++ ${FLAGS[@]} src/core/ControlReversibilityTable*.cpp + tools/dawproject-a16-histogram.cpp && ./hist > .../logs/a16work` | 0 (`HIST_EXIT=0`; `MEASURED rows=831 true_inverse=416 snapshot=41 irreversible=22 not_mutating=352`, `DECLARED rows=831 entries=831 duplicates=0` on this wasm-off build; published block moves +3 rows to `rows=840 snapshot=44 not_mutating=357`) |
| Published A16 figure equals the measured table | `ctest -R "^ReversibilityContractTest$" --output-on-failure > .../logs/test-ReversibilityContractTest-final.log 2>&1; echo EXIT=$?` | 0 |
| Canary figures | `python3 tests/doc-figures-gate.py > .../logs/doc-figures-final.log 2>&1; echo EXIT=$?` | 0 (`snapshot measures 831 ids / 76 groups`, `ok README.md`) |
| Full-surface contract tests | `ctest -R "^ControlResultSchemaTest$" --output-on-failure > .../logs/test-ControlResultSchemaTest-final.log 2>&1; echo EXIT=$?` | 0 |
| Full-surface registry test | `ctest -R "^ControlRegistryTest$" --output-on-failure > .../logs/test-ControlRegistryTest-final.log 2>&1; echo EXIT=$?` | 0 |
| Gate-1 checked log pickup (the three new ids reach a schema-checked success) | `QT_QPA_PLATFORM=offscreen ZENE_CONTROL_CHECKED_LOG=.../logs/checked-partial.log ctest -R "BrowserCatalogQueryTest\|ControlBrowserCommandsTest\|BrowserTagMenuTest\|LibraryMenusTest\|LibraryProjectsTest"; grep -c "browser.categor" .../logs/checked-partial.log` | 0 (`browser.categories`, `browser.category.set`, `browser.category.clear` all present; run exit 0) |

## Gates (final tree, before each commit)

| Gate | Exact command | EXIT | Note |
|---|---|---:|---|
| 4 complexity | `bash tests/complexity-gate.sh --check > .../logs/gate-complexity-final.log 2>&1; echo EXIT=$?` | 1 | red on train 5 on purpose; none of my files/functions newly listed — the REGRESSION function set diffs byte-identical to `../zene-cachyos` (`IDENTICAL_FUNCTION_SET`); my earlier `BrowserTagStore::load` CCN-12 entry was removed by splitting `readCategoryTable` out |
| 7 file length | `bash tests/file-length-gate.sh --check > .../logs/gate-filelength-final.log 2>&1; echo EXIT=$?` | 1 | red on train 5 on purpose; 0 of my files in the over-500 list or its REGRESSION lines |
| 9 fork sources | `bash tests/fork-sources-gate.sh > .../logs/gate-forksources-final.log 2>&1; echo EXIT=$?` | 0 | |
| all-sources reproduce | `bash tests/all-sources-reproduce.sh > .../logs/gate-allsources-final.log 2>&1; echo EXIT=$?` | 0 | |
| 6 no upstream regression | `bash tests/no-upstream-regression-gate.sh > .../logs/gate-noupstream-final.log 2>&1; echo EXIT=$?` | 0 | every changed product file is fork-NEW in `tests/fork-sources.txt`; tests/, tools/ and *.md allowed |
| namespace | `tests/scripted/check-namespace > .../logs/gate-namespace-final.log 2>&1; echo EXIT=$?` | 0 | |
| no tautology | `bash tests/no-tautology-gate.sh > .../logs/gate-tautology-final.log 2>&1; echo EXIT=$?` | 0 | |

## Known limits

- One category per file; vocabulary = what files carry (no standalone category manager).
- Categories are user-library state (`browser-tags.json`, optional `"categories"` table);
  old stores and old collections load unchanged. No project-format change.
- The Category submenu is in the Search source's row menu, not the file-list tabs'.
- Indexed mode reads the category live from the store (like tags), not from the index.
- `tools/dawproject-proof.sh` part 1 is red at the base (stale TU list after the
  `DawProjectZipRead.cpp` split) — pre-existing, not changed here; its part 2 recipe was
  run directly for the A16 measurement (exit 0).
- Local TSan does not run on this box; no audio-thread path was touched (catalog/GUI/
  control thread only), and store edits serialize through the registry's UI-thread
  dispatch.

## Files committed

- Core: `include/BrowserCatalog.h`, `include/ControlBrowserSupport.h`,
  `include/ControlRegistryGroups.h`, `src/core/BrowserCatalog.cpp`,
  `src/core/BrowserTagStore.cpp`, `src/core/ControlBrowserSupport.cpp`,
  `src/core/ControlCommandsBrowser.cpp`, `src/core/ControlCommandsBrowserCollections.cpp`,
  `src/core/ControlCommandsBrowserIndex.cpp`, `src/core/ControlCommandsBrowserTags.cpp`,
  `src/core/ControlReversibilityTablePassive.cpp`,
  `src/core/ControlReversibilityTableSnapshot.cpp`
- GUI: `include/BrowserSearchView.h`, `include/BrowserTagMenu.h`,
  `src/gui/BrowserSearchView.cpp`, `src/gui/BrowserTagMenu.cpp`
- Tests: `tests/src/core/BrowserCatalogQueryTest.cpp`,
  `tests/src/core/ControlBrowserCommandsTest.cpp`,
  `tests/src/core/LibraryProjectsTest.cpp`, `tests/src/gui/BrowserTagMenuTest.cpp`,
  `tests/src/gui/LibraryMenusTest.cpp`
- Surface/docs: `tools/mcp-zene-control/zene_control/commands_snapshot.json`,
  `README.md`, `docs/RELEASE-NOTES-v0.3.0-alpha.md`, `data/manual/index.html`,
  `docs/050-features/Q050-170.md`, `docs/qwen-fixes-2026-10/cluster-Q050-170.md`
