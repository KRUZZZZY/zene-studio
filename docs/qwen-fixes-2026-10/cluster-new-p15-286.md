FIXED

# cluster new-p15-286 — the index save ignores a short write

**Finding:** `src/core/BrowserIndex.cpp:375-379` — `save()` ignored the byte count returned by
`QSaveFile::write()` and called `commit()` anyway, so a write the kernel cut short (full disk, quota)
published incomplete JSON and replaced the previous index.

## Verdict: REAL, fixed at the cause

- Before, in `BrowserIndex::save()` (`src/core/BrowserIndex.cpp:374`):

  ```cpp
  file.write(QJsonDocument(all).toJson(QJsonDocument::Compact));
  return file.commit();
  ```

  The write's return value is dropped. `QSaveFile::commit()` does not check a short write either —
  measured on this box (Qt 6.12.0, log `logs/scratch/probe2.log`): a 16385-byte write past an
  8-byte `RLIMIT_FSIZE` returns **8** bytes, `commit()` returns **true**, the previous file is
  replaced by 8 bytes. (At 16384 bytes and below, QSaveFile buffers the write, `commit()` catches
  the cut and returns false — which is why the regression document has to be bigger than 16384
  bytes to exercise the defect at all.)
- After (`src/core/BrowserIndex.cpp:381-396`): the document's byte count is checked, a short write
  calls `file.cancelWriting()` and returns `false`, so `scan()` (`src/core/BrowserIndex.cpp:320-327`)
  reports "the index could not be written to …; the previous index is kept" and does **not** publish
  the walked entries. `commit()` still decides the flush/rename half.
- Contract this holds to: `include/BrowserIndex.h:88-92` — "replaces the index when it finishes and
  its store is written (a scan whose store cannot be written keeps the previous index and reports
  the error)". Same shape as the two already-fixed siblings: `writeWhole()`
  (`src/core/DawProjectZip.cpp:209-234`) and the collections store write (`cluster new-p17-271`).
- Callers/siblings checked: `save()` is private and has exactly one caller, `scan()`
  (`src/core/BrowserIndex.cpp:320`), reached from `scanNow()` and the worker `start()`. No other
  writer of `browser-index.json` exists in `src/` (only `storePath()` and `loadLocked()` read it).

## Test matrix

| Path | Case | Where |
|---|---|---|
| normal | a full-size index document is written and read back (257 entries, > 32 KiB) | `BrowserIndexStoreWriteTest::aShortIndexStoreWriteIsRefusedNotPublished` |
| failure | write cut short by the kernel: save refuses, `status().error` says the store could not be written, entries and store bytes unchanged | same test |
| restart | `reload()` reads the kept index, not a truncated document | same test |
| exhaustion / reconfiguration | not applicable: the store path is the pre-existing `ZENE_BROWSER_INDEX` seam, unchanged; no limit was added (`MaxEntries`, `MaxAnalysed` untouched) | — |
| audio thread | `save()` runs in `scan()` on the worker (`start()`) or the caller (`scanNow()`), never in a process/play path; the change adds no allocation, lock, wait or log | — |
| platform | product code is Qt-only (builds on Windows/macOS/MSVC); the fault injection is POSIX (`RLIMIT_FSIZE`, `SIGXFSZ`) behind `#if defined(Q_OS_UNIX)`, `QSKIP` elsewhere like the existing siblings | `tests/src/core/ClipConsolidateTest.cpp:448` precedent |

## Commands and their exit codes (all logs under the run's `logs/`)

