FIXED

# Cluster new-p11-72 — Recovery can delete an unreadable pending save

## Verdict

FIXED. The finding is real on the branch base (`639e1d23e`). A `<file>.new` that could not be opened, or whose read failed, was reported as an incomplete document, and `recoverPendingWrite()` then deleted it. The fix keeps such a file in place, reports it, and adopts it on a later open once it can be read.

Round 2 (Luna REWORK) closed the one remaining path: a v2 container `.new` whose second read (the container reader's) failed while the file still opened was also deleted. The zip reader now reports read failures, and the recovery keeps those containers as well. Malformed and truncated containers are still discarded.

## Evidence (file:line on the committed tree, `ff8df3eda`)

Plain pending save (round 1):
- `src/core/DataFile.cpp:282-290` (`isCompleteDocument`): an open failure sets `*reason = "unreadable"` (:288). Before the fix it was `return false` with no reason, and the caller discarded the file.
- `src/core/DataFile.cpp:299-304`: a read error (`f.error() != QFileDevice::NoError` after `readAll()`, :300) also sets `"unreadable"`. An empty file with no error still returns `false` and is discarded as before.
- `src/core/DataFile.cpp:383-389` (`recoverPendingWrite`): the `else if( reason == "unreadable" )` branch prints `Could not read <pending> ... it was not checked, so it is left in place` and does not remove the file. The truncation branch (`Discarded incomplete`, :390) is unchanged.

Container pending save (round 2):
- `src/core/DawProjectZipRead.cpp:171-194` (`readWhole`): an open failure sets `*unreadable = true` (:176). After `readAll()`, a QFile read error sets it too (:185-190). A short or malformed file leaves it false.
- `src/core/DawProjectZipRead.cpp:557-562` (`dawProjectZipReadTolerant`), `:576-` (`dawProjectZipRead`, the strict reader the container uses): both pass the flag to `readWhole`. The DAWproject importer (`src/core/DawProjectRead.cpp:502`) calls the tolerant reader without it and is unchanged.
- `src/core/ProjectContainer.cpp:310,320` (`readContainer`) and `src/core/ProjectContainerEntries.cpp:620,623` (`readDocumentContainer`): pass the flag down. `include/ProjectContainer.h:156,333` and `include/DawProjectInterchange.h:571,586` declare it with `nullptr` defaults, so every other caller is unchanged.
- `src/core/DataFile.cpp:320-331` (container branch of `isCompleteDocument`): `readDocumentContainer(..., &unreadable)`; `if( unreadable ) *reason = "unreadable"`. The reopen-the-path check from round 1 is removed, because it could not tell a failed read from malformed content.
- `src/core/DawProjectZipRead.cpp:153-164` (`armTestReadFault`): the test seam. When `ZENE_TEST_CONTAINER_READ_FAULT` is set (Unix only), the descriptor is `dup2`'d onto a write-only `/dev/null`, so the next `read()` fails with EBADF while the open succeeded. It is a no-op without the variable and compiled out elsewhere.

Regressions:
- `tests/src/core/DataFileContainerRecoveryTest.cpp:134` (`unreadableContainerNewFileIsPreservedNotDeleted`): a valid container `.new` whose second read fails. Asserts no `Discarded incomplete`, a `Could not read` line, the `.new` still present, the canonical project unchanged, and then that the preserved container is adopted once the seam is off (`s5probe == "pending"`). It QSKIPs on non-Unix, where the seam does not exist.
- `tests/src/core/DataFileContainerRecoveryTest.cpp:185` (`truncatedContainerNewFileIsDiscardedWithReport`): a container cut in half is still discarded with `Discarded incomplete`. This keeps malformed content on the old path.
- `tests/src/core/DataFileSaveIntegrityTest.cpp:685` (`unreadableNewFileIsPreservedNotDeleted`, round 1): the plain-file permission case, unchanged.

No wall-clock timing, sleeps or races are used. The seam is driven by an environment variable, and the failing read is a real kernel error.

## Commands and exit codes

Log folder (round 2): `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-72.r2/logs`. Round 1 logs are in `new-p11-72.r1/logs`.

Commits: `de9ee899f` (seam and RED regression), `ff8df3eda` (fix and registration).

RED on the round-1 fix (before the round-2 fix), with the seam and regression in place:

| Step | Command | EXIT | Log |
|---|---|---|---|
| Build zene | `cmake --build build --target zene -j4` | 0 | build-r2-1-red.log |
| Build test | `cmake --build build --target DataFileSaveIntegrityTest -j4` | 0 | build-r2-2-red.log |
| Fixture first try | `./DataFileSaveIntegrityTest` | 2 | test-r2-1-red-direct.log (writer refused: no `<z:index>`; fixture error, fixed) |
| Fixture second try | same, after the trackcontainer child | 2 | test-r2-2-red-direct.log (same cause; fixed) |
| **RED** | `./DataFileSaveIntegrityTest` | **1** | test-r2-3-red-direct.log: `unreadableContainerNewFileIsPreservedNotDeleted` FAILS on `!report.contains("Discarded incomplete")`; 18 passed, 1 failed |

GREEN with the round-2 fix:

| Step | Command | EXIT | Log |
|---|---|---|---|
| Build zene | `cmake --build build --target zene -j4` | 0 | build-r2-5-green.log |
| Build the 7 targets | DataFileSaveIntegrityTest, DataFileFormatTest, ProjectContainerTest, ProjectContainerEntriesTest, ProjectContainerSaveTest, DawProjectInterchangeRoundTripTest, DawProjectDeflateImportTest | 0 each | build-r2-6-*.log |
| Focused, direct | `./DataFileSaveIntegrityTest` | 0 | test-r2-4-green-direct.log: 19 passed (before the test moved) |
| Family, 7 targets | `ctest -R '^(DataFileFormatTest\|DataFileSaveIntegrityTest\|ProjectContainerTest\|ProjectContainerEntriesTest\|ProjectContainerSaveTest\|DawProjectInterchangeRoundTripTest\|DawProjectDeflateImportTest)$'` | 0 | test-r2-5-family-green.log: 7/7 |

After the test moved to its own file (the save-integrity file exceeded the file-length ratchet; see Gates):

| Step | Command | EXIT | Log |
|---|---|---|---|
| Build new target | `cmake --build build --target DataFileContainerRecoveryTest -j4` | 0 | build-r2-13-newfile.log |
| GREEN, ctest | `ctest -R '^DataFileContainerRecoveryTest$' --output-on-failure` | 0 | test-r2-9-newfile-green.log |
| GREEN, direct | `./DataFileContainerRecoveryTest` | 0 | test-r2-10-newfile-direct.log: 4 passed (2 tests, plus init/cleanup) |

### Step 4b: the new regression fails with the product fix reverted

Product diff of the fix (`src include`, against the RED commit): `git diff -- src include > fix-r2b.patch` (EXIT 0; identical to the earlier `fix-r2.patch`).

| Step | Command | EXIT | Log |
|---|---|---|---|
| Revert | `git apply -R fix-r2b.patch` | 0 | — |
| Rebuild zene | `cmake --build build --target zene -j4` | 0 | build-r2-14-revert.log |
| Rebuild test | `cmake --build build --target DataFileContainerRecoveryTest -j4` | 0 | build-r2-15-revert.log |
| **Run, reverted** | `ctest -R '^DataFileContainerRecoveryTest$' --output-on-failure` | **8** | test-r2-11-revert-red.log: `FAIL! unreadableContainerNewFileIsPreservedNotDeleted() '!report.contains("Discarded incomplete")' returned FALSE` |
| Re-apply | `git apply fix-r2b.patch` | 0 | — |
| Rebuild zene, test | as above | 0; 0 | build-r2-16-reapply.log, build-r2-17-reapply.log |
| **Run, restored** | ctest as above | **0** | test-r2-12-reapply-green.log: 100% |

The same cycle was run earlier against the first test location (`DataFileSaveIntegrityTest`): reverted EXIT 8 (test-r2-6-revert-red.log), restored EXIT 0 (test-r2-7-reapply-green.log, 19 passed).

Affected-family count: `ctest -R '^(DataFileFormatTest|DataFileSaveIntegrityTest|DataFileContainerRecoveryTest|ProjectContainerTest|ProjectContainerEntriesTest|ProjectContainerSaveTest|DawProjectInterchangeRoundTripTest|DawProjectDeflateImportTest)$'`: EXIT 0, **8/8** (test-r2-13-family.log). The two DAWproject tests cover the shared zip reader used by import.

`AudioFileProcessorView` (named in the brief) is still not a test target in this build (round 1, build-5-AudioFileProcessorView.log), so it is not counted.

### Gates

`feature_check.py` writes its logs next to the worktree (outside the write jail), so it cannot run as given. I ran an otherwise identical copy with three lines changed: `ROOT` points at the bunny-fix folder (reads `FIX-BASE` and the cached base baselines, `639e1d23e`), and the log folder points into the run's `logs/feature-check/`.

- Before the commit, `fork-sources` and `all-sources-reproduce` failed (gates-r2-2.log, EXIT 1). Both manifests are derived from git state, and the new test file was untracked. After `git add`, `all-sources-reproduce` reproduced exactly (its header's verify diff, empty output). `fork-sources` reads `HEAD`, so the commit was needed.
- Also in gates-r2-2.log: `file-length` FAILED with `NEW: new file over 500 lines: tests/src/core/DataFileSaveIntegrityTest.cpp (579)`. That file's code lines were over the limit once this round's container tests were added to it. The tests moved to `DataFileContainerRecoveryTest.cpp`, and the save-integrity file is back to its round-1 content. I did not re-anchor or change any gate.
- After the commit (`ff8df3eda`), `python3 -I <shim>/feature_check_wt.py <worktree>` → EXIT 0 (gates-r2-3.log): fork-sources PASS, all-sources-reproduce PASS, no-upstream-regression PASS, check-namespace PASS, no-tautology PASS, doc-figures PASS, complexity "no new item in your files" PASS, file-length "no new item in your files" PASS, `RESULT: PASS`.

## Files committed

Round 1 (`ee4257cee`, `0370312a9`): `src/core/DataFile.cpp`, `tests/src/core/DataFileSaveIntegrityTest.cpp`, `tests/upstream-modifications.txt`, this report.

Round 2 (`de9ee899f`): `src/core/DawProjectZipRead.cpp` (seam), `tests/src/core/DataFileSaveIntegrityTest.cpp` (the RED regression, later moved out).

Round 2 (`ff8df3eda`): `src/core/DataFile.cpp`, `src/core/DawProjectZipRead.cpp`, `src/core/ProjectContainer.cpp`, `src/core/ProjectContainerEntries.cpp`, `include/DawProjectInterchange.h`, `include/ProjectContainer.h`, `tests/src/core/DataFileContainerRecoveryTest.cpp` (new), `tests/src/core/DataFileSaveIntegrityTest.cpp` (back to round-1 content), `tests/CMakeLists.txt`, `tests/all-sources.txt`, `tests/fork-sources.txt`, `tests/upstream-modifications.txt`.

Round 2 report commit: this file.

## Self-check

1. **Each new test fails with the product change reverted, then passes restored.** Yes, for both sets. The plain-file regression: EXIT 8 reverted, 0 restored (round 1, test-5/test-6). The container regression: EXIT 8 reverted (test-r2-11-revert-red.log), 0 restored (test-r2-12-reapply-green.log).
2. **Acceptance criteria proven by a test you ran.** The finding: distinguish I/O failure from invalid content, and preserve a pending save that could not be checked. Plain file: `unreadableNewFileIsPreservedNotDeleted`. Container: `unreadableContainerNewFileIsPreservedNotDeleted`, including adoption once readable. Malformed and truncated content: `truncatedContainerNewFileIsDiscardedWithReport`, `truncatedNewFileIsDiscardedWithReport`, `unrelatedXmlNewFileIsNotAdopted`, `deeplyNestedNewFileIsRefusedBeforeParsing`, `newFileDeclaringAnEnormousSizeIsRefusedBeforeDecompressing`, and `orphanedNewFileIsAdoptedWithReport`, all passing in the family run.
3. **Undo and reversibility.** Not affected. The change is confined to recovering a `.new` in the `DataFile` constructor. No command, undo entry or reversibility class is added or changed.
4. **Audio thread.** Nothing on the audio thread. `recoverPendingWrite()` runs only in the `DataFile(fileName)` constructor on project open. The zip reader runs on project open and DAWproject import. The new code adds a flag and a `qWarning()` on a failure path, which is the same kind of logging the existing branches use.
5. **Old projects still load, and the reader's behaviour outside the bug is unchanged.** Only a read that fails changes behaviour, and the failure now reports as an error instead of parsing partial bytes. Under that change: the canonical v2 load (`src/core/DataFile.cpp`, `readDocumentContainer` with no flag) refuses an unreadable container with its existing warning; the DAWproject importer (`DawProjectRead.cpp:502`) refuses an unreadable file instead of decoding partial bytes. Both are refusals of a file that could not be read, not new rejections of valid data. Valid containers, malformed containers and the DAWproject round-trip and deflate tests all pass (family 8/8). The `unreadable` out-parameter defaults to `nullptr`, so existing callers are unchanged.
6. **Portability.** The seam uses `dup2`, `open`, `fcntl.h` and `unistd.h` only under `Q_OS_UNIX`. Elsewhere it is compiled out, and the container regression QSKIPs. `QFileDevice::NoError` is valid in Qt 5.15 and Qt 6. `QFile::setPermissions` is used by the plain-file regression, which QSKIPs when the process can still read a 0-permission file.
7. **Deterministic.** Yes. The container regression does no timing, sleeping or threading. The seam is set and cleared by the test around one load, and the failing read is a real kernel `EBADF`, not a simulated flag. The fixture is a fixed literal document.
8. **Sibling and caller coverage.** The only caller of `readWhole` is `dawProjectZipReadTolerant`, which is reached from the strict `dawProjectZipRead` (the container path) and from `DawProjectRead.cpp:502` (import). The only caller of `readContainer` is `readDocumentContainer`, which is reached from `DataFile.cpp` (recovery at :320 and the canonical load at :509). `isCompleteDocument` has one caller, `recoverPendingWrite` (`DataFile.cpp:363`). The first-read check (`DataFile.cpp:299-304`) and the container-reader check (`DawProjectZipRead.cpp:185-190`) are separate; each is covered.
9. **Verdict.** FIXED, not ALREADY-FIXED or FALSE-POSITIVE, so no "already fixed" citation is needed. The finding's line is `src/core/DataFile.cpp:~275`; the code it names is now at `:282-331`, cited above.

Not covered by a regression test, stated plainly: an open refused by the zip reader after DataFile's own open succeeded (`DawProjectZipRead.cpp:176`). It takes the same flag as the read path and the same branch, but reaching it needs the file to change permission between two opens, which a deterministic test cannot arrange.

## Known environment issues (not product defects)

- ccache could not create temp files under `zene-bunny-fix/dsh/home/ccache-tmp-r6`, outside the write jail. Builds used `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp/ccache`.
- `feature_check.py` writes its logs next to the worktree (outside the jail). `submit_check.py` calls it directly, so its "gates" row will report the same `PermissionError` until the script writes inside the worktree or the run folder. The gate logic passes (EXIT 0 above).
- The lane build tree refuses `cmake -S . -B build` ("configuring is the parent's job"). The new test target was picked up by `cmake --build build --target DataFileContainerRecoveryTest`, which regenerated it.
