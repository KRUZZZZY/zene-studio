# Cluster new-p8-287-port: LV2 preset bundles are never returned by browser searches

**Verdict: FIXED** (ported by hand onto `050/m0-train-5` from `bugs/qwen-new-p8-287`, commit `cf75740d2`).
Not ALREADY-FIXED: on this branch an LV2 bundle is still walked into and never returned, before the change.

## Evidence (before the fix, current tree)

- `src/core/BrowserCatalog.cpp` `readDirectory`: a directory entry is only queued for descent
  (`walkMayDescend`) or counted as truncated (`skippedForDepth`). Nothing is appended for it, so
  `MySynth.lv2` never reaches the result; its `manifest.ttl` does (the `.ttl` file is kind `other`).
- `src/core/BrowserIndex.cpp` `BrowserIndex::scan`: `QDirIterator(dir, QDir::Files | QDir::Readable,
  QDirIterator::Subdirectories)` lists files only, so the bundle directory is never indexed while its
  files are.
- The kind table maps `lv2` to `BrowserKind::Preset` (`src/core/BrowserCatalog.cpp` `kKinds`), so the
  Presets source does include bundles; the walkers were the only obstacle.

## The fix

- `include/BrowserCatalog.h` / `src/core/BrowserCatalog.cpp`: new `browserIsLv2Bundle(const QFileInfo&)`:
  a non-symlinked directory whose suffix is `lv2` (case-insensitive). Shared by both walkers so the
  rule has one definition. Forward-declares `QFileInfo` to avoid a new include.
- `readDirectory` (query walk): a bundle is appended to the results and not queued, so its contents are
  never walked. Symlinked bundles are refused like any symlinked directory (not followed).
- `src/core/BrowserIndex.cpp`: the single `QDirIterator ... Subdirectories` call is replaced by
  `walkDirectory` / `visitEntry` / `reportProgress`: a stack of directory listings with
  `QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable`. Ordinary subdirectories are
  queued, bundles are admitted as one item, symlinked directories are not followed. The filters keep
  the old exclusions (hidden entries, symlinks, unreadable). Cancel, truncation and progress reporting
  are unchanged.
- No new control commands. No new files (nothing added to `tests/fork-sources.txt`).

## Regression tests

Placed in `tests/src/core/BrowserCatalogQueryTest.cpp` (the file that already covers the walk), not in
`BrowserCatalogTest.cpp`, which is unchanged.

- `lv2BundleIsOnePresetAndItsContentsAreNotWalked`: no-kind query returns exactly the bundle as
  `BrowserKind::Preset`; `kind=preset` query returns it too.
- `indexRecordsLv2BundleAsOnePreset`: the persistent index records the bundle as one `Preset` entry and
  does not index `manifest.ttl`. Store is a separate temp file via `ZENE_BROWSER_INDEX`, unset at end.