| Step | Command | EXIT | Log |
|---|---|---|---|
| RED (test only, product reverted) | `cmake --build build --target BrowserIndexStoreWriteTest -j4` | 0 | `build-14-red.log` |
| RED run | `cd build/tests && ctest -R BrowserIndexStoreWriteTest --output-on-failure` | **8** | `test-15-red.log` |
| failing assertion in that run | `'savedError.contains(QStringLiteral("could not be written"))' returned FALSE. (a save whose write was cut short was not reported as a failed save: '')` at `BrowserIndexStoreWriteTest.cpp:170` | — | `test-15-red.log` |
| GREEN build | `cmake --build build --target BrowserIndexStoreWriteTest BrowserCatalogQueryTest -j4` | 0 | `build-15-green.log` |
| GREEN run | `cd build/tests && ctest -R BrowserIndexStoreWriteTest --output-on-failure` | **0** | `test-18-green.log` |
| family | `cd build/tests && ctest -R "BrowserIndexStoreWriteTest\|BrowserCatalogQueryTest\|BrowserTempoKeyTest\|BrowserCollectionStoreWriteTest\|ControlBrowserCommandsTest\|LibraryBrowserTest\|BrowserCatalogTest" --output-on-failure` | 8 (only the pre-existing `BrowserCatalogQueryTest` failures, see Other findings) | `test-16-family-green.log` |

### Step 4b — the test catches the bug

```
git diff -- src/core/BrowserIndex.cpp > logs/fix.patch        EXIT=0
git apply -R logs/fix.patch                                   EXIT=0
cmake --build build --target BrowserIndexStoreWriteTest -j4   EXIT=0   (build-14-red.log)
ctest -R BrowserIndexStoreWriteTest --output-on-failure       EXIT=8   (test-15-red.log)   FAILS
git apply logs/fix.patch                                      EXIT=0
cmake --build build --target BrowserIndexStoreWriteTest …     EXIT=0   (build-15-green.log)
ctest -R BrowserIndexStoreWriteTest --output-on-failure       EXIT=0   (test-18-green.log) PASSES
```

The first RED attempt was a wrong test and was fixed: with a small index document (250 bytes) Qt
buffers the write, so `commit()` fails on its own and the test passed without the fix. The test now
builds 257 long-named entries so one document is > 32 KiB (it asserts `stored.size() > 32768`), and
it asserts and prints outside the `RLIMIT_FSIZE` scope, because while the cap is in force a redirected
log file stops at 8 bytes and swallowed QTest's own failure message.

## Gates

`python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` — **EXIT=1, it cannot run in
this lane**:

```
bunny-guard: 'git -C …/qwen/gate-baselines/tree checkout -q --detach 9913d36d53…' is not allowed in a Bunny lane
…
subprocess.CalledProcessError: … returned non-zero exit status 97.        (logs/gates-1.log)
```

It computes its base ratchet listing by checking out the base into `qwen/gate-baselines/tree` and
caching `<base>.json` there; that path is on a read-only mount here (`touch .probe` →
`Read-only file system`), and the lane's git shim refuses `checkout --detach`/`worktree`. Every gate
it wraps was therefore run directly, with its own committed baseline:

| Gate | Command | EXIT | Log |
|---|---|---|---|
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 | `gate-4-fork-sources.log` |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 | `gate-4-all-sources.log` |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 | `gate-4-no-upstream-regression.log` |
| check-namespace | `tests/scripted/check-namespace` | 0 | `gate-4-check-namespace.log` |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 | `gate-4-no-tautology.log` |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 | `gate-4-doc-figures.log` |
| file-length | `bash tests/file-length-gate.sh --check` | 0 | `gate-4-file-length.log` |
| complexity | `PYTHONPATH=logs/pylib bash tests/complexity-gate.sh --check` | 0 | `gate-6-complexity.log` |

`lizard` had to be installed into a writable directory (`python3 -m pip install --target logs/pylib
lizard` → lizard 1.26.1, `pip-lizard-3.log`) because `~/.local/lib` is read-only here; the gate itself
is unmodified.

