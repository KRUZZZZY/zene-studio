# Cluster new-p8-287-port: LV2 preset bundles are never returned by browser searches

**Verdict: FIXED** (ported by hand onto `050/m0-train-5` from `bugs/qwen-new-p8-287`, commit `cf75740d2`).
Round 2 reworks the index walk as Luna's round-1 review (`new-p8-287-port.r1.md`, verdict REWORK) asked.
Not ALREADY-FIXED: on this branch an LV2 bundle is still walked into and never returned, before the change.

## Evidence (before the fix, current tree)

- `src/core/BrowserCatalog.cpp` `readDirectory`: a directory entry is only queued for descent
  (`walkMayDescend`) or counted as truncated (`skippedForDepth`). Nothing is appended for it, so
  `MySynth.lv2` never reaches the result; its `manifest.ttl` does (the `.ttl` file is kind `other`).
- `src/core/BrowserIndex.cpp` `BrowserIndex::scan` (before b001a10b3): `QDirIterator(dir, QDir::Files | QDir::Readable,
  QDirIterator::Subdirectories)` lists files only, so the bundle directory is never indexed while its
  files are.
- The kind table maps `lv2` to `BrowserKind::Preset` (`src/core/BrowserCatalog.cpp` `kKinds`), so the
  Presets source does include bundles; the walkers were the only obstacle.

## The fix (current, round 2)

- `include/BrowserCatalog.h` / `src/core/BrowserCatalog.cpp`: `browserIsLv2Bundle(const QFileInfo&)`: a
  non-symlinked directory whose suffix is `lv2` (case-insensitive). Shared by both walkers.
- `readDirectory` (query walk, unchanged in round 2): a bundle is appended to the results and not queued,
  so its contents are never walked. Symlinked bundles are refused like any symlinked directory.
- `src/core/BrowserIndex.cpp` (round 2 rework):
  - `walkDirectory` (line 209) is a streaming stack of `QDirIterator`s, one open listing per level of
    descent (`std::vector<std::unique_ptr<QDirIterator>>`). An ordinary directory is entered the moment
    it is met, and its parent's listing resumes when it is done. This is the order QDirIterator's
    `Subdirectories` walk takes (see below). Nothing else is buffered.
  - `isWalkedDirectory` (line 186): a directory the walk handles itself rather than admitting. An LV2
    bundle is not one (it is admitted as one item). A symlinked directory is skipped, never followed.
  - `openListing` (line 192): filters `QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable`.
    Hidden and unreadable entries stay excluded, as before.
  - `admit` (line 170) returns true when it counted the file. `reportProgress` (line 197) is called only
    then, so a directory entry never takes the progress mutex. The `ProgressEvery` cadence is unchanged.
  - Cancel and truncation are unchanged.
- No new control commands. No new files (`src/core/BrowserIndex.cpp` is already in
  `tests/fork-sources.txt`).

## Round 2: what Luna asked for, and what was verified

Luna's round-1 findings: (1) the sibling-path queue drained with `takeLast` changes the traversal order,
which changes which entries survive the 50,000-entry cap (`include/BrowserIndex.h:89`); (2) `reportProgress`
took the mutex once per directory entry while `scanned` was 0 or a multiple of 64; (3) both gates exit 1.

**Traversal order.** `walk-order-check-2.log` (a standalone program, `/tmp/walkorder-qwen/walkorder.cpp`,
outside the tree) walks one tree three ways. The tree holds nested directories, a hidden directory and a
symlinked directory; all three walks exclude the hidden and symlinked ones.

| Walk | Order | Equal to `QDirIterator(Subdirectories)`? |
| --- | --- | --- |
| `QDirIterator(..., Subdirectories)` (before b001a10b3) | top2 top1 d/d1 b/b1 b/c/c1 a/a2 a/a1 a/z/z1 a/x/x1 a/x/y/y1 | (reference) |
| round-1 queue (`takeLast`) | top2 top1 a/a2 a/a1 a/x/x1 a/x/y/y1 a/z/z1 b/b1 b/c/c1 d/d1 | NO |
| round-2 streaming stack | top2 top1 d/d1 b/b1 b/c/c1 a/a2 a/a1 a/z/z1 a/x/x1 a/x/y/y1 | YES |

The index sorts its entries by path before saving, so traversal order is observable only when the cap
truncates the walk. A ctest case would need more than 50,000 files, so this order is verified by the
standalone comparison above, not by a ctest case.

