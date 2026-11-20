FALSE-POSITIVE

# cluster new-p15-287: FIFO named like audio is not a walked item

Branch `bugs/qwen-new-p15-287`, base commit `fa4a5f2e78`. No product code or tests are committed by this report. Log paths below are under `$RUN/logs/`, where `RUN=/home/kruzzzzy/.config/kads/zene/state/runs/new-p15-287.r1` (outside the repo). `$W` is the worktree `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p15-287`.

## (a) Finding (verbatim)

> BUG 9: A readable FIFO with an audio extension can block a metadata query. where: src/core/BrowserCatalog.cpp:275-304, 215-236; src/core/BrowserMetadata.cpp:145-150. what: The directory walk appends readable non-directory entries without checking that they are regular files. A FIFO named with an audio extension can reach sf_open(), whose read-only open can wait indefinitely for a writer. The query runs synchronously, so this can hang the UI and control request. when: Put a readable FIFO such as waiting.wav in a searched root and run a query with probe=true that reaches it. fix: Exclude non-regular files before probing metadata.

The finding's line numbers are stale. In the current code the walk is `readDirectory` at `src/core/BrowserCatalog.cpp:347-381`, the probe call is `:247`, and `probeAudioMetadata` is defined at `src/core/BrowserMetadata.cpp:145`. The mapping below uses the current lines.

## (b) Code evidence

- `src/core/BrowserCatalog.cpp:350-351`: the walk opens `QDirIterator entries(dirPath, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable)`. There is no `QDir::System`, so the iterator does not return a FIFO. This refutes the finding's premise that a FIFO reaches the walk. The finding's `what` holds only for entries the filter already admits, and a FIFO is not one of them.
- `src/core/BrowserCatalog.cpp:347-381`: `readDirectory` is the only walk that appends query entries. Its append sites are `:360` (LV2 bundle) and `:378` (file), both fed by the `:350` iterator.
- `src/core/BrowserIndex.cpp:194`: sibling `openListing()` filter, the same flags without `QDir::System`.
- `src/core/BrowserCatalog.cpp:247`: the sole product caller of `probeAudioMetadata`. Test-only callers are `tests/src/core/BrowserCatalogTest.cpp:215, :233, :237`.
- Chain: `runBrowserQuery:605 -> considerPath:311 -> probeAndAppend:247 -> probeAudioMetadata` (`logs/p2-callers-chain.log`, `logs/p2-notes.md`).
- `rg 'QDir::System'` over both walk files: no matches (`logs/p2-walk-filters.log`).

## (c) Real-surface probe and product-level mutation

Temporary probe test `fifoIsNotAWalkedItem` (patch `logs/p1-fifo-probe.patch`; applied, run, and reverted; not committed). It calls `runBrowserQuery` with `probe = true` on a FIFO directory and on a regular-file control directory. Both directories are fresh `QTemporaryDir`s. The FIFO is `waiting.wav`, the control is `waiting.wav`.

- FIFO directory: `QCOMPARE(scanned, 0)`, `QCOMPARE(matched, 0)`, `QVERIFY(items.isEmpty())`, `QCOMPARE(probed, 0)`.
- Regular-file control: `QCOMPARE(scanned, 1)`, `QCOMPARE(matched, 1)`, `QCOMPARE(probed, 0)`.

Product-level mutation (`logs/p3c-mutation.patch`): `| QDir::System` added to the filter at `src/core/BrowserCatalog.cpp:351`, with the probe patch applied. The mutation was reverted before the commit.

