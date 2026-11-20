FALSE-POSITIVE

# cluster new-p15-287: FIFO named like audio is not a walked item

Branch `bugs/qwen-new-p15-287`, base commit `fa4a5f2e78`. No product code or tests changed in this report. Log paths below are relative to the run's `logs/` folder (outside the repo, under `state/runs/new-p15-287.r1/logs/`).

## (a) Finding (verbatim)

> BUG 9: A readable FIFO with an audio extension can block a metadata query. where: src/core/BrowserCatalog.cpp:275-304, 215-236; src/core/BrowserMetadata.cpp:145-150. what: The directory walk appends readable non-directory entries without checking that they are regular files. A FIFO named with an audio extension can reach sf_open(), whose read-only open can wait indefinitely for a writer. The query runs synchronously, so this can hang the UI and control request. when: Put a readable FIFO such as waiting.wav in a searched root and run a query with probe=true that reaches it. fix: Exclude non-regular files before probing metadata.

The finding's line numbers are stale. In the current code the walk is `readDirectory` at `src/core/BrowserCatalog.cpp:347-381`, the probe call is `:247`, and `probeAudioMetadata` is defined at `src/core/BrowserMetadata.cpp:145`. The mapping below uses the current lines.

## (b) Code evidence

- `src/core/BrowserCatalog.cpp:350-351`: the walk opens `QDirIterator entries(dirPath, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable)`. There is no `QDir::System`, so a FIFO is never returned by the iterator. This refutes the finding's premise that a FIFO reaches the walk. The finding's `what` is true only for entries the filter already admits, and a FIFO is not one of them.
- `src/core/BrowserCatalog.cpp:347-381`: `readDirectory` is the only walk that appends query entries. Its append sites are `:360` (LV2 bundle) and `:378` (file), both fed by the `:350` iterator.
- `src/core/BrowserIndex.cpp:194`: sibling `openListing()` filter, the same flags without `QDir::System`.
- `src/core/BrowserCatalog.cpp:247`: the sole product caller of `probeAudioMetadata` (`logs/p2-probe-callers.log`, generated earlier in this run and not re-run here). Test-only callers are `tests/src/core/BrowserCatalogTest.cpp:215, :233, :237`.
- Chain: `runBrowserQuery:605 -> considerPath:311 -> probeAndAppend:247 -> probeAudioMetadata` (`logs/p2-callers-chain.log`, `logs/p2-notes.md`).
- `rg 'QDir::System'` over both walk files: no matches (`logs/p2-walk-filters.log`).

## (c) Real-surface probe evidence

Temporary probe test `fifoIsNotAWalkedItem` (patch `logs/p1-fifo-probe.patch`, applied for this run and reverted; not committed). It runs `runBrowserQuery` with `probe = true` on a FIFO directory and a regular-file control directory. The patch asserts:

- FIFO directory: `QCOMPARE(scanned, 0)`, `QCOMPARE(matched, 0)`, `QVERIFY(items.isEmpty())`, `QCOMPARE(probed, 0)`.
- Regular-file control: `QCOMPARE(scanned, 1)` and the matching assertions.

`logs/p3b-probe-test.log` shows `PASS : BrowserCatalogQueryTest::fifoIsNotAWalkedItem()`. The log does not print the numbers; they are established by that PASS.

## (d) Standalone Qt probes

| Qt | without `QDir::System` | with `QDir::System` | log |
|---|---|---|---|
| 6.12.0 | listed=0 entries=0 | listed=1 entries=1 | `logs/p1-probe-qt6.log`, `RESULT: as expected` |
| 5.15.19 | listed=0 entries=0 | listed=1 entries=1 | `logs/p1-probe-qt5.log`, `RESULT: as expected` |

Versions: `logs/p1-qt-versions.log`. Provenance of the "p1 exit 0": no log in the run's `logs/` folder records an `EXIT=` line for the standalone probe run. `logs/p1-build-qt5.log` and `logs/p1-build-qt6.log` are 0 bytes. The RESULT lines are printed by the probe itself. **The p1 exit code is unverified.**

## Commands and exit codes

All commands run from the worktree `kads-new-p15-287` unless shown. Each log ends with an `EXIT=` line written by the command.

| # | Command (abbrev.) | EXIT | Log |
|---|---|---|---|
| 1 | `git apply logs/p1-fifo-probe.patch` | 0 | `p3b-apply.log` |
| 2 | `cmake --build build --target zene BrowserCatalogQueryTest` | 0 | `p3b-probe-build.log` |
| 3 | `ctest -R '^BrowserCatalogQueryTest$' --output-on-failure` (in `build/tests`) | 8 | `p3b-probe-test.log` (15 passed, 2 failed; `fifoIsNotAWalkedItem` PASS) |
| 4 | `git apply -R logs/p1-fifo-probe.patch` | 0 | `p3b-revert.log` |
| 5 | `cmake --build build --target zene BrowserCatalogQueryTest` | 0 | `p3b-baseline-build.log` |
| 6 | `ctest -R '^BrowserCatalogQueryTest$' --output-on-failure` (in `build/tests`) | 8 | `p3b-baseline.log` (14 passed, 2 failed) |
| 7 | `ls -ld` on a scratch `Documents/Zene Studio`, then `HOME=<scratch>/home QT_QPA_PLATFORM=offscreen ./BrowserCatalogQueryTest` (in `build/tests`) | 0 | `p3b-isolated.log` (16 passed, 0 failed; the `ls -ld` line shows the created dir) |
| 8 | `rm -rf logs/scratch-p3b` | 0 | none |
| 9 | Read-only check: `ls -ld "$HOME/Documents/Zene Studio" > /dev/null 2>&1; echo EXIT=$?` | 2 | not logged; re-run in session, output below |

