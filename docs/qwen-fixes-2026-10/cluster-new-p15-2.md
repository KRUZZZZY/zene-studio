Verdict: FIXED

new-p15-2: REAL. `browser.thumbnail` wrote its PNG over whatever `out` named, including the audio file it draws from.

Evidence (before the fix): `thumbnail()` took `out` from the arguments and saved the PNG there at [ControlCommandsBrowserMedia.cpp:139](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p15-2/src/core/ControlCommandsBrowserMedia.cpp:139) with no check that `out` was not the input. `path` is canonicalised by `readBrowserFileArgument`, but `out` was not. So `out` equal to `path`, a `..` spelling, a symbolic link or a hard link all reached `QImage::save()`, which truncates the file and writes PNG bytes into it. The RED run below reproduced exactly that: the refusal returned ok.

Fix: after the absolute-path check, `thumbnail()` refuses `out` when `BounceInPlace::namesTheSameFile(key, out)` holds, with `InvalidArgs`. [ControlCommandsBrowserMedia.cpp:130-134](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p15-2/src/core/ControlCommandsBrowserMedia.cpp:130). That helper already exists and compares canonical paths, and on POSIX also device and inode (so hard links), and it is the same guard `ControlCommandsRenderQueue.cpp:115` applies to render output. The check is placed before the peak cache is read or the file is written. No new command, schema, or saved data changes.

Regression test: `LibraryBrowserTest::aThumbnailNeverWritesOverItsSource()` at [LibraryBrowserTest.cpp:416](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p15-2/tests/src/core/LibraryBrowserTest.cpp:416). It runs on a private copy of the 440 Hz tone under `thumb-source/`, so the shared fixtures are never touched. It checks four refused outputs (the source itself, a `..` spelling, a symbolic link, a POSIX hard link), requires the source's bytes to stay identical after each refusal, and has a positive control: a PNG beside the source is still written.

## Evidence table

| Step | Command (run from the worktree or `build/tests`) | Log (LOG FOLDER) | EXIT |
| --- | --- | --- | --- |
| Build test target, first attempt | `cmake --build build --target LibraryBrowserTest -j4` | `build-red-1.log` | 2 (ccache temp dir under `zene-bunny-fix/dsh` is outside the write jail; not a code error) |
| Build test target, with `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp-new-p15-2` | same | `build-red-2.log` | 0 |
| RED, first test version (source-only check) | `ctest -R "^LibraryBrowserTest$" --output-on-failure` | `test-red-1.log` | 8: `aThumbnailNeverWritesOverItsSource() '!refused.ok' returned FALSE`, 22 passed / 1 failed |
| GREEN, first version with fix | `cmake --build build --target zene LibraryBrowserTest -j4`, then `ctest -R "^LibraryBrowserTest$" -V` | `build-green-1.log`, `test-green-2-verbose.log` | 0, 0: 23 passed, 0 failed, 0 skipped |
| 4b revert | `git diff -- src/core/ControlCommandsBrowserMedia.cpp > logs/fix.patch && git apply -R logs/fix.patch` | (patch in logs) | 0, 0 |
| Rebuild with fix reverted | `cmake --build build --target zene LibraryBrowserTest BrowserCatalogQueryTest -j4` | `build-revert-1.log` | 0 |
| RED, reverted, first version | `ctest -R "^LibraryBrowserTest$" --output-on-failure` | `test-revert-red-2.log` | 8 |
| Test restructured to fix the 500-line gate (see below); RED with fix reverted | `git apply -R logs/fix.patch`; `cmake --build build --target zene LibraryBrowserTest -j4`; `ctest -R "^LibraryBrowserTest$" --output-on-failure` | `build-red-3.log`, `test-red-3.log` | 0, 8: `refusedOver(source)` returned FALSE, 22 passed / 1 failed |
| Reapply (final test version) | `git apply logs/fix.patch`; `cmake --build build --target zene LibraryBrowserTest -j4`; `ctest -R "^LibraryBrowserTest$" -V` | `build-green-2.log`, `test-green-3-verbose.log` | 0, 0, 0: 23 passed, 0 failed, 0 skipped (`aThumbnailNeverWritesOverItsSource()` PASS) |
| Affected family, final files | `ctest -R "Browser"` from `build/tests` (7 tests; the six others were built with `cmake --build build --target BrowserCatalogQueryTest BrowserCatalogTest ControlBrowserCommandsTest BrowserFeaturesWindowTest BrowserTagMenuTest BrowserDragUrlTest -j4`) | `build-family-1.log`, `test-family-final-2.log` | 8: 6 of 7 pass; the one failure is `BrowserCatalogQueryTest`, see caveats |
| Pre-existing check on `BrowserCatalogQueryTest` with fix reverted | `ctest -R "^BrowserCatalogQueryTest$"` (reverted state) | `test-revert-catalog-1.log` | 8: 14 passed, 2 failed (same two assertions as with the fix) |

The family count: `ctest -R Browser -N` reports 7 tests. `LibraryBrowserTest` is one ctest test whose binary reports 23 passed test functions (verbose totals above).