**Progress.** `reportProgress` is called only when `admit` returns true, so a walk over a directory-only
tree takes the progress mutex zero times instead of once per directory.

**Complexity** (`lizard -l cpp src/core/BrowserIndex.cpp`, `lizard-browserindex-2.log`): `walkDirectory` CCN 8,
`admit` 5, `isWalkedDirectory` 2, `openListing` 1, `reportProgress` 2, `BrowserIndex::scan` 6.

## Regression tests

Placed in `tests/src/core/BrowserCatalogQueryTest.cpp` (the file that already covers the walk), not in
`BrowserCatalogTest.cpp`, which is unchanged.

- `lv2BundleIsOnePresetAndItsContentsAreNotWalked` (line 365): no-kind query returns exactly the bundle as
  `BrowserKind::Preset`; `kind=preset` query returns it too.
- `indexRecordsLv2BundleAsOnePreset` (line 388): the persistent index records the bundle as one `Preset`
  entry and does not index `manifest.ttl`. Store is a separate temp file via `ZENE_BROWSER_INDEX`, unset at end.

### RED / GREEN (round 2 tree)

The proof is the whole fix, not only the rework: `fix.patch` is `git diff b001a10b3^ -- src/core/BrowserCatalog.cpp
src/core/BrowserIndex.cpp` (the header only declares `browserIsLv2Bundle`, so it is not reversed). Reversing it
restores the pre-fix behaviour in both product files.

| Run | Command | EXIT | Result (`-V` log) |
| --- | --- | --- | --- |
| Patch check | `git apply --check -R fix.patch` | 0 | reverses cleanly |
| **RED** | `git apply -R fix.patch`; `cmake --build build --target zene BrowserCatalogQueryTest -j2`; `ctest -R '^BrowserCatalogQueryTest$' -V` (in `build/tests`) | 0; 0; **8** | `test-9-red-verbose.log`: 7 passed, 5 failed. `lv2BundleIsOnePresetAndItsContentsAreNotWalked` FAIL (Compared values are not the same); `indexRecordsLv2BundleAsOnePreset` FAIL (`bundleIndexed` returned FALSE, "the LV2 bundle was not indexed") |
| **GREEN** | `git apply fix.patch`; same build; same ctest | 0; 0; **8** | `test-10-green-verbose.log`: 9 passed, 3 failed. Both LV2 cases PASS |

The 3 failures in both runs are the tag-store cases described below, not LV2 cases.

## The 3 failing cases (environment, not this change)

`theQueryMatchesNameTagAndMetadata` (its tag half, line 142 adds the tag and line 146 fails),
`theStoreKeyIsCanonical` and `scanCountCoversWhatTheTagFilterRejected` fail because
`BrowserTagStore::add` returns false. The tests do not capture the error text. The likely cause is
`save()` failing at the default store path. That is inferred from the path below, not observed.

- Round-1 code (HEAD, `b001a10b3`) fails the same 3 cases with the same messages in this environment
  (A/B: `build-3-headAB.log`, `test-2-headAB.log`, EXIT 8). The rework does not touch `BrowserTagStore`.
- The default path is `browserTagStorePath()` (`src/core/BrowserCatalog.cpp:421-426`) =
  `ConfigManager::workingDir()/browser-tags.json`; on Linux the working directory is
  `~/Documents/Zene Studio/` (`src/core/ConfigManager.cpp:745`). That is outside this worktree's write-jail.
  I could not see the errno (the tests do not print the save error), so the cause is inferred from the
  location, not confirmed. `~/Documents/Zene Studio/browser-tags.json` was last modified 06:09, after round 1
  was committed at 06:02, which fits round 1's run writing there. I did not read or change that file.
- **For the owner:** these tests write to the default tag store, not a temp file. `openAt` is called only
  in `cleanupTestCase`, so the tag adds before it write to the user's real store. This is a pre-existing
  test-isolation problem. It is not changed here, and no test was weakened.

