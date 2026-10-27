FIXED

# Cluster new-p11-72 — Recovery can delete an unreadable pending save

## Verdict

FIXED. The finding is real on the branch base (`639e1d23e`). A `<file>.new` that could not be opened, or whose read did not return what the file holds, was reported as an incomplete document, and `recoverPendingWrite()` then deleted it. The fix keeps such a file in place, reports it, and adopts it on a later open once it can be read.

Round 3 (automated proof check, DIAGNOSTIC CHECKPOINT) asked for negative controls that fail without the fix, portable fault injection, and coverage of the first read, the second read, repeated failures and malformed/oversized controls. The loop's negative control had reverted the product changes and ran five collateral tests that do not reach the changed branch, so they passed. This round adds a differential case to each of those five files, so each one fails with the fix removed. The fault is now injected portably (on every platform, with no skips) and the regression set covers both reads.

## Evidence (file:line on the committed tree, `eebd92f17`)

Recovery (`src/core/DataFile.cpp`):
- `:226` (`armPendingReadFault`): test seam `ZENE_TEST_PENDING_READ_FAULT`. Closes the pending file after its open, before the read, so the read gives back nothing.
- `:309-312` (`isCompleteDocument`, plain pending file): `expected = f.size()` before the read. The file is unreadable if QFile reports an error or the read returns `bytes.size() != expected`. Before this round only the error was checked, so a short read was taken as a truncated save and deleted.
- `:338` (container branch): `readDocumentContainer(..., &unreadable)`, then `if( unreadable ) *reason = "unreadable"`. The round-2 flag path is unchanged.
- `:395-401` (`recoverPendingWrite`): the `"unreadable"` branch prints `Could not read <pending> ... it was not checked, so it is left in place` and keeps the file.

Zip reader (`src/core/DawProjectZipRead.cpp`):
- `:147-151` (`armTestReadFault`): test seam `ZENE_TEST_CONTAINER_READ_FAULT`. Closes the file after its open, before the read. Portable: `QFile::close()` needs no platform call. The Unix-only `dup2` seam from round 2 is removed.
- `:160-168` (`refuseUnreadable`): one place that sets `*unreadable = true` and writes `cannot read <path>: <QFile error or "it gave back N of M bytes">`. Used for an open failure and for a failed or short read (factored out to keep `readWhole` under the complexity gate).
- `:171-190` (`readWhole`): `expected = source.size()` before the read; a read that errors or returns fewer bytes than `expected` is refused through `refuseUnreadable`.
- The strict and tolerant readers and `readContainer`/`readDocumentContainer` pass the flag down (round 2, unchanged).

Regressions. Each file below has a case that fails with the fix removed:
- `tests/src/core/DataFileContainerRecoveryTest.cpp:146` `unreadableContainerNewFileIsPreservedNotDeleted`: the second read of a valid v2 `.new` fails; preserved; adopted once readable.
- `:236` `unreadablePendingFileIsPreservedNotDeleted`: the initial read of a plain `.new` fails; preserved, canonical project untouched; adopted once readable.
- `:280` `repeatedContainerReadFailuresKeepTheNewFileUntilAReadableAttempt`: three failed recoveries keep the `.new`, then one readable attempt adopts it.
- `:333` `oversizedContainerNewFileIsRefusedAndKept` (control, passes both ways): the container is refused by its own size limit and kept.
- `:194` `truncatedContainerNewFileIsDiscardedWithReport` (control, passes both ways): a truncated container is still discarded.
- `tests/src/core/ProjectContainerTest.cpp:333` `aFailedReadIsNamedAsARead`: `readContainer` names a failed read, and an opaque file is still "malformed".
- `tests/src/core/ProjectContainerEntriesTest.cpp:464` `aFailedDocumentReadIsNamedAsARead`: the document entry point.
- `tests/src/core/ProjectContainerSaveTest.cpp:529` `aFailedReadOfASavedContainerIsNamedAsARead` (with an ordinary read as control) and `:553` `aTruncatedSavedContainerIsRefusedAsMalformed` (control).
- `tests/src/core/DawProjectDeflateImportTest.cpp:633` `aFailedReadIsNamedAsARead`: both the tolerant and strict readers, with a valid import as control.
- `tests/src/core/DawProjectInterchangeRoundTripTest.cpp:296` `aFailedReadOfTheStrictReaderIsNamedAsARead`: the strict reader, with a malformed control.
- `tests/src/core/DataFileSaveIntegrityTest.cpp:685` `unreadableNewFileIsPreservedNotDeleted` (round 1, unchanged): the permission-denied open. It skips only when the process can still read a 0-permission file (root, or a platform without read bits).

