FIXED

# cluster new-p18-20 — recovery could replace a newer project with an inappropriate `.new`

## Verdict

REAL. `recoverPendingWrite()` (src/core/DataFile.cpp:433) adopted any *complete,
recognized* `<file>.new` by renaming it over the canonical file. Two properties
the recovery never checked are exactly what distinguishes a genuine interrupted
save from an inappropriate temp:

* **Staleness.** A save writes `<file>.new` *before* the canonical path changes
  (src/core/DataFile.cpp's `writeFile()` stages the temp, backs up, then renames
  at :1070). If the canonical file is a complete recognized document *newer* than
  the temp, that canonical file was written separately after the temp, so the
  temp is stale, not pending. Before this fix the stale temp was adopted and the
  newer project was lost.
* **Type.** A save of this project cannot write a document of another type, but
  `isRecoverableDocument()` only asks whether the temp is *some* recognized
  document (src/core/DataFile.cpp:529). Before this fix a complete document of
  another type at the sidecar path (e.g. a `songtemplate` beside `song.mmp`)
  replaced the project.
* **No backup.** `writeFile()` preserves the previous version in `<file>.bak`
  before publishing the new one; recovery renamed the temp over the canonical
  file with no backup at all, so the one older copy was destroyed.

The earlier recovery hardening (commit `ad4a3d96d8`, "recovery adopted any
parseable `.new` over the project") closed the *unrecognized document* half;
it did not check the canonical file, so the two conflicts above and the missing
backup remained.

## Fix

`src/core/DataFile.cpp`:

* `recoverPendingWrite()` (:433) now, when the canonical file is a complete
  recognized document:
  * refuses adoption and keeps both files when the canonical file is newer than
    the temp (src/core/DataFile.cpp:461);
  * refuses adoption and keeps both files when the temp's document type differs
    from the canonical file's (:469);
  * stages the previous version to `<file>.bak` with the same `replaceBackup()`
    the save path uses (:483), honouring `app/disablebackup`, and refuses
    adoption (keeping both files) if that backup cannot be made (:485).
* `isCompleteDocument()` / `decompressesAsRecoverableDocument()` /
  `parsesAsRecoverableDocument()` now report the recognized document's type
  through an optional out-parameter, normalized through the loader's own
  compatibility names (`channelsettings`, `pattern`) so a legacy spelling is not
  mistaken for a different type. No header changed.
* A genuine interrupted save is unaffected: its temp is newer than the canonical
  file and of the same type, so it is still adopted, and now the previous
  version is additionally kept as `.bak` — the end state a completed save would
  have produced.

## Test matrix

| Path | Test |
| --- | --- |
| normal adoption | `DataFileSaveIntegrityTest::orphanedNewFileIsAdoptedWithReport` (existing, still passes) |
| stale temp / newer canonical | `olderPendingSaveDoesNotReplaceANewerProject` (new) |
| type mismatch | `pendingSaveOfAnotherDocumentTypeIsNotAdopted` (new) |
| backup preserved | `adoptionKeepsThePreviousProjectAsABackup` (new) |
| backup failure | `adoptionIsRefusedWhenTheBackupCannotBeMade` (new; blocked staging name, as the save-path test does) |
| reconfiguration `disablebackup` | `adoptionHonoursTheDisableBackupConfiguration` (new) |
| exhaustion / limits | unchanged: `newFileDeclaringAnEnormousSizeIsRefusedBeforeDecompressing`, `deeplyNestedNewFileIsRefusedBeforeParsing` still pass |
| audio thread | recovery runs only from the file constructor, never from process/render paths; no new allocation, lock, wait or log there |
| platforms | `QFileInfo::lastModified()` and `QFileDevice::setFileTime()` are portable; no new platform branch |

The tests inject modification times with `QFile::setFileTime()` instead of
sleeping or relying on wall-clock ordering.

## Commands and results (EXIT codes copied from the logs)

Logs: `/home/kruzzzzy/.config/kads/zene/state/runs/new-p18-20.r1/logs`.

RED (tests only, product code unmodified):

```
cmake --build build --target DataFileSaveIntegrityTest -j4   -> EXIT 0   (build-1-red.log)
cd build/tests && ctest -R '^DataFileSaveIntegrityTest$' --output-on-failure
                                                            -> EXIT 8   (test-1-red.log)
```

`test-1-red.log`: 22 passed, 4 failed — the four new regression tests each fail
because the temp is adopted (`Recovered interrupted save: ... adopted as ...`).

GREEN (fix applied):

```
cmake --build build --target zene DataFileSaveIntegrityTest -j4 -> EXIT 0 (build-2-green.log)
cd build/tests && ctest -R '^DataFileSaveIntegrityTest$' --output-on-failure
                                                                -> EXIT 0 (test-2-green.log)
./DataFileSaveIntegrityTest                                    -> EXIT 0 (test-2-green-detail.log:
                                                                          26 passed, 0 failed)
```

Siblings (`DataFileFormatTest`, `DataFileContainerRecoveryTest`,
`ProjectContainerTest`, `ProjectContainerEntriesTest`,
`ProjectContainerSaveTest`, `ProjectOpenIntegrityTest`):

```
cmake --build build --target DataFileContainerRecoveryTest DataFileFormatTest \
  ProjectContainerSaveTest ProjectContainerTest ProjectContainerEntriesTest \
  ProjectOpenIntegrityTest -j4                                  -> EXIT 0 (build-3-siblings.log)
cd build/tests && ctest -R '^(DataFileContainerRecoveryTest|DataFileFormatTest|\
DataFileSaveIntegrityTest|ProjectContainerSaveTest|ProjectContainerTest|\
ProjectContainerEntriesTest|ProjectOpenIntegrityTest)$' --output-on-failure
                                                                -> EXIT 0 (test-3-siblings.log:
                                                                           100% tests passed, 7/7)
```

Negative control (procedure 4b), product patch only:

```
git diff -- src/core/DataFile.cpp > .../logs/fix.patch          -> EXIT 0
git apply -R .../logs/fix.patch                                 -> EXIT 0
cmake --build build --target zene DataFileSaveIntegrityTest -j4 -> EXIT 0 (build-4-revert.log)
cd build/tests && ctest -R '^DataFileSaveIntegrityTest$' --output-on-failure
                                                                -> EXIT 8 (test-4-revert.log:
                                                                           4 FAIL!, the four regressions)
git apply .../logs/fix.patch                                    -> EXIT 0
cmake --build build --target zene DataFileSaveIntegrityTest -j4 -> EXIT 0 (build-5-restore.log)
cd build/tests && ctest -R '^DataFileSaveIntegrityTest$' --output-on-failure
                                                                -> EXIT 0 (test-5-restore.log:
                                                                           100% tests passed)
```

After adding the reconfiguration test:

```
cmake --build build --target DataFileSaveIntegrityTest -j4      -> EXIT 0 (build-6-green2.log)
cd build/tests && ctest -R '^DataFileSaveIntegrityTest$' --output-on-failure
                                                                -> EXIT 0 (test-6-green2.log)
./DataFileSaveIntegrityTest                                     -> EXIT 0 (test-6-green2-detail.log:
                                                                           27 passed, 0 failed)
```

Gates:

```
python3 .../qwen/feature_check.py <worktree>                    -> EXIT 0 (gates-1.log)
RESULT: PASS (fork-sources, all-sources-reproduce, no-upstream-regression,
check-namespace, no-tautology, doc-figures, complexity, file-length)
```

## Registration

`src/core/DataFile.cpp` is an inherited file: an entry was added to
`tests/upstream-modifications.txt` in the same commit. The regression test lives
in the already-registered `tests/src/core/DataFileSaveIntegrityTest.cpp` and its
already-registered `...Parts2.h`; no new product file. The header was not
changed.

## Self-check

Answers to the `submit_check.py` ASK items (log `submit-1.log`):

* **The regression test FAILS with the product change reverted and PASSES with
  it.** Product patch only, tests kept:
  `git diff -- src/core/DataFile.cpp > logs/fix.patch` EXIT 0;
  `git apply -R logs/fix.patch` EXIT 0;
  build EXIT 0 (`build-4-revert.log`); `ctest -R '^DataFileSaveIntegrityTest$'`
  EXIT 8, 4 FAIL (`test-4-revert.log`); `git apply logs/fix.patch` EXIT 0;
  build EXIT 0 (`build-5-restore.log`); ctest EXIT 0, 100% passed
  (`test-5-restore.log`). The four failures are exactly the four conflict /
  backup tests.
* **Deterministic.** No sleep, no wall-clock ordering. The tests set fixed
  modification times with `QFile::setFileTime(QDateTime::fromSecsSinceEpoch(1000000 /
  2000000), FileModificationTime)` (`DataFileSaveIntegrityTestParts2.h:279`),
  so the newer-canonical conflict and the newer-temp adoption are
  injected, not observed. The "no backup" failure path blocks the staging name
  with a non-empty directory (`occupy()`, the same fixture the save-path test
  uses) rather than relying on permissions or timing.
* **Cause and complete.** `recoverPendingWrite()` is called from exactly one
  place, the file constructor (`src/core/DataFile.cpp:601`); `isCompleteDocument()`'s
  only caller is `recoverPendingWrite()`; `parsesAsRecoverableDocument()` and
  `decompressesAsRecoverableDocument()` are internal to it. No header changed.
  The new out-parameter is optional and every other caller passes two arguments,
  so their behaviour is byte-for-byte unchanged. The type comparison normalizes
  the loader's own compatibility names (`channelsettings`,
  `pattern`) so a legacy spelling still matches. Backup uses the existing
  `replaceBackup()` and the existing `app/disablebackup` switch, keeping the
  recovery path consistent with `writeFile()`. Siblings pass: 7/7 in
  `test-3-siblings.log`.
* **No collateral damage.** The 22 pre-existing `DataFileSaveIntegrityTest`
  cases still pass, including `orphanedNewFileIsAdoptedWithReport` (a genuine
  newer temp of the same type is still adopted) and the oversized / deep-nesting
  / unreadable-temp limits, which are untouched. `DataFileContainerRecoveryTest`
  (plain and container temps, all adoption outcomes) passes. The backup step is
  skipped when `app/disablebackup` is set, proven by
  `adoptionHonoursTheDisableBackupConfiguration`. A canonical file that is not a
  complete recognized document still does not block adoption of a complete temp
  (the type/mtime checks only run when there is a recognized project to compare
  against), and the backup preserves even such a canonical file as `.bak`.
* **ALREADY-FIXED / FALSE-POSITIVE:** not applicable; verdict is FIXED.
* **Sibling commands.** This change adds no control command and touches no
  command handler or command-registration file. The listed `app.*` and
  `arrangement.*` commands are unaffected: no file under `src/core/Control*` or
  `src/gui` was edited.

`submit_check.py` exited 0 with `RESULT: PASS - submit`; the only remaining
items were the ASK questions above (`submit-1.log`).

