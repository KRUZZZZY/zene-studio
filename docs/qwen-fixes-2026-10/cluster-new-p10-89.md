FIXED

# cluster new-p10-89 — recovery can adopt a live save from another process (round 3)

Verdict: **REAL**, fixed at the cause. Round 1 (`8421bcb81f`) coordinated saves and recovery
through `<file>.save-lock`; round 2 (`565c2907e7`) pinned the lock's staleness to a dead pid
(`setStaleLockTime(0)`, so a live writer's lock is never reclaimed for being old) and made the save
fail closed on every lock error. This round closes what was still open at the **callers**: the lock
was taken inside `DataFile::writeFile`, *after* `project.save` had already rotated the revision set,
and the two revision-restore paths took no lock at all.

## What was still wrong after round 2

- `src/core/ControlCommandsProjectFiles.cpp` (`project.save` handler): `rotateProjectRevision(target…)`
  ran first, then `saveProjectFile`. With another instance's save in flight, this save rotated
  rev0→rev1→rev2 (dropping the oldest of a full keep-3 set) and copied the live file into rev0 from
  under the writer, and only then was refused by `writeFile`'s lock. A refused save still cost history.
- `src/core/ProjectRevisions.cpp` `restoreProjectRevision` and `src/core/RevisionTimeline.cpp`
  `restoreTimelineRevision` (the non-rotation ids, e.g. `backup`): each rotates the set and replaces the
  live file with no coordination, so a restore could run in the middle of another instance's save.

QLockFile is not re-entrant for one process, so the handler could not simply take the lock and let
`writeFile` take it again: the lock has to be handed down.

## The fix (this round)

- **New RAII type** `DataFileSaveLock` (`include/DataFileSaveLock.h`, `src/core/DataFileSaveLock.cpp`),
  exposed as `DataFile::SaveLock` (`include/DataFile.h`). `tryAcquire(name, &refusal)` makes one
  non-blocking attempt with `setStaleLockTime(0)` and returns null plus a user-worded reason on *any*
  failure. `tryAcquireForProject(path, &refusal)` is the same for a project path: it takes the lock for
  the name `writeFile` will write and appends the sidecar to the refusal.
- **Shared static normalisation**: `DataFile::normalisedName(type, fn)` (`src/core/DataFile.cpp`, the
  body that was `nameWithExtension`, now static; `nameWithExtension` calls it), so every contender for a
  project contends for one sidecar. It replaces round 2's file-local `saveLockPath` / `disableLockAge` /
  `saveLockRefusal`.
- **Overloads taking a held lock**: `DataFile::writeFile(fn, withResources, SaveLock* held = nullptr)`
  and `Song::saveProjectFile(filename, withResources, format, DataFileSaveLock* held = nullptr)`
  (`src/core/Song.cpp:2475`, passes it on at `:2513`). A held lock for a *different* file is refused,
  never trusted (`lockForWrite`, `src/core/DataFile.cpp:207`). With none given, `writeFile` takes and
  releases its own, exactly as before, so every other caller of `saveProjectFile`/`writeFile` is
  unchanged.
- **`project.save`** (`ControlCommandsProjectFiles.cpp:281`): acquires the lock **before**
  `rotateProjectRevision` (`:291`), does rotation and write inside it, releases it right after the write
  (`:301`). Contention returns a typed `ControlErrorKind::Refused` ("… was NOT saved: …. No revision was
  rotated and nothing was written or discarded.") with no rotation, no `.new` and no `.bak` change.
- **Both restore paths** take the lock before staging or rotating (`ProjectRevisions.cpp:431`,
  `RevisionTimeline.cpp:446`) and refuse with "… was NOT restored: …. No revision was rotated and the
  project was not replaced." The timeline's rotation-id branch delegates to `restoreProjectRevision`
  before its own lock, so the lock is never taken twice.
- **Recovery** (`DataFile.cpp:492`) and the writer's own path (`:1064`) use the same type.
- Registration: the two new files are in `tests/fork-sources.txt` and `tests/all-sources.txt`; the
  .cpp is in `src/core/CMakeLists.txt`; `include/DataFile.h`, `include/Song.h`, `src/core/DataFile.cpp`
  and `src/core/Song.cpp` were already listed in `tests/upstream-modifications.txt`.

Siblings checked: other `saveProjectFile` callers (autosave, render, bounce, collab, mastering,
`ControlCommandsAlternatives/Handoff/Documents/Project*`, `ScriptBindings`, `MainWindow` recovery save)
pass no lock and keep `writeFile`'s own, so they are consistent and unchanged. The restore undo/redo
lambdas call `restoreProjectRevision` outside any held lock. `rotateProjectRevision`'s other callers are
the two restore functions, both now inside the lock.

## Tests (test matrix)

| path | test | what it pins |
| --- | --- | --- |
| lock type | `DataFileSaveIntegrityTest::aHeldSaveLockIsExclusiveAndHandedToWriteFile` | exclusive; refusal text; `normalisedName` adds `.mmpz`; `writeFile` with a held lock for another file refuses and writes nothing; with the right one it saves and leaves the lock held; release removes the sidecar |
| restore, both paths | `DataFileSaveIntegrityTest::restoreIsRefusedBeforeRotatingWhileASaveHoldsTheLock` | with the lock held `restoreProjectRevision` and `restoreTimelineRevision("backup")` refuse; project, rev0, rev1, no rev2, `.bak` and the directory listing are unchanged; with the lock gone both restore and rotate as before |
| handler | `ProjectContainerSaveTest::aSaveRefusedByAHeldLockRotatesNothing` | `project.save` is a `Refused` result with the lock held, rev0/`.bak`/live bytes unchanged, no rev1, no `.new`; with the lock gone it lands, `revision_kept` is true, rev1 exists, no lock is left |
| exhaustion / reconfiguration | round-2 tests (aged live lock, obstructed sidecar, permission) and `adoptionHonoursTheDisableBackupConfiguration`, unchanged and still passing | |
| audio thread | none of this runs there (open/save/control thread only) | — |
| platforms | `QLockFile` is Qt Core on Linux/macOS/Windows; `setStaleLockTime(int)` exists in Qt 5.15 and 6; no platform code added | built here (Linux) |

Deterministic: no sleeps, threads or wall-clock; the "other instance" is a real `DataFileSaveLock` held
in the test process (QLockFile refuses a live pid).

## Commands and EXIT codes (logs in the r3 folder)

RED: the three wiring files reverted (`git diff -- src/core/ControlCommandsProjectFiles.cpp
src/core/ProjectRevisions.cpp src/core/RevisionTimeline.cpp > logs/fix.patch; git apply -R
logs/fix.patch`, both EXIT=0). The lock API stays, so the tests compile and fail on assertions:
```
cmake --build build --target zene DataFileSaveIntegrityTest ProjectContainerSaveTest -j4 > logs/build-red-2.log 2>&1   EXIT=0
cd build/tests && ctest -R '^(DataFileSaveIntegrityTest|ProjectContainerSaveTest)$' --output-on-failure > logs/test-red-2.log 2>&1   EXIT=8
  FAIL! DataFileSaveIntegrityTest::restoreIsRefusedBeforeRotatingWhileASaveHoldsTheLock ... (restore ran during a save)   Parts3.h(293)
  FAIL! ProjectContainerSaveTest::aSaveRefusedByAHeldLockRotatesNothing ... 'refused.errorMessage.contains("NOT saved")' returned FALSE.
        (the engine refused to write .../locked.mmp)   Parts1.h(444)
```
(an earlier identical RED, `build-red-1.log` / `test-red-1.log`, EXIT=0 / EXIT=8, same two failures,
was taken before the helper `tryAcquireForProject` was factored out.) The handler failure is the bug:
without the lock-before-rotation the refusal came late from `writeFile`, with the engine's generic text,
after the rotation had run.

GREEN: `git apply logs/fix.patch` (EXIT=0), then
```
cmake --build build --target zene DataFileSaveIntegrityTest ProjectContainerSaveTest RevisionTimelineTest ProjectRevisionRestoreStagingTest ReversibilityUndoTest ControlSuccessPathsTest DataFileFormatTest DataFileContainerRecoveryTest ProjectOpenIntegrityTest SessionModelTest -j4 > logs/build-green-4.log 2>&1   EXIT=0
cd build/tests && ctest -R '^(DataFileSaveIntegrityTest|ProjectContainerSaveTest|RevisionTimelineTest|ProjectRevisionRestoreStagingTest|ReversibilityUndoTest|ControlSuccessPathsTest|DataFileFormatTest|DataFileContainerRecoveryTest|ProjectOpenIntegrityTest|SessionModelTest)$' --output-on-failure > logs/test-green-4.log 2>&1   EXIT=8
  9 of 10 pass; the one failure is ControlSuccessPathsTest (see Other findings)
cd build/tests && ctest -R '^(DataFileSaveIntegrityTest|ProjectContainerSaveTest)$' --output-on-failure > logs/test-green-5.log 2>&1   EXIT=0
```
Final code (after a formatting-only edit of the handler lines to keep the file under the length gate):
```
cmake --build build --target zene DataFileSaveIntegrityTest ProjectContainerSaveTest RevisionTimelineTest ProjectRevisionRestoreStagingTest ReversibilityUndoTest DataFileFormatTest DataFileContainerRecoveryTest ProjectOpenIntegrityTest SessionModelTest -j4 > logs/build-green-6.log 2>&1   EXIT=0
cd build/tests && ctest -R '^(DataFileSaveIntegrityTest|ProjectContainerSaveTest|RevisionTimelineTest|ProjectRevisionRestoreStagingTest|ReversibilityUndoTest|DataFileFormatTest|DataFileContainerRecoveryTest|ProjectOpenIntegrityTest|SessionModelTest)$' --output-on-failure > logs/test-green-6.log 2>&1   EXIT=0   (9/9)
cd build/tests && ./DataFileSaveIntegrityTest > logs/test-green-verbose-1.log 2>&1   EXIT=0   (32 passed, 0 failed; both new slots PASS)
cd build/tests && ./ProjectContainerSaveTest aSaveRefusedByAHeldLockRotatesNothing > logs/test-green-verbose-2.log 2>&1   EXIT=0   (PASS)
```
Gates, `python3 …/feature_check.py <worktree>`:
```
gates-1.log EXIT=1  file-length: ControlCommandsProjectFiles.cpp 507 (new code lines) + fork/all-sources (new files untracked)
gates-2.log EXIT=1  file-length: ControlCommandsProjectFiles.cpp 501   -> helper tryAcquireForProject + one-line braces
gates-4.log EXIT=0  PASS fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace,
                    no-tautology, doc-figures, complexity, file-length      RESULT: PASS
```
(`gates-3.log` is a crash of my own invocation from the wrong directory, not a gate result.)

Not run: the full 4b revert of *every* product file (headers included) would rebuild the whole tree
twice; the revert above removes exactly the behaviour added this round and is the proof that the tests
catch it. TSan does not run on this box; the concurrency argument is the design (one non-blocking
lock attempt, no wait, holder pid checked, no age rule) and the tests above.

## Self-check

- **Test fails without the fix, passes with it**: `test-red-2.log` EXIT=8 with the two assertion
  failures above; `test-green-5.log` and `test-green-6.log` EXIT=0.
- **Fix at the cause and complete**: the lock is taken before every operation that changes the project or
  its revision set (`project.save` before rotation, both restores before staging, recovery before any
  temp, `writeFile` for its own temp); callers and siblings listed above.
- **No collateral**: with no contention every path behaves as before (same rotation, same write,
  same replies). `writeFile`/`saveProjectFile` gained a defaulted trailing parameter, so no caller
  changes. Save/restore/revision/format families pass (9/9). The one refusal that is new is the one the
  finding asks for.
- **Realtime / undo / commands**: nothing runs on the audio thread; no allocation, lock or wait was added
  to a process/render path. No control command added (`ControlCommandsSnapshot` unmoved); `project.save`
  gains a refusal result, one honest save, no extra undo step.
- **Sibling commands**: `project.restore_revision` and the undo/redo of `project.save` reach
  `restoreProjectRevision`, which now takes the lock; the timeline restore likewise. No other command
  rotates the set.
- **Verdict**: FIXED (not ALREADY-FIXED / FALSE-POSITIVE), the defect was reproduced RED.
- **Unverified**: behaviour on Windows/macOS (not built here); the full-header revert described above.

Delegation: Haiku ran the first green build and test run (found a missing `control::` qualifier in my
test), the first RED run and the wider family run, and fetched failure lines; it used up its five
starts, so the later RED/GREEN rebuilds, the gates and the report were done directly.

## Other findings

- `ControlSuccessPathsTest` (`controllerFeedbackTakeoverAndTemplates`, `failedBuiltInDocumentKeepsCurrentSession`,
  `failedBuiltInDocumentSaveKeepsCurrentSession`) fails here because `/home/kruzzzzy/Documents` is a
  read-only file system in this sandbox (`touch: Read-only file system`), so the tests' `mkpath` under
  `~/Documents/Zene Studio` and the cache location fail; they do not reach project saving. Not compared
  against an unmodified build.
- `project.save` rotates `target` literally, while `writeFile` writes `nameWithExtension(target)`; for a
  bare name with no extension the revision set and the written file differ. Pre-existing, left alone; the
  lock uses the written name.
- `MainCliStartupTest::failedControlSocketStartupClearsBothSessionMarkers` fails on this box with
  `QLocalServer::listen: Unknown error 25` (recorded in round 2; environmental).
