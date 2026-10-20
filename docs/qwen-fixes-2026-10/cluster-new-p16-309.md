# Cluster new-p16-309: Enter on a missing project asset attempts to load it

**Verdict: FIXED**

In Project Media, Enter (and Shift+Enter) on a row for a missing file ran `browser.load` on the path, which refused it as "not a file". The context menu already hid Load for missing rows, and drag-out already refused them, but the keyboard path did not. `loadPath()` now refuses the `missing` kind before any command runs.

## Evidence (file:line, current tree)

- **Bug path, before the fix:** `src/gui/BrowserResults.cpp:59-65` (`loadCommand`) maps any kind other than `project` and `midi` to `browser.load`. `:68-76` (`loadFile`) forwards the path to it. `:178-184` (`loadPath`) had no check for `missing`. Enter and Shift+Enter reach `loadPath` through `load()` at `:173-175` and the key filter at `:207-210`.
- **Kind string:** Project Media sets `kind` to `tr("missing")` for a missing reference at `src/gui/BrowserSearchView.cpp:368`.
- **What `browser.load` does with it:** `src/core/ControlBrowserSupport.cpp:315-322` (`readBrowserFileArgument`) returns `NotFound` ("... is not a file") for a missing path. `loadPath` then emits `failed` (`BrowserResults.cpp:182`), which shows on the status line (`BrowserSearchView.cpp:142`).
- **The fix:** `src/gui/BrowserResults.cpp:180` returns `false` for `kind == BrowserSearchView::tr("missing")` before `stopPreview()` and before `loadFile()`. The string comparison matches the existing ones at `:135` and `:188`.
- **Siblings checked:** the context-menu entry is already hidden for missing rows (`:188`). Drag-out refuses them (`:135`). `preview()` refuses any non-audio kind (`:154`), so Space is unaffected. `loadPath` is the only route into `browser.load` from this class (its callers are `load()`, `:175`, and the menu lambda, `:191`).
- **Regression test:** `tests/src/gui/LibraryMenusTest.cpp:285-304` (`enterOnAMissingProjectAssetDoesNotTryToLoadIt`). It builds a `missing` row, presses Enter and Shift+Enter on the results view, and asserts that no `failed` signal is emitted and no track is added. It uses no timers, sleeps or races.
- **Correction to my first draft:** my first version asserted only the track count. It passed without the fix, because `browser.load` refuses before creating a track. The test was wrong, so I changed it to assert on the `failed` signal, which is the user-visible symptom. The RED run below is for the corrected test.

## Commands and exit codes

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-309.r1/logs/`. `CCACHE_TEMPDIR=/tmp/ccache-tmp-p16-309` is an environment setting only. The default ccache temp dir (`/run/user/1000/ccache-tmp`) returns "Permission denied" in this session.

| Step | Command (run from the worktree root unless noted) | EXIT |
| --- | --- | --- |
| Build, first try | `cmake --build build --target LibraryMenusTest -j4 > .../build-1.log 2>&1` | 2 (ccache temp-dir error, not code) |
| Build | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p16-309 cmake --build build --target LibraryMenusTest -j4 > .../build-2.log 2>&1` | 0 |
| RED, first draft (track-count assertion) | `ctest -R '^LibraryMenusTest$' --output-on-failure > .../test-red-1.log 2>&1` from `build/tests` | 0. Passed without the fix, so that draft was not a valid regression test. Corrected as above. |
| Build | `CCACHE_TEMPDIR=... cmake --build build --target LibraryMenusTest -j4 > .../build-3.log 2>&1` | 0 |
| **RED (corrected test, pre-fix)** | `./LibraryMenusTest enterOnAMissingProjectAssetDoesNotTryToLoadIt -v2 > .../test-red-3.log 2>&1` from `build/tests` | **1**. `FAIL!` at `LibraryMenusTest.cpp:300`: `Actual (failures.count()): 1`, `Expected (0): 0`. Enter alone emitted the failure. |
| Build (fix applied) | `CCACHE_TEMPDIR=... cmake --build build --target zene LibraryMenusTest -j4 > .../build-4.log 2>&1` | 0 |
| **GREEN** | `ctest -R '^LibraryMenusTest$' --output-on-failure > .../test-green-1.log 2>&1` from `build/tests` | **0** (1/1 ctest target passed) |
| GREEN, new function | `./LibraryMenusTest enterOnAMissingProjectAssetDoesNotTryToLoadIt -v2 > .../test-green-2.log 2>&1` | 0 (`PASS`) |
| Proof, save patch | `git diff -- src/gui/BrowserResults.cpp > .../fix.patch` | 0 (12-line patch) |
| Proof, revert | `git apply -R .../fix.patch` | 0 |
| Proof, build reverted | `cmake --build build --target zene LibraryMenusTest -j4 > .../build-5-reverted.log 2>&1` | 0 |
| **Proof RED (reverted)** | `ctest -R '^LibraryMenusTest$' --output-on-failure > .../test-red-proof.log 2>&1` from `build/tests` | **8**. `FAIL!` `Actual 1`, `Expected 0` at `LibraryMenusTest.cpp:300`. `Totals: 16 passed, 1 failed`. |
| Proof, reapply | `git apply .../fix.patch` | 0 |
| Proof, build reapplied | `cmake --build build --target zene LibraryMenusTest -j4 > .../build-6-reapplied.log 2>&1` | 0 |
| **Proof GREEN (reapplied)** | `ctest -R '^LibraryMenusTest$' --output-on-failure > .../test-green-proof.log 2>&1` from `build/tests` | **0** |
| Affected family, full binary | `QT_QPA_PLATFORM=offscreen ./LibraryMenusTest > .../test-libmenus-full.log 2>&1` from `build/tests` | 0. `Totals: 17 passed, 0 failed`. |
| Related family | `CCACHE_TEMPDIR=... cmake --build build --target BrowserDragUrlTest -j4 > .../build-8-dragurl.log 2>&1`, then `ctest -R '^BrowserDragUrlTest$' --output-on-failure > .../test-dragurl.log 2>&1` from `build/tests` | build 0; ctest **0** (1/1 passed) |