Registration: the new test file is in `tests/fork-sources.txt`, `tests/all-sources.txt` (both in the
recipe's sorted position — both gates print REPRODUCES/PASS) and `tests/CMakeLists.txt`. The
registration gates could only pass **after** the commit, because the recipe derives the list from
`git diff --diff-filter=A … HEAD`, which does not see an untracked file (`gate-3-*.log`, the earlier
attempt).

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it (exit codes).**
  Reverted: `test-15-red.log`, EXIT=8, `'savedError.contains("could not be written")' returned FALSE
  (a save whose write was cut short was not reported as a failed save: '')`. Restored:
  `test-18-green.log`, EXIT=0. `git apply -R logs/fix.patch` EXIT=0 and `git apply logs/fix.patch`
  EXIT=0 around them.
- **The test is deterministic.** No timing, sleeps or threads: the fault is the kernel's
  `RLIMIT_FSIZE` cap (8 bytes, `SIGXFSZ` ignored) held for one scope by `ScopedFileSizeLimit`, and
  the assertion that the document is bigger than QSaveFile's 16384-byte write buffer is a property of
  the fixture, not of the machine's speed. The store path is injected through the existing
  `ZENE_BROWSER_INDEX` environment seam.
- **The fix is at the cause and complete.** `save()` is private with one caller (`scan()`); a refused
  save leaves `m_entries` and the store as they were and sets `status().error`, which is the contract
  `BrowserIndex.h:88-92` states. The two siblings that already check the byte count
  (`DawProjectZip.cpp:209`, collections store) are unchanged and still pass
  (`test-16-family-green.log`: `BrowserCollectionStoreWriteTest` Passed).
- **No collateral damage.** The happy path is unchanged (`test-16-family-green.log`:
  `BrowserTempoKeyTest`, `BrowserCatalogTest`, `ControlBrowserCommandsTest`, `LibraryBrowserTest`,
  `BrowserCollectionStoreWriteTest` all Passed); `failedIndexSaveKeepsThePreviousIndex` (the
  unwritable-store sibling) still passes; no limit or bound was added, so no caller that legitimately
  writes a large index is affected. Old `browser-index.json` files load as before — the reader
  (`loadLocked`) is untouched.
- **Audio thread.** Nothing was added to process/play code; the change is inside a worker-thread file
  writer and adds one `QByteArray` local and a `qint64` compare.
- **Undo / commands.** No command was added or changed; `browser.index_scan`/`browser.index_status`
  behave as before (the same `status().error` they already report).
- **Verdict evidence for a non-REAL claim.** Not applicable: the verdict is FIXED, and the defect is
  shown in the pre-fix source (`BrowserIndex.cpp:375-379` at HEAD before this commit) and reproduced
  by the RED run above.
- **Sibling commands.** No file under `src/core/ControlCommands*` was changed, so the sibling set is
  unaffected; `browser.index_scan`/`browser.index_status` surface the same `status()` they always did.
- **The `feature_check.py` row of `submit_check.py` FAILs in this session** for the read-only
  `gate-baselines` mount quoted above, not for a gate result: all eight gate scripts it wraps exit 0
  when run directly (table above).

## Other findings

- **Pre-existing, not mine:** `BrowserCatalogQueryTest::theQueryMatchesNameTagAndMetadata` and
  `::categoryNarrowsTheQueryAndEmptyMeansAny` fail in this environment with or without this change.
  Proof: with the unmodified test file (pristine `git checkout --`) and no product change, the same
  two slots fail and the totals are `14 passed, 2 failed` (`test-5-baseline.log`, EXIT=8); with this
  change it is `15 passed, 2 failed` (`test-16-family-green.log`). Cause: both slots use the default
  tag store, `browserTagStorePath()` = `ConfigManager::workingDir()/browser-tags.json`
  (`src/core/BrowserCatalog.cpp:563-568`), and that directory (`~/Documents/Zene Studio/`) does not
  exist in this sandbox (`ls`: `No such file or directory`), so `BrowserTagStore::add`/`setCategory`
  cannot write. Not fixed here: outside this finding's scope, and it is an environment gap rather
  than a product defect.
- `tests/file-length-gate.sh --check` failed mid-run when the new test was written into
  `BrowserCatalogQueryTest.cpp` (512 code lines > 500, `gate-file-length.log`); the test was moved to
  `BrowserIndexStoreWriteTest.cpp`, exactly as `cluster new-p17-270` did for the collections store.