## Gates

The wrapper `feature_check.py` cannot run in this session. It creates `<worktree>-feature-check/` in the projects folder and a base baseline cache under the bunny-fix tree, both outside the write jail (`feature-check-1.log`: `PermissionError`, EXIT 1). I did not change the gate. I ran each command the wrapper lists, from the worktree, with the logs in the LOG FOLDER:

| Gate | Command | EXIT |
| --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 |
| check-namespace | `tests/scripted/check-namespace` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 (7 REGRESSION lines, all in files this branch does not touch; none names `ControlCommandsBrowserMedia` or `LibraryBrowserTest`, `gate-complexity-2.log`) |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 (19 REGRESSION lines, none in a changed file, `gate-file-length-2.log`) |

The file-length gate first flagged `tests/src/core/LibraryBrowserTest.cpp` as a new file over 500 code lines (504) because my first test version added too much. I restructured the test around one `refusedOver` helper (same four refusals, same positive control) and the file no longer appears in the list. The ratchets are red on the train by design. Under the wrapper's rule (fail only on items new in changed files) my two changed files have no item in either list, so the wrapper's ratchet rows should pass. I could not run the baseline comparison itself, so that is inferred from the listings, not measured.

## Self-check

- **Regression test fails with the fix reverted and passes with it:** yes. RED with the revert: `test-red-3.log` EXIT 8 (`refusedOver(source)` returned FALSE); GREEN with the fix: `test-green-3-verbose.log` EXIT 0 (23 passed, 0 failed, 0 skipped).
- **Test is deterministic (no timing, sleep or race):** yes. It makes its own copy of a fixture file and its own symbolic and hard links in a `QTemporaryDir`, and every check reads file contents, so there is no clock, no sleep and no thread. The positive control is an exact call with no timing involved.
- **Fix at the cause and complete:** the cause is that `out` is not checked against the input, and that check is placed in the one function that writes. Callers: `src/gui/BrowserResults.cpp:125` and `src/gui/BrowserSearchView.cpp:218` pass only `path`, never `out`, so no legitimate caller reaches the refusal. Siblings: `ControlCommandsRenderQueue.cpp:115` already refuses a render output that is its own source, using the same helper, so the two are now consistent. No other browser command takes an `out` argument (`rg '"out"' src/core/ControlCommandsBrowser*.cpp` lists only the thumbnail).
- **No collateral damage:** the refusal only fires when `out` resolves to the audio file being thumbnailed, which can only destroy that file. Old projects and saved data are not read or written by this path. The cached path (no `out`) is unchanged, and `aThumbnailIsAPicture` still passes.
- **Sibling commands (the whole `browser.*` group, which the completion check counts as siblings):** none is affected. The fix changes only the `browser.thumbnail` handler. Caller-supplied output paths exist only on that command: `rg '"out"' src/core/ControlCommandsBrowser*.cpp` lists only `ControlCommandsBrowserMedia.cpp:127` and `:136` and the schema at `:353`. The only file write at a caller-supplied path in the browser command files is `ControlCommandsBrowserMedia.cpp:146` (the thumbnail `save`). `ControlCommandsBrowserCollections.cpp:64` and `:95` write only to their fixed store path. `browser.preview`, `browser.preview_stop` and `browser.load` write no file at a caller-given path. The other browser commands therefore keep their behaviour.
- **Gates row (`feature_check.py`):** the fixer could not run the wrapper in its session (`feature-check-1.log`: `PermissionError` creating `<worktree>-feature-check/`, outside the write jail). The reviewer ran `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p15-2` after the final test edit; it exited 0 with all gates PASS, including both ratchets' baseline comparisons.
- **Verdict FIXED, so no ALREADY-FIXED or FALSE-POSITIVE proof is needed.**

## Caveats

- `BrowserCatalogQueryTest` fails 2 of 16 assertions (`theQueryMatchesNameTagAndMetadata` at line 146, `categoryNarrowsTheQueryAndEmptyMeansAny` at line 181: `store.setCategory(...)` returns false). It fails the same way with my fix reverted (`test-revert-catalog-1.log`), so it is independent of this change. The store is the process-wide `BrowserTagStore` singleton, which saves to the configured working directory (`browserTagStorePath()`), and that test does not isolate it. I did not establish the exact save failure beyond "save() returned false with no message".
- Running the browser family wrote to the user's real `~/Documents/Zene Studio/plugin-scan-cache.json` (modified 15:59). That is existing test behaviour outside this change and I did not touch those files on purpose. Flagged for the owner.
- The hard-link and symlink cases run on POSIX; direct and `..` path cases run on all platforms. The test no longer uses `QSKIP` for unavailable link creation.
- The ccache temp directory set by the environment lies outside the write jail; the builds that succeeded used `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp-new-p15-2`.

Files committed for this verdict: `src/core/ControlCommandsBrowserMedia.cpp`, `tests/src/core/LibraryBrowserTest.cpp`, and this report. No new files; both source files are already registered in `tests/fork-sources.txt`.