Gates: `feature_check.py` could not run in this session. Its first step is `logs.mkdir` on `zene-qwen-new-p16-309-feature-check/`, a sibling of the worktree that is outside the write-jail. It fails with `PermissionError` (`EXIT=1`). I therefore ran each of its gate commands directly from the worktree, with output in the log folder:

| Gate | Command | EXIT |
| --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 |
| check-namespace | `tests/scripted/check-namespace` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 |

The two ratchets are red on the train, as the brief says. I checked their `REGRESSION:` lines (`gate-complexity.log`, `gate-file-length.log`): none names `src/gui/BrowserResults.cpp` or `tests/src/gui/LibraryMenusTest.cpp`. Neither file gained a new over-limit function or a new over-500-line file, so both are "no new item in your files" under the feature-check rule. The ratchet baselines were not changed.

No new files were added, so no registration in `tests/fork-sources.txt` or `tests/upstream-modifications.txt` was needed. No control command was added, so the snapshot is untouched.

## Self-check

- **Each new test FAILS with the product change reverted and PASSES restored.** Yes. Reverted: `test-red-proof.log`, ctest EXIT 8, `Actual 1 / Expected 0` at `LibraryMenusTest.cpp:300`. Restored: `test-green-proof.log`, EXIT 0.
- **The test is deterministic.** It uses no timers, sleeps or races. The missing path is a file name inside the test's temporary directory that is never created (`QVERIFY(!QFileInfo::exists(gone))`). The assertion is a count of `failed` signals from a `QSignalSpy`, which is synchronous under `QTest::keyClick`.
- **The fix is at the cause and complete.** `loadPath` is the only function that forwards a row to `browser.load`, and all three entry points (Enter, Shift+Enter, the menu) reach it. The sibling guards at `:135` and `:188` already refuse missing rows, and the new guard matches them.
- **No collateral damage.** The guard fires only for kind `tr("missing")`, which only `BrowserSearchView.cpp:368` produces, and only in the Project Media list. No saved data or project file is read or written. Other kinds take the same path as before.
- **Behaviour choice to confirm:** the guard returns before `stopPreview()`. Before the fix, an Enter on a missing row also stopped any playing preview, and now it does not, because a refused load should not change playback. Nothing else changes. If the owner wants the old playback behaviour, move the guard below `stopPreview()`.
- **Realtime, undo, commands:** the change runs on the GUI thread only. No audio-thread code, no undo step (a refused load creates nothing), no new command, so no snapshot or canary change.
- **ALREADY-FIXED / FALSE-POSITIVE evidence:** not applicable. The verdict is FIXED.
- **Unverified:** I did not run the full `ctest` tree, and I did not run `feature_check.py` itself (see the gates note). The completion check's `gates (feature_check.py)` line is the only FAIL. It fails because the script cannot create its log folder outside the write-jail. Every gate command it runs passed or failed as the table above shows, and the two red ratchets are not in files this change touched.