## Commands and exit codes (round 2)

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-287-port.r2/logs`

| Step | Command | EXIT | Log |
| --- | --- | --- | --- |
| First build | `cmake --build build --target zene BrowserCatalogQueryTest BrowserCatalogTest BrowserTagMenuTest LibraryBrowserTest ControlBrowserCommandsTest -j2` | 2 (ccache could not write `/run/user/1000/ccache-tmp`) | `build-1.log` |
| Build with ccache temp dir moved | `CCACHE_TEMPDIR=/tmp/ccache-tmp-qwen cmake --build ...` (same targets) | 0 | `build-2.log` |
| Query suite, HEAD (A/B) | build (same 5 targets, HEAD `BrowserIndex.cpp`); `ctest -R '^BrowserCatalogQueryTest$'` | 0; 8 | `build-3-headAB.log`; `test-2-headAB.log` |
| Query suite, rework | build (same 5 targets); `ctest -R '^BrowserCatalogQueryTest$'` | 0; 8 | `build-4-rework.log`; `test-3-rework.log` |
| Fix patch saved | `git diff b001a10b3^ -- src/core/BrowserCatalog.cpp src/core/BrowserIndex.cpp > fix.patch` | 0 | `fix.patch` |
| RED | see the RED / GREEN table | 0; 0; 8 | `build-9-red.log`; `test-9-red-verbose.log` |
| GREEN | see the RED / GREEN table | 0; 0; 8 | `build-10-green.log`; `test-10-green-verbose.log` |
| Final family build | `CCACHE_TEMPDIR=/tmp/ccache-tmp-qwen cmake --build build --target zene BrowserCatalogQueryTest BrowserCatalogTest BrowserTagMenuTest LibraryBrowserTest ControlBrowserCommandsTest -j2` | 0 | `build-11-family-final.log` |
| **Browser family (final)** | `ctest -R '^(BrowserCatalogQueryTest\|BrowserCatalogTest\|BrowserTagMenuTest\|LibraryBrowserTest\|ControlBrowserCommandsTest)$' -V` (in `build/tests`) | **8** | `test-11-family-final.log` |
| Complexity gate | `bash tests/complexity-gate.sh --check` | **1** | `gate-complexity-2.log` |
| File-length gate | `bash tests/file-length-gate.sh --check` | **1** | `gate-filelength-2.log` |
| CCN measurement | `lizard -l cpp src/core/BrowserIndex.cpp` | 0 | `lizard-browserindex-2.log` |
| Walk-order check | `g++ -std=c++20 -fPIC walkorder.cpp -o walkorder $(pkg-config --cflags --libs Qt6Core)`; `./walkorder` | 0; 0 | `walk-order-check-2.log` |

One run is not in the evidence. A build step used an unquoted variable (`$B`) under zsh, so it did not build
and exited 127. Its two test results were discarded, and RED and GREEN were rerun with explicit commands (above).

### Family results (final tree)

5 tests executed (`ctest -R`), 4 passed: `BrowserCatalogTest`, `ControlBrowserCommandsTest`,
`LibraryBrowserTest`, `BrowserTagMenuTest`. `BrowserCatalogQueryTest` failed only the 3 tag-store cases above.
The family exit is 8, not 0, because of those 3 cases in this environment.

### Gates (final tree)

- `tests/file-length-gate.sh --check` EXIT=1: 14 `REGRESSION` lines, the same count as round 1's baseline
  run (`baseline-flen-0.log`). None names a file this change touches; `src/core/BrowserIndex.cpp` is 382 lines.
- `tests/complexity-gate.sh --check` EXIT=1: 6 `REGRESSION` lines, all in files this change does not touch
  (`XyPadPairTest.cpp`, `DawProjectReadTracks.cpp`, `ControlCommandsMixerTools.cpp`,
  `ControlCommandsMixerStrip.cpp`, `ProjectRevisions.cpp`, `ControlCommandsShortcuts.cpp`). No browser
  function is listed. The complexity gate was not run on the unmodified tree.
- No baseline or limit was changed.

## Files committed

Round 1 (`b001a10b3`): `include/BrowserCatalog.h`, `src/core/BrowserCatalog.cpp`, `src/core/BrowserIndex.cpp`,
`tests/src/core/BrowserCatalogQueryTest.cpp`, this report.

Round 2 (this commit): `src/core/BrowserIndex.cpp` (the rework), `docs/qwen-fixes-2026-10/cluster-new-p8-287-port.md`.

## Not done / notes

- `cf75740d2` put its index change in `BrowserIndex.cpp` with a similar directory-stack walk, but it was
  written against the older tree. This port writes the walk against the current `BrowserIndex.cpp`.
- `BrowserCatalogTest.cpp` was not touched; the tests went to the query-walk file.
- Build-tree caveat: `ccache` needs `CCACHE_TEMPDIR` outside `/run/user` in this sandbox; the code build itself was not affected.
- The 3 tag-store failures and the default-store write described above are open. They need an owner decision on test isolation (a temp store for the whole file) or a sandbox allowance for the Documents path.