No wall-clock timing, sleeps or threads anywhere. Every fault is set and cleared by the test around one call, and the fixture documents are fixed literals.

## Negative controls (the proof)

Two controls were run on the final code. Neither changed a test.

**Control A: only this round's fix removed (size comparisons out; seams and round-2 flags kept).** The seam still empties the read, so the removed check is the only thing that keeps the file.
- Build: `zene` and the 8 targets, all EXIT 0 (`negctl-size-final/build-*.log`).
- `ctest` (8 targets): **EXIT 8**, 6 of 8 failed (`negctl-size-final/ctest-family.log`):
  - `DataFileContainerRecoveryTest`: `unreadableContainerNewFileIsPreservedNotDeleted`, `unreadablePendingFileIsPreservedNotDeleted`, `repeatedContainerReadFailuresKeepTheNewFileUntilAReadableAttempt`. Each: `!report.contains("Discarded incomplete")` returned FALSE, i.e. the `.new` was deleted.
  - `ProjectContainerTest::aFailedReadIsNamedAsARead`, `ProjectContainerEntriesTest::aFailedDocumentReadIsNamedAsARead`, `ProjectContainerSaveTest::aFailedReadOfASavedContainerIsNamedAsARead`, `DawProjectInterchangeRoundTripTest::aFailedReadOfTheStrictReaderIsNamedAsARead`, `DawProjectDeflateImportTest::aFailedReadIsNamedAsARead`: the refusal says `… is too short to be a ZIP container`, not `cannot read`.
- Passing without the fix, as they should: the oversized and truncated controls, the permission test, and `DataFileFormatTest`.

**Control B: the full revert the loop performs** (`git diff 639e1d23e -- src include` reversed; every product change since base removed, seams included; tests kept). `negctl-full/`.
- Build: all EXIT 0. `ctest`: **EXIT 8**, 7 of 8 failed (`negctl-full/ctest-family.log`). Only `DataFileFormatTest` passed, which doesn't touch the read path.
- In this control the seam is absent, so the fault-injection tests fail for that reason too. Control A is the one that isolates the fix; control B shows the set as a whole fails without the change.

After each control the product files were restored and verified: `cmp` identical to the saved copies (`RESTORED_IDENTICAL`), and `git diff -- src include` byte-identical to the saved patch (`PRODUCT_DIFF_IDENTICAL`). Each restore was followed by a rebuild and a GREEN run.

One invalid attempt, recorded for honesty: the first Control A build failed (`-Werror=unused-variable` in `DataFile.cpp`, `negctl-size/build-zene.log`), so `ctest` ran the stale binaries and passed 8/8. That run was discarded. The control was re-applied with the declaration removed, the build was checked to exit 0, and the run above was taken on that build.

