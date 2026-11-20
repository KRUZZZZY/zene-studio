FALSE-POSITIVE

# cluster new-p15-287: FIFO named like audio is not a walked item

Branch `bugs/qwen-new-p15-287`, base commit `fa4a5f2e78`. No product code or tests changed. Log paths below are relative to the run's `logs/` folder (outside the repo, under `state/runs/new-p15-287.r1/logs/`).

## (a) Finding

The verbatim finding text was not in the packet, so it is not quoted here. The claim under test, as the packet and commit subject state it: a FIFO named like an audio file (`waiting.wav`) can enter the audio query path and be probed. The verdict is that it cannot.

## (b) Code evidence

- `src/core/BrowserCatalog.cpp:350-351`: the only walk filter in the query path, `QDirIterator entries(dirPath, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable)`. No `QDir::System`, so FIFOs and other special files are not listed.
- `src/core/BrowserCatalog.cpp:347-381`: `readDirectory`, the only function that appends walked entries from a `QDirIterator`. Its two append sites are `:360` (LV2 bundle entries) and `:378` (file entries). Both come from the `entries` iterator opened at `:350`.
- `src/core/BrowserIndex.cpp:194`: sibling `openListing()` filter, the same flags without `QDir::System`.
- `src/core/BrowserCatalog.cpp:247`: the sole product caller of `probeAudioMetadata` (`logs/p2-probe-callers.log`). Test-only callers are `tests/src/core/BrowserCatalogTest.cpp:215, :233, :237`.
- `src/core/BrowserMetadata.cpp:145`: the definition of `probeAudioMetadata`, not a filter.
- Chain: `runBrowserQuery:605 -> considerPath:311 -> probeAndAppend:247 -> probeAudioMetadata` (`logs/p2-callers-chain.log`, `logs/p2-notes.md`).
- `rg 'QDir::System'` over both walk files: no matches (`logs/p2-walk-filters.log`).

## (c) Real-surface probe evidence

Temporary probe test `fifoIsNotAWalkedItem` (patch: `logs/p1-fifo-probe.patch`, not committed). It runs `runBrowserQuery` with `probe = true` on two directories:

| Directory | scanned | matched | items | probed |
|---|---|---|---|---|
| FIFO `waiting.wav` | 0 | 0 | empty | 0 |
| Regular-file control `waiting.wav` | 1 | 1 | — | 0 |

The patch asserts these values with `QCOMPARE`/`QVERIFY`. The log shows `fifoIsNotAWalkedItem ... PASS` (`logs/p1-test-probe.log`). The log does not print the numbers, so they are established by that PASS, not by a printed value.

## (d) Standalone Qt probes

| Qt | without `QDir::System` | with `QDir::System` | log |
|---|---|---|---|
| 6.12.0 | listed=0 entries=0 | listed=1 entries=1 | `logs/p1-probe-qt6.log` |
| 5.15.19 | listed=0 entries=0 | listed=1 entries=1 | `logs/p1-probe-qt5.log` |

Versions: `logs/p1-qt-versions.log`.

## (e) Commands and exit codes

- Probe ctest, `BrowserCatalogQueryTest` with the probe patch: 15 passed, 2 failed (`logs/p1-test-probe.log`). The `fifoIsNotAWalkedItem` entry passed. The two failures are the baseline tag-store failures (see Other findings). **Exit code: not recorded in any log.** The packet expects 8; this is unverified.
- Baseline ctest, `BrowserCatalogQueryTest` without the probe: 14 passed, 2 failed (`logs/p1b-test-baseline.log`, summary `logs/p1b-summary.log`). **Exit code: not recorded.** The packet expects 8; unverified.
- Isolated HOME, `BrowserCatalogQueryTest`: 16 passed, 0 failed (`logs/p1b-test-isolated.log`). **Exit code: not recorded.** The packet expects 0; unverified.
- Build of the probe test: `logs/p1-build-probe.log` shows `BrowserCatalogQueryTest` linked. No exit line is recorded there either.

The exact command lines were not recorded in the logs, so they are not reproduced here.

## Self-check

- **reversed-test:** N/A as a claim-only closure. The positive control is the regular-file case (scanned=1, matched=1). The `|QDir::System` mutation is shown only at the standalone Qt level (`logs/p1-probe-qt6.log`, `logs/p1-probe-qt5.log`): admitting `QDir::System` lists the FIFO. No product-level mutation run is logged, so the claim that the product test would see a FIFO if admitted rests on the Qt probe plus the control, not on a product mutation.
- **determinism:** The probe uses a FIFO plus a regular-file control. It has no sleeps (`logs/p1-fifo-probe.patch`). It calls `mkfifo` but never opens the FIFO, so it cannot block.
- **cause/completeness:** One walk source (`readDirectory`, `BrowserCatalog.cpp:347-381`), one sibling filter (`BrowserIndex.cpp:194`), and one probe caller (`BrowserCatalog.cpp:247`). All probes are reached through `runBrowserQuery:605`.
- **collateral:** No product change in this report. The baseline failures exist without the probe (`logs/p1b-test-baseline.log`), and the isolated-HOME run passes 16/16 (`logs/p1b-test-isolated.log`). That isolates them to the environment, not to the FIFO path.
- **FALSE-POSITIVE evidence:** `BrowserCatalog.cpp:350-351` (filter), `:347-381` (only walk source), `:247` (sole probe caller), `BrowserIndex.cpp:194` (sibling filter), `BrowserMetadata.cpp:145` (probe definition). Logs: `p1-test-probe.log` (PASS), `p1-probe-qt6.log`, `p1-probe-qt5.log`, `p2-notes.md`.

## Other findings

- **Baseline tag-store failures:** `theQueryMatchesNameTagAndMetadata` (matched 0, expected 1) and `categoryNarrowsTheQueryAndEmptyMeansAny` (`setCategory` returned false) fail with and without the probe. They pass in the isolated-HOME run (16/16). The packet attributes them to a missing `~/Documents/Zene Studio` working directory. **Unverified:** no log shows that path, the isolated HOME setup is not logged, and the source has no such path reference (only copyright headers). Treat it as a hypothesis.
- **`src/gui/FileSearchJob.cpp`:** a separate `QDirIterator` at `:82-83` (with `FollowSymlinks`). It filters with `fileInfo.isFile()` at `:93`. Its `task.dirFilters` were not read. It is outside `runBrowserQuery` and does not call `probeAudioMetadata` (`logs/p2-probe-callers.log`), so it does not affect this verdict.
- **Symlinks:** not tested. No symlink claim is made.
