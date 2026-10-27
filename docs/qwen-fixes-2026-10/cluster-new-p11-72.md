FIXED

# Cluster new-p11-72 — Recovery can delete an unreadable pending save

## Verdict

FIXED. The finding is real on the branch base (`639e1d23e`). A `<file>.new` that could not be opened, or whose read failed, was reported as an incomplete document, and `recoverPendingWrite()` then deleted it. The fix keeps such a file in place, reports it, and adopts it on a later open once it can be read.

## Evidence (file:line on the fixed tree)

- `src/core/DataFile.cpp:286-290` (`isCompleteDocument`): the open-failure branch now sets `*reason = "unreadable"` (set at :288). Before the fix it was `return false` with no reason, which made the caller discard the file.
- `src/core/DataFile.cpp:299-304`: a read error (`f.error() != QFileDevice::NoError` after `readAll()`, :299-302) also sets `"unreadable"`. An empty file with no error still returns `false` and is discarded as before.
- `src/core/DataFile.cpp:320-331` (container branch): if `readDocumentContainer()` fails, the file is opened again. One that no longer opens is marked `"unreadable"` (:328); a file that reopens is treated as truncated as before.
- `src/core/DataFile.cpp:383-389` (`recoverPendingWrite`): the new `else if( reason == "unreadable" )` branch prints `Could not read <pending> ... it was not checked, so it is left in place` and does not remove the file. The truncation branch (`Discarded incomplete`, :390) is unchanged.
- Regression: `tests/src/core/DataFileSaveIntegrityTest.cpp:685` (new last method `unreadableNewFileIsPreservedNotDeleted`). It writes a complete, recognised pending save, removes its read permission (`QFile::setPermissions(temp, WriteOwner)`), and opens the project. It asserts no `Discarded incomplete` line, a `Could not read` line, the `.new` still present, the canonical project unchanged, and then, after read permission is restored, that the preserved save is adopted (`s5probe == "pending"`). It QSKIPs only if the process can still open a file with no read permission (root, or a platform that ignores read bits), so it never passes without performing the refused open.

No wall-clock timing, sleeps or races are used. The only environment dependency is the permission bit, and the QSKIP covers that.

