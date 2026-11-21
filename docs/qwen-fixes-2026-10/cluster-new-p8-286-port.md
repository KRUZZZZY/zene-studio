# Cluster new-p8-286-port

Verdict: **FIXED** (port of `68efebd89` from `bugs/qwen-new-p8-286` onto `050/m0-train-5`, written by hand, not merged or cherry-picked)

## Finding and evidence

`browserKindForPath` in `src/core/BrowserCatalog.cpp:369-377` (pre-fix) read only `QFileInfo::suffix()`. For `standard.cs.xml` that is `xml`, which is not in the kind table, so the result was `BrowserKind::Other`. The browser's own Presets filter lists `*.cs.xml` (`src/gui/shell/BrowserPanel.cpp:323`), so the classification disagreed with the browser UI.

The function is the single classifier for both paths the finding names:

- indexing: `src/core/BrowserIndex.cpp:148` calls `browserKindForPath(info.fileName())`
- querying: `src/core/ControlCommandsBrowserMedia.cpp:309`, `src/core/ControlCommandsLibraryClips.cpp:326`, and `src/core/BrowserCatalog.cpp:445`

Fixing the function fixes both.

**Port note:** on the original branch the kind assertions were in `tests/src/core/BrowserCatalogTest.cpp`. On this tree they live in `tests/src/core/BrowserCatalogQueryTest.cpp` (`kindsAndRootsAreTheBrowsersOwn`, lines 157-165), which is why the old commit conflicted. The regression was therefore placed there. `BrowserCatalogTest.cpp` is unchanged.

## Change

- `src/core/BrowserCatalog.cpp:369-381`: before the suffix lookup, a file name ending in `.cs.xml` (case-insensitive) returns `BrowserKind::Preset`.
- `tests/src/core/BrowserCatalogQueryTest.cpp:163-164`: adds `/tmp/standard.cs.xml` → `Preset` (the regression) and `/tmp/a.xml` → `Other` (negative control, so plain XML is not swept in).

No new files, so nothing to register. Both changed files are already in `tests/fork-sources.txt` (`src/core/BrowserCatalog.cpp`, `tests/src/core/BrowserCatalogQueryTest.cpp`).