Check 9 output: `ls: cannot access '/home/kruzzzzy/Documents/Zene Studio': No such file or directory`, while `~/Documents` exists (`drwx------`). Check 9 is my addition; it was not in the packet, and it is the evidence for the baseline cause below.

The two baseline failures are the same with and without the probe: `theQueryMatchesNameTagAndMetadata` (matched 0, expected 1) and `categoryNarrowsTheQueryAndEmptyMeansAny` (`store.setCategory(kick, "Drums")` returned FALSE). Run 7 passes both with the working dir present.

Check 9 was re-run once for its exit code (`ls -ld "$HOME/Documents/Zene Studio" > /dev/null 2>&1; echo EXIT=$?` -> `EXIT=2`). The commit and post-commit `git status` results are in the run's final status, not in this file.

## Baseline failure cause (verified)

The baseline failures are caused by the missing `~/Documents/Zene Studio` working directory. The chain, with each step cited:

1. `src/core/ConfigManager.cpp:764`: the installed working dir is `docs + "/Zene Studio/"`, unless `~/lmms/projects/` exists (`:773`), in which case the legacy path is adopted instead. The test HOME has no `lmms/projects`, so the Documents path applies.
2. `src/core/ConfigManager.cpp:627-629`: `createWorkingDir()` runs only when `hasWorkingDir()` is true. A missing working dir therefore gets no subdirectories created.
3. `src/core/BrowserCatalog.cpp:565-567`: `browserTagStorePath()` returns `workingDir/browser-tags.json`.
4. `src/core/BrowserTagStore.cpp:225-230`: `save()` opens `QSaveFile file(storePath())` and returns false with `m_error` set when `open` fails. The failing tests show the save failing (`setCategory` returned FALSE, and the name/tag query matched 0 expected 1).
5. Controlled comparison: the same binary passes 16/16 with HOME pointing at a copy that has the directory (run 7), and fails 2 of 16 with the real HOME that lacks it (run 6, check 9).

Caveat: the step 4 claim that `QSaveFile::open` fails when the parent directory is absent is Qt behavior, not something I re-checked in this session. The controlled comparison in step 5 is the evidence that does not depend on that.

## Self-check

- **Claim-only closure, no product change:** Yes. The only file staged for commit is this report. The probe patch was applied for run 3 and reverted before run 5 (check 4 EXIT=0). No product or test code is in the commit.
- **Positive control and `|QDir::System` mutation as non-vacuity evidence:** The positive control is the regular-file control in `fifoIsNotAWalkedItem` (asserted `scanned == 1`, from the patch). Its PASS is the same test, so it is a control in the same run. The `|QDir::System` mutation is shown only at the standalone Qt level (`logs/p1-probe-qt6.log`, `logs/p1-probe-qt5.log`): adding `QDir::System` lists the FIFO. **No product-level mutation was run.** Running one would require a product change, which is out of scope, so product-level non-vacuity is inferred from the standalone probe plus the control, not measured.
- **Determinism:** The probe calls `mkfifo` and never opens the FIFO. It has no sleeps (`logs/p1-fifo-probe.patch`), so it cannot block. The probe was run once this session (run 3). **Repeat-run determinism is not measured.**
- **One walk source, sibling, sole caller:** One walk source, `readDirectory` (`BrowserCatalog.cpp:347-381`). One sibling filter, `BrowserIndex.cpp:194`. One product caller of `probeAudioMetadata`, `BrowserCatalog.cpp:247` (from `logs/p2-probe-callers.log`, not re-run this session).
- **No collateral:** No product or test change is in the commit. The baseline failures exist without the probe (run 6) and are explained by the missing working dir (section above). The isolated HOME run passes 16/16 (run 7). This isolates the two failures to the environment, not the FIFO path.
- **FileSearchJob note kept:** `src/gui/FileSearchJob.cpp` has a separate `QDirIterator` at `:82-83` (with `FollowSymlinks`), filtering with `fileInfo.isFile()` at `:93`. Its `task.dirFilters` were not read. It is outside `runBrowserQuery` and does not call `probeAudioMetadata` (`logs/p2-probe-callers.log`), so it does not affect this verdict.

## Other findings

- **Symlinks:** not tested. No symlink claim is made.