| Run | Product state | Command (full) | EXIT | Result | Log |
|---|---|---|---|---|---|
| Probe apply (earlier session) | probe patch | `git apply $RUN/logs/p1-fifo-probe.patch` | 0 | applied | `p3b-apply.log` |
| Probe build (earlier session) | probe, no mutation | `cmake --build build --target zene BrowserCatalogQueryTest` | 0 | built | `p3b-probe-build.log` |
| Probe test (earlier session) | probe, no mutation | `cd $W/build/tests && ctest -R '^BrowserCatalogQueryTest$' --output-on-failure` | 8 | 15 passed, 2 failed (the two baseline failures); `fifoIsNotAWalkedItem` PASS | `p3b-probe-test.log` |
| Mutation apply | probe + `\| QDir::System` | `cd $W && sed -i '351s/QDir::Readable)/QDir::Readable \| QDir::System)/' src/core/BrowserCatalog.cpp && git diff -- src/core/BrowserCatalog.cpp > $RUN/logs/p3c-mutation.patch` | 0 | mutation saved | `p3c-mutation.patch` |
| Mutation build | probe + mutation | `cd $W && cmake --build build --target zene BrowserCatalogQueryTest` | 0 | built | `p3c-mutation-build.log` |
| **Mutation test** | probe + mutation | `cd $W/build/tests && ctest -R '^BrowserCatalogQueryTest$' --output-on-failure` | 8 | 14 passed, 3 failed; **`fifoIsNotAWalkedItem` FAIL**: `Actual (fifoResult.scanned): 1`, `Expected (0): 0`, `BrowserCatalogQueryTest.cpp(616)`; the other two failures are the baseline pair | `p3c-mutation-test.log` |
| Revert mutation | probe only | `cd $W && git apply -R $RUN/logs/p3c-mutation.patch` | 0 | reverted | `p3c-revert-mutation.log` |
| Reverted build | probe only | `cd $W && cmake --build build --target zene BrowserCatalogQueryTest` | 0 | built | `p3c-reverted-build.log` |
| **Reverted test** | probe only | `cd $W/build/tests && ctest -R '^BrowserCatalogQueryTest$' --output-on-failure` | 8 | 15 passed, 2 failed; **`fifoIsNotAWalkedItem` PASS** | `p3c-reverted-test.log` |
| Revert probe | none | `cd $W && git apply -R $RUN/logs/p1-fifo-probe.patch` | 0 | reverted | `p3c-revert-test.log` |
| Clean build | none | `cd $W && cmake --build build --target zene BrowserCatalogQueryTest` | 0 | built | `p3c-clean-build.log` |
| Status | none | `cd $W && git status --short` | 0 | empty (clean) | `p3c-status-before-commit.log` |
| Baseline (earlier session) | none | `cd $W/build/tests && ctest -R '^BrowserCatalogQueryTest$' --output-on-failure` | 8 | 14 passed, 2 failed | `p3b-baseline.log` |
| Isolated HOME (earlier session) | none | `cd $W/build/tests && HOME=$RUN/logs/scratch-p3b/home QT_QPA_PLATFORM=offscreen ./BrowserCatalogQueryTest` | 0 | 16 passed, 0 failed | `p3b-isolated.log` |

Mutation row, summarized: with `QDir::System` the FIFO is walked and `fifoIsNotAWalkedItem` FAILs (scanned 1, expected 0). Without it the same test PASSes. The regular-file control inside the test asserts `scanned == 1` on every run.

The baseline and isolated HOME rows come from the earlier session. The `ctest` and `HOME=` command lines are as documented in that session's report; the logs record the outputs and the EXIT lines, but not the exact shell invocation. The isolated run's `SETUP` line shows the created `scratch-p3b/home/Documents/Zene Studio` directory. That scratch directory was removed afterwards (`rm -rf $RUN/logs/scratch-p3b`, EXIT 0, no log).

Standalone Qt probes (`logs/p1-probe-qt6.log`, `logs/p1-probe-qt5.log`; versions in `logs/p1-qt-versions.log`). The RESULT lines are the evidence:

| Qt | without `QDir::System` | with `QDir::System` | RESULT line |
|---|---|---|---|
| 6.12.0 | listed=0 entries=0 | listed=1 entries=1 | `RESULT: as expected` |
| 5.15.19 | listed=0 entries=0 | listed=1 entries=1 | `RESULT: as expected` |

## Baseline failure cause (verified)

The baseline failures are caused by the missing `~/Documents/Zene Studio` working directory. The chain, with each step cited:

1. `src/core/ConfigManager.cpp:764`: the installed working dir is `docs + "/Zene Studio/"`, unless `~/lmms/projects/` exists (`:773`), in which case the legacy path is adopted instead. The test HOME has no `lmms/projects`, so the Documents path applies.
2. `src/core/ConfigManager.cpp:627-629`: `createWorkingDir()` runs only when `hasWorkingDir()` is true. A missing working dir therefore gets no subdirectories created.
3. `src/core/BrowserCatalog.cpp:565-567`: `browserTagStorePath()` returns `workingDir/browser-tags.json`.
4. `src/core/BrowserTagStore.cpp:225-230`: `save()` opens `QSaveFile file(storePath())` and returns false with `m_error` set when `open` fails. The failing tests show the save failing (`setCategory` returned FALSE, and the name/tag query matched 0 expected 1).
5. Controlled comparison: the same binary passes 16/16 with HOME pointing at a copy that has the directory (`p3b-isolated.log`), and fails 2 of 16 with the real HOME that lacks it (`p3b-baseline.log`). The real-HOME directory state is logged in `logs/p3d-home-check.log`:

   `ls -ld "$HOME/Documents/Zene Studio"` -> `EXIT=2`
   (`ls: cannot access '/home/kruzzzzy/Documents/Zene Studio': No such file or directory`, the `EXIT=2` line is the last line of the log.)

Caveat: the step 4 claim that `QSaveFile::open` fails when the parent directory is absent is Qt behavior, not re-checked in this session. The controlled comparison in step 5 is the evidence that does not depend on that.

The two baseline failures are `theQueryMatchesNameTagAndMetadata` (matched 0, expected 1) and `categoryNarrowsTheQueryAndEmptyMeansAny` (`store.setCategory(kick, "Drums")` returned FALSE). They fail with and without the probe.

## Self-check

- **Claim-only closure, no product change:** The commit adds only this report. No product or test code is committed. The probe patch and the `QDir::System` mutation were applied and reverted; `git status --short` is empty after the revert (`p3c-status-before-commit.log`, EXIT 0).
- **Non-vacuity, product-level mutation:** Adding `| QDir::System` at `src/core/BrowserCatalog.cpp:351` makes `fifoIsNotAWalkedItem` FAIL at `BrowserCatalogQueryTest.cpp:616` (scanned 1, expected 0; `p3c-mutation-test.log`, EXIT 8). Reverting it makes the same test PASS (`p3c-reverted-test.log`, EXIT 8, the two baseline failures only). The test therefore detects the FIFO admission it is meant to catch, and it does not depend on the environmental failures.
- **Determinism, by construction:** The test uses fixed names (`waiting.wav` for the FIFO and for the control) in fresh `QTemporaryDir`s. It contains no sleeps and no threads. The FIFO is created with `mkfifo` and is never opened by the test, so the test itself cannot block on a writer. There is no wall-clock timing or scheduler dependence to repeat, because the outcome does not depend on any sleep, timeout, thread, or race.- **One walk source, sibling, sole caller:** One walk source, `readDirectory` (`BrowserCatalog.cpp:347-381`). One sibling filter, `BrowserIndex.cpp:194`. One product caller of `probeAudioMetadata`, `BrowserCatalog.cpp:247`.
- **No collateral:** No product or test change is in the commit. The baseline failures exist without the probe and are explained by the missing working dir (section above). The isolated HOME run passes 16/16. This isolates the two failures to the environment, not the FIFO path.
- **FALSE-POSITIVE code evidence:** `BrowserCatalog.cpp:350-351` (walk filter, no `QDir::System`), `:247` (sole probe caller), `BrowserIndex.cpp:194` (sibling filter), `BrowserMetadata.cpp:145` (probe definition). A FIFO is not returned by `QDirIterator` with these flags, so it never reaches `sf_open`.
- **FileSearchJob note:** `src/gui/FileSearchJob.cpp` has a separate `QDirIterator` at `:82-83` (with `FollowSymlinks`), filtering with `fileInfo.isFile()` at `:93`. Its `task.dirFilters` were not read. It is outside `runBrowserQuery` and does not call `probeAudioMetadata`, so it does not affect this verdict.

## Other findings

- **Symlinks:** not tested. No symlink claim is made.
- **Hang not reproduced:** The mutation run completed with the FIFO walked. The test uses a cheap name match, so the metadata probe is not reached; it therefore shows admission but not a blocking probe.
- **Baseline tag-store failures:** The two baseline failures are an unrelated environment issue, caused by the missing `~/Documents/Zene Studio` working directory (see the Baseline failure cause section). They are not caused by the FIFO finding.