## Evidence

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-286-port.r1/logs/`. CTest ran from `build/tests`. All builds used `CCACHE_TEMPDIR=/tmp/ccache-tmp-qwen-p8-286` (see the first build below).

| Phase | Exact command | Exit | Result |
| --- | --- | --- | --- |
| Build attempt 1 | `cmake --build build --target zene BrowserCatalogQueryTest -j2 > .../build-red-1.log 2>&1` | 2 | ccache could not create a temp file under `/run/user/1000` (permission denied). Environment failure, not code. |
| RED build | `CCACHE_TEMPDIR=/tmp/ccache-tmp-qwen-p8-286 cmake --build build --target zene BrowserCatalogQueryTest -j2 > .../build-red-2.log 2>&1` | 0 | built, test edit only |
| **RED test (before fix)** | `ctest -R '^BrowserCatalogQueryTest$' --output-on-failure > .../test-red-1.log 2>&1` | **8** | `kindsAndRootsAreTheBrowsersOwn` FAILED at `BrowserCatalogQueryTest.cpp:163`: `Actual (browserKindForPath("/tmp/standard.cs.xml")): 4`, `Expected (BrowserKind::Preset): 1`. Totals 6 passed, 4 failed (3 others are the baseline in the tag-store note below). |
| GREEN build | `CCACHE_TEMPDIR=... cmake --build build --target zene BrowserCatalogQueryTest -j2 > .../build-green-1.log 2>&1` | 0 | built with fix |
| GREEN test | `ctest -R '^BrowserCatalogQueryTest$' --output-on-failure > .../test-green-1.log 2>&1` | 8 | `kindsAndRoots...` now passes; Totals 7 passed, 3 failed (same 3 baseline failures) |
| Negative control: save patch | `git diff -- src/core/BrowserCatalog.cpp > .../fix.patch` | 0 | |
| Negative control: remove fix | `git apply -R .../fix.patch` | 0 | |
| Negative control build | `cmake --build build --target zene BrowserCatalogQueryTest -j2 > .../build-neg-1.log 2>&1` | 0 | |
| **Negative control test** | `ctest -R '^BrowserCatalogQueryTest$' --output-on-failure > .../test-neg-1.log 2>&1` | **8** | `kindsAndRoots...` FAILED at `:163`, `Actual ... 4` (fails again with the same values) |
| Restore fix | `git apply .../fix.patch` | 0 | |
| Restored build | `cmake --build build --target zene BrowserCatalogQueryTest -j2 > .../build-green-2.log 2>&1` | 0 | |
| Restored test | `ctest -R '^BrowserCatalogQueryTest$' --output-on-failure > .../test-green-2.log 2>&1` | 8 | 7 passed, 3 failed (baseline); kind test passes |
| Family build | `cmake --build build --target BrowserCatalogTest BrowserTagMenuTest -j2 > .../build-family-1.log 2>&1` | 0 | |
| Family test | `ctest -R '^(BrowserCatalogTest\|BrowserCatalogQueryTest\|BrowserTagMenuTest)$' --output-on-failure > .../test-family-1.log 2>&1` | 8 | 3 tests executed: `BrowserCatalogTest` Passed, `BrowserTagMenuTest` Passed, `BrowserCatalogQueryTest` 7/10 (3 baseline failures) |
| Complexity gate (final tree) | `bash tests/complexity-gate.sh --check > .../gate-complexity-1.log 2>&1` | **1** | regressions listed below; none in the files touched |
| File-length gate (final tree) | `bash tests/file-length-gate.sh --check > .../gate-filelength-1.log 2>&1` | **1** | regressions listed below; none in the files touched |
| Complexity gate (fix and test reverted) | `git apply -R .../full.patch`, then `bash tests/complexity-gate.sh --check > .../gate-complexity-base.log 2>&1` | **1** | identical REGRESSION lines to the final tree (`diff` empty) |
| File-length gate (fix and test reverted) | `bash tests/file-length-gate.sh --check > .../gate-filelength-base.log 2>&1` | **1** | identical REGRESSION lines to the final tree (`diff` empty); `git apply .../full.patch` restored the diff (exit 0) |

Tests executed by the affected query binary: 10 test functions (7 pass, 3 baseline failures). Family ctest count: 3 targets.

### Pre-existing failures, not caused by this change

1. **Tag-store cases in `BrowserCatalogQueryTest`** (`theQueryMatchesNameTagAndMetadata` at `:135`, `theStoreKeyIsCanonical` at `:203`, `scanCountCoversWhatTheTagFilterRejected` at `:217`). They fail with the production fix removed as well as with it (test-red-1.log), so they fail on this tree without the change. `BrowserTagStore::add` returns false because `save()` fails (`src/core/BrowserTagStore.cpp:173-200`), which writes to `ConfigManager::workingDir()/browser-tags.json`. Hypothesis checked and ruled out: a missing `build/tests/zene-workspace` directory (created and removed for the check, `test-diag-workspace.log`, still 3 failures). The actual write error is not printed by the test, so the cause is **not confirmed**.
2. **Gates.** Both ratchet gates exit 1 on this tree with and without this change. Regressions include `XyPadPairTest.cpp` (CCN 16), `DawProjectReadTracks.cpp` `parseNotes` (CCN 13), `ControlCommandsMixerTools.cpp` `adjust`, `ControlCommandsMixerStrip.cpp` `setStrip`, `ProjectRevisions.cpp` `rotateProjectRevision`, `ControlCommandsShortcuts.cpp` `importedOverrides`, and new files over 500 lines (`VideoTrackTest.cpp`, `ArrangerTimelineTest.cpp`, `TakeLane.cpp`, and others). None is in a file this change touches. Baselines were not changed.

## Files committed

- `src/core/BrowserCatalog.cpp`
- `tests/src/core/BrowserCatalogQueryTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-286-port.md`