## Commands and exit codes

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-287-port.r1/logs`

| Step | Command | EXIT | Log |
| --- | --- | --- | --- |
| Baseline gate, before any edit | `bash tests/file-length-gate.sh --check` | 1 | `baseline-flen-0.log` |
| First build attempt | `cmake --build build --target BrowserCatalogQueryTest -j2` | 2 | `build-1.log` (ccache could not write `/run/user/1000/ccache-tmp`; sandbox) |
| Build with temp dir moved | `CCACHE_TEMPDIR=/tmp/ccache-tmp-new-p8-287 cmake --build build --target BrowserCatalogQueryTest -j2` | 0 | `build-2.log` |
| **RED** (unfixed code, new tests) | `ctest -R '^BrowserCatalogQueryTest$' --output-on-failure` (in `build/tests`) | 8 | `test-red-1.log` |
| Fix saved as patch | `git diff -- include/BrowserCatalog.h src/core/BrowserCatalog.cpp src/core/BrowserIndex.cpp > fix.patch` | 0 | `fix.patch` |
| Fix removed | `git apply -R fix.patch` | 0 | |
| Rebuild, unfixed | `cmake --build build --target BrowserCatalogQueryTest -j2` | 0 | `build-4.log` |
| RED re-proof | `ctest -R '^BrowserCatalogQueryTest$' --output-on-failure` | 8 | `test-red-4.log` |
| Fix restored | `git apply fix.patch` | 0 | |
| Rebuild, fixed | `cmake --build build --target BrowserCatalogQueryTest -j2` | 0 | `build-5.log` |
| **GREEN** | `ctest -R '^BrowserCatalogQueryTest$' --output-on-failure` | 8 | `test-green-5.log` |
| Targets for the family and app | `cmake --build build --target zene BrowserCatalogTest BrowserTagMenuTest LibraryBrowserTest ControlBrowserCommandsTest -j2` | 0 | `build-6.log` |
| Browser family | `ctest -R '^(BrowserCatalogQueryTest\|BrowserCatalogTest\|BrowserTagMenuTest\|LibraryBrowserTest\|ControlBrowserCommandsTest)$' --output-on-failure` | 8 | `test-family-6.log` |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 1 | `complexity-gate-7.log` |
| File-length gate | `bash tests/file-length-gate.sh --check` | 1 | `file-length-gate-7.log` |

### RED / GREEN for the regression

| Run | Result | Browser-LV2 tests |
| --- | --- | --- |
| RED (`test-red-1.log`, `test-red-4.log`) | 7 passed, 5 failed of 12 | both FAIL: `lv2BundleIsOnePresetAndItsContentsAreNotWalked` (test line 376: first item is `.../MySynth.lv2/manifest.ttl`), `indexRecordsLv2BundleAsOnePreset` (line 414: "the LV2 bundle was not indexed") |
| GREEN (`test-green-5.log`) | 9 passed, 3 failed of 12 | both PASS |

The 3 failures present in both runs are not LV2-related: `theQueryMatchesNameTagAndMetadata`,
`theStoreKeyIsCanonical` and `scanCountCoversWhatTheTagFilterRejected`. Each fails because
`BrowserTagStore::add` returns false (the default tag-store save fails in this environment). The new
tests are the last two functions in the file, so they cannot affect those earlier ones. I did not
find the root cause of the save failure (the default store path is not writable or does not exist
here; not verified further), and I did not change them.

Affected family (`ctest -R`, 5 tests executed): BrowserCatalogQueryTest FAILED (only the 3 pre-existing
tests above), BrowserCatalogTest, BrowserTagMenuTest, LibraryBrowserTest, ControlBrowserCommandsTest
PASSED. `LibraryBrowserTest` exercises the index scan and passes with the new walk.

### Gates

- `tests/file-length-gate.sh --check` EXIT=1, **also on the unmodified tree** (`baseline-flen-0.log`,
  14 `REGRESSION` lines). After the fix it is EXIT=1 with the same 14 (`file-length-gate-7.log`); no
  listed file is one I changed. The gate is failing on the branch's own baseline; I did not re-anchor it.
- `tests/complexity-gate.sh --check` EXIT=1 (`complexity-gate-7.log`): 6 `REGRESSION` lines, all in
  files this change does not touch (`XyPadPairTest.cpp`, `DawProjectReadTracks.cpp`,
  `ControlCommandsMixerTools.cpp`, `ControlCommandsMixerStrip.cpp`, `ProjectRevisions.cpp`,
  `ControlCommandsShortcuts.cpp`). No browser function is listed. I did not run this gate on the
  unmodified tree. The functions this change touches measure with `lizard` as CCN ≤ 7: `readDirectory` 7,
  `browserIsLv2Bundle` 3, `visitEntry` 4, `reportProgress` 2, `walkDirectory` 7, `BrowserIndex::scan` 6.

## Files committed

- `include/BrowserCatalog.h`
- `src/core/BrowserCatalog.cpp`
- `src/core/BrowserIndex.cpp`
- `tests/src/core/BrowserCatalogQueryTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-287-port.md`

## Not done / notes

- `cf75740d2` put its index change in `BrowserIndex.cpp` with a similar directory-stack walk, but it was
  written against the older tree. This port writes the walk against the current `BrowserIndex.cpp`.
- `BrowserCatalogTest.cpp` was not touched; the tests went to the query-walk file.
- Build-tree caveat: `ccache` needs `CCACHE_TEMPDIR` outside `/run/user` in this sandbox; the code
  build itself was not affected.