## Commands and exit codes

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-72.r1/logs`

| Step | Command (run from worktree, ctest from `build/tests`) | EXIT | Log |
|---|---|---|---|
| RED build, attempt 1 | `cmake --build build --target DataFileSaveIntegrityTest -j4` | 2 | build-1-red.log (ccache could not create its temp file under `zene-bunny-fix/dsh`, a write outside the jail; no compile error) |
| RED build, attempt 2 | `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp/ccache cmake --build build --target DataFileSaveIntegrityTest -j4` | 0 | build-2-red.log (0 `ccache: error` lines) |
| **RED run** | `ctest -R '^DataFileSaveIntegrityTest$' --output-on-failure` | **8** | test-1-red.log: `FAIL! : ...unreadableNewFileIsPreservedNotDeleted() '!report.contains( "Discarded incomplete" )' returned FALSE`; `Totals: 16 passed, 1 failed, 0 skipped` |
| GREEN build zene | `CCACHE_TEMPDIR=... cmake --build build --target zene -j4` | 0 | build-3-green.log |
| GREEN build test | `CCACHE_TEMPDIR=... cmake --build build --target DataFileSaveIntegrityTest -j4` | 0 | build-4-green.log |
| **GREEN run** | `ctest -R '^DataFileSaveIntegrityTest$' --output-on-failure` | **0** | test-2-green.log: 100% tests passed, 1 out of 1 |
| GREEN run, direct | `./DataFileSaveIntegrityTest` (in `build/tests`) | 0 | test-3-green-direct.log: `Totals: 17 passed, 0 failed, 0 skipped` |
| Family | `ctest -R '^(DataFileFormatTest\|DataFileSaveIntegrityTest\|ProjectContainerSaveTest\|ProjectContainerTest)$' --output-on-failure` | **0** | test-4-family-green.log: 4/4 passed |

Targets built for the family: `DataFileFormatTest` (EXIT 0), `ProjectContainerSaveTest` (EXIT 0), `ProjectContainerTest` (EXIT 0), all in build-5-*.log.

`AudioFileProcessorView` (named in the brief) is not a test target in this build: `cmake --build build --target AudioFileProcessorView` returned EXIT 2 with `No rule to make target 'AudioFileProcessorView'` (build-5-AudioFileProcessorView.log), and `ctest -N` lists no such test. It is a reference source file under `tests/reference/`, so it was not counted.

Affected-family test count: ctest runs 4 tests (DataFileFormatTest, DataFileSaveIntegrityTest with 17 functions, ProjectContainerSaveTest, ProjectContainerTest), 4 of 4 passing.

### Step 4b: prove the test catches the bug

| Step | Command | EXIT |
|---|---|---|
| Save the product diff | `git diff -- src/core/DataFile.cpp > .../logs/fix.patch` | 0 |
| Revert the fix | `git apply -R .../logs/fix.patch` | 0 |
| Rebuild zene, test | `cmake --build build --target zene -j4`; `... --target DataFileSaveIntegrityTest -j4` | 0; 0 |
| **Run on reverted code** | `ctest -R '^DataFileSaveIntegrityTest$' --output-on-failure` | **8** (test-5-revert-red.log: same `Discarded incomplete` assertion) |
| Re-apply the fix | `git apply .../logs/fix.patch` | 0 |
| Rebuild zene, test | as above | 0; 0 |
| **Run on fixed code** | ctest as above (test-6-reapply-green.log), and the binary directly (test-7-reapply-direct.log) | **0**; **0** (17 passed) |

### Gates

`feature_check.py` could not be run as given: it creates its log folder at `<worktree>/../<name>-feature-check`, which is `projects/zene-studio-program/`, outside the write jail (first attempt: `PermissionError` on `zene-qwen-new-p11-72-feature-check`; that traceback was not kept in a log file). I ran an otherwise unchanged copy (`/home/kruzzzzy/.cache/zene-tmp/fc/feature_check_wt.py`) with three lines altered: `ROOT` points at the bunny-fix folder so `FIX-BASE` and the cached base baselines (`639e1d23e`, already present) are read; the log folder points into the LOG FOLDER. All gate commands and checks are the same.

- `python3 -I .../feature_check_wt.py <worktree>` → EXIT 0, output in gates-1.log. Rows: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures all PASS; complexity "no new item in your files" PASS; file-length "no new item in your files" PASS; `RESULT: PASS`.

## Files committed

- `src/core/DataFile.cpp` (product fix)
- `tests/src/core/DataFileSaveIntegrityTest.cpp` (regression test; file already registered in `tests/fork-sources.txt`)
- `tests/upstream-modifications.txt` (one line recording the `DataFile.cpp` change with its reason; the file is inherited, so the entry is required)
- `docs/qwen-fixes-2026-10/cluster-new-p11-72.md` (this report)

## Self-check

1. **Each new test fails with the product change reverted, then passes restored.** Yes. Reverted: `git apply -R` then ctest EXIT 8 with the assertion above (test-5-revert-red.log). Restored: `git apply`, then ctest EXIT 0 (test-6-reapply-green.log).
2. **Acceptance criteria proven by a test you ran.** The finding asked for: (a) invalid content and I/O failure to be distinguished, and (b) a pending save that could not be checked to be preserved. (a) and (b) are both shown by `unreadableNewFileIsPreservedNotDeleted` (test-2-green.log, 17/17 direct in test-3). The existing truncation, oversized, unrelated-XML, deep-nesting and orphan-adoption tests (`truncatedNewFileIsDiscardedWithReport`, `unrelatedXmlNewFileIsNotAdopted`, `deeplyNestedNewFileIsRefusedBeforeParsing`, `newFileDeclaringAnEnormousSizeIsRefusedBeforeDecompressing`, `orphanedNewFileIsAdoptedWithReport`) still pass, so the behaviour for invalid content is unchanged.
3. **Undo and reversibility.** Not affected. The change is confined to `DataFile`'s constructor-time recovery of a `.new` file. No command, undo entry or reversibility class is added or changed.
4. **Audio thread.** Nothing on the audio thread. `recoverPendingWrite()` runs only from the `DataFile(fileName)` constructor on project open (`src/core/DataFile.cpp:471`). The new code adds a `qWarning()` on the unreadable path, which is the same kind of logging the existing branches already do, on a non-audio path.
5. **Old projects still load.** The canonical-file load path (`src/core/DataFile.cpp:473-522`) is unchanged. Only the handling of a `.new` temp that cannot be opened or read changes, and that case previously deleted the file. Valid, truncated, oversized and unrelated temps follow the same code paths as before, covered by the existing tests above.
6. **Portability.** `QFileDevice::NoError` is valid in Qt 5.15 and Qt 6. The test uses `QFile::setPermissions` and `QSKIP`. On a platform where read bits are ignored (Windows, or root on Linux) the open is not refused, so the test skips rather than passing without the check.

7. **The test is deterministic (no wall-clock timing, sleeps or races).** Yes. The test uses no timers, sleeps, threads or waits. Its only input variation is the file permission bit (`QFile::setPermissions`), set and restored in the test body. The `MessageRecorder` helper in the same file captures log lines synchronously. The pending save's content is a fixed literal, so the adopted probe value (`pending`) is known in advance.
8. **The fix is at the cause, and every caller and sibling was checked.** The cause is the `isCompleteDocument()` contract: it returned `false` with no reason for "could not open", and `recoverPendingWrite()` treats any `false` without a reason as "incomplete". The fix gives refusals their own reason rather than changing that caller's contract. The only caller of `isCompleteDocument` is `recoverPendingWrite` (`src/core/DataFile.cpp:363`). The sibling paths into the same function were checked: the `readAll()` read error (`:299-304`), the container branch's second open (`:320-331`), and the size checks, which already used their own reason. The canonical-file load (`src/core/DataFile.cpp:473-522`) is a separate open and was not changed, since it reports its failure directly and never deletes anything.
9. **Verdict is FIXED, not ALREADY-FIXED or FALSE-POSITIVE,** so no "already fixed" citation is needed. The finding's line is `src/core/DataFile.cpp:~275`; the code it names is now at `:282-331`, and the cited evidence above is at those lines.

Not covered by the regression test (stated plainly): the read-error branch (`f.error()` after a successful open) and the container reopen branch (`readDocumentContainer` failing after the file opened). Both need an I/O fault that cannot be injected from the test without a new seam, so they are reasoned from the code and not run.

## Known environment issues (not product defects)

- ccache could not create temp files under `zene-bunny-fix/dsh/home/ccache-tmp-r6` (outside the write jail). Builds were run with `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp/ccache`; the second build reported zero ccache errors.
- `feature_check.py` writes its logs next to the worktree (outside the jail). `submit_check.py` calls `feature_check.py` directly, so its "gates" row will report the same `PermissionError` until that script writes its logs inside the worktree or the run folder. The gates themselves pass (EXIT 0 above).