## Commands and exit codes (round 3)

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-72.r3/logs`. Earlier rounds are in `new-p11-72.r2/` and `new-p11-72.r1/`.

| Step | Command | EXIT | Log |
|---|---|---|---|
| Build zene (fix) | `cmake --build build --target zene -j4` | 0 | build-r3-1-green.log |
| Build 8 targets | DataFileContainerRecoveryTest, DataFileSaveIntegrityTest, DataFileFormatTest, ProjectContainerTest, ProjectContainerEntriesTest, ProjectContainerSaveTest, DawProjectDeflateImportTest, DawProjectInterchangeRoundTripTest | 0 each | build-r3-2-*.log |
| **GREEN, family** | `ctest -R '^(DataFileFormatTest\|DataFileSaveIntegrityTest\|DataFileContainerRecoveryTest\|ProjectContainerTest\|ProjectContainerEntriesTest\|ProjectContainerSaveTest\|DawProjectInterchangeRoundTripTest\|DawProjectDeflateImportTest)$' --output-on-failure` | **0** | test-r3-1-green-family.log: 8/8 |
| GREEN, totals | `./<target>` (direct) | 0 | direct-r3-green-*.log: DataFileContainerRecoveryTest 7, DataFileSaveIntegrityTest 17, ProjectContainerTest 13, ProjectContainerEntriesTest 16, ProjectContainerSaveTest 17, DawProjectDeflateImportTest 12, DawProjectInterchangeRoundTripTest 60 — all passed, 0 failed |
| Control A (fix removed) | build + ctest as above | build 0; **ctest 8** | negctl-size-final/ |
| Control B (base) | build + ctest as above | build 0; **ctest 8** | negctl-full/ |
| GREEN after restore | build + ctest | 0 | test-r3-5-final-family.log: 8/8 |

Refactor step: `readWhole` at CCN 11 failed the complexity gate (`gates-r3-1.log`). It was split into `refuseUnreadable`, the build and family were re-run (`test-r3-4-refactor-family.log`, EXIT 0), and the gates re-run.

### Gates

`feature_check.py` writes its logs next to the worktree, outside the write jail, so it cannot run as given. I ran an identical copy with two lines changed: `ROOT` points at the bunny-fix folder (reads `FIX-BASE` and the cached base baselines, `639e1d23e`), and the log folder points into the run's `logs/feature-check/`.
- `gates-r3-1.log` (before the refactor): complexity FAIL (`readWhole` CCN 11).
- `gates-r3-2.log` (after the refactor): all PASS, EXIT 0.
- `gates-r3-3.log` (after recording the two inherited test files in `upstream-modifications.txt`): all PASS, EXIT 0. `fork-sources` and `all-sources-reproduce` PASS; `no-upstream-regression` PASS; complexity and file-length: "no new item in your files".

### Real-time boundary audit

Recovery and the zip readers run only from constructors and load or command paths:
- `DataFile(fileName)` (which runs `recoverPendingWrite`) is built from GUI and command code (`src/gui/…`, `src/core/Song.cpp`, `ControlCommands*.cpp`, `ProjectJournal.cpp`, and others).
- The one `DataFile` in an audio-adjacent object is `PresetPreviewPlayHandle`'s constructor. It is called from `src/gui/FileBrowser.cpp:850-858` on the GUI thread, before the handle is played (`src/core/PresetPreviewPlayHandle.cpp:156`).
- `readContainer`, `readDocumentContainer`, `dawProjectZipRead(Tolerant)` have no caller in an audio callback (`rg` over `src` and `include`).
- The new code adds an `expected` size read and `qgetenv` calls on those same load paths. No allocation or I/O is added to any process or render path.

### Portability

- The seams use `QFile::close()` and `qgetenv` only; no `dup2`, no `fcntl.h`, no `Q_OS_UNIX`. The new regression cases run on every platform and never skip.
- The only `QSKIP` left is the round-1 permission test, which skips only when the process can still read a 0-permission file.

## Files committed

Round 1: `ee4257cee` (`src/core/DataFile.cpp`, `tests/src/core/DataFileSaveIntegrityTest.cpp`, `tests/upstream-modifications.txt`), `0370312a9` (report).
Round 2: `de9ee899f` (seam and RED regression, Unix dup2 seam), `ff8df3eda` (fix and registration), `1be5dc1ec` (report).
Round 3: `eebd92f17` (portable seams, size checks, `refuseUnreadable`, the new and differential regressions, the two inherited test files recorded in `tests/upstream-modifications.txt`), and this report.

Round 3 product files: `src/core/DataFile.cpp`, `src/core/DawProjectZipRead.cpp`. Round 3 tests: `tests/src/core/DataFileContainerRecoveryTest.cpp`, `ProjectContainerTest.cpp`, `ProjectContainerEntriesTest.cpp`, `ProjectContainerSaveTest.cpp`, `DawProjectDeflateImportTest.cpp`, `DawProjectInterchangeRoundTripTest.cpp`.

## Self-check

1. **Each new test fails with the product change reverted, then passes restored.** Yes, shown per test in Control A (the 8 failing cases listed above, `negctl-size-final/ctest-family.log`) and Control B (7 of 8 fail, `negctl-full/ctest-family.log`). Restored: GREEN 8/8 (`test-r3-5-final-family.log`).
2. **Acceptance criteria proven by a test that was run.** First read: `unreadablePendingFileIsPreservedNotDeleted`. Second read: `unreadableContainerNewFileIsPreservedNotDeleted`. Repeated failures: `repeatedContainerReadFailuresKeepTheNewFileUntilAReadableAttempt`. Named refusals at each reader: the five differential cases. Malformed and oversized controls: `truncatedContainerNewFileIsDiscardedWithReport`, `aTruncatedSavedContainerIsRefusedAsMalformed`, `oversizedContainerNewFileIsRefusedAndKept`, `aTruncatedDirectoryIsListedAndANonZipIsRefused` (existing), and the existing document-size and compressed-size tests in `DataFileSaveIntegrityTest`.
3. **Undo and reversibility.** Not affected. Recovery runs only in the `DataFile` constructor. No command, undo entry or reversibility class is added or changed.
4. **Audio thread.** Nothing on the audio thread. See the audit above.
5. **Old projects still load.** A valid project, a legacy save and an ordinary v2 save read exactly as before. Only a read that returns fewer bytes than the file holds, or that errors, changes: the canonical v2 load and the DAWproject import now refuse it with `cannot read`, where they previously parsed partial bytes. Those callers see a refusal of an unreadable file, not a new rejection of valid data. The `unreadable` flag and the `refuseUnreadable` helper default to no effect for other callers.
6. **Portability.** The seams are portable and every new case runs everywhere. `QFileDevice::NoError` and `QFile::close()` exist in Qt 5.15 and Qt 6.
7. **Deterministic.** Yes. No timing, sleeps or threads. The fault is set and cleared by the test around one call.
8. **Sibling and caller coverage.** `readWhole` has one caller, `dawProjectZipReadTolerant`, reached from the strict `dawProjectZipRead` (container path) and from `DawProjectRead.cpp:502` (import). `readContainer` is called only from `readDocumentContainer`, which is called from `DataFile.cpp` (recovery at `:338` and the canonical load at `:521`). `isCompleteDocument` has one caller, `recoverPendingWrite` (`DataFile.cpp:375`). The first-read check (`DataFile.cpp:309-312`) and the zip reader's check (`DawProjectZipRead.cpp:183`) are separate, and both are covered.
9. **Verdict.** FIXED. Not ALREADY-FIXED or FALSE-POSITIVE, so no "already fixed" citation is needed. The finding's line is `src/core/DataFile.cpp:~275`; the code it names is now at `:290-345`.

Stated plainly, not covered:
- The `QFile::error()` branch is in production code but no test reaches it. The seam closes the file, and Qt reports no error for a closed-file read, so the size comparison is the path the tests exercise. A real I/O error (EIO) would take the `error()` branch.
- An open refused after DataFile's own open succeeded (`DawProjectZipRead.cpp:174`) takes the same branch as a failed open, but a deterministic test can't arrange a permission change between two opens.

## Requested of the loop

- The negative-control selection should include `DataFileContainerRecoveryTest` (and `DataFileSaveIntegrityTest`). The loop's five files do not reach the changed branch without these; the five now carry differential cases, but the recovery behaviour is tested only in those two targets.

## Known environment issues (not product defects)

- ccache could not create temp files under `zene-bunny-fix/dsh/home/ccache-tmp-r6`, outside the write jail. Builds used `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp/ccache`.
- `feature_check.py` writes its logs next to the worktree (outside the jail). `submit_check.py` calls it directly, so its "gates" row will report the same `PermissionError` until the script writes inside the worktree or the run folder. The gate logic passes (EXIT 0 above).
- The lane build tree refuses `cmake -S . -B build` ("configuring is the parent's job"). The new test targets were picked up by `cmake --build`.
- A shell guard blocked one `rm -f "$N/*.log"`, because the glob was a shell variable. I used literal paths after that.
