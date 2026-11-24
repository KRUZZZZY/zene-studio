FIXED

# cluster new-p10-89-port - recovery can adopt a live save from another process

Verdict: **REAL**, fixed at the cause. This is the train-52 port of the fix already proven on
`bugs/qwen-new-p10-89` (`8421bcb81f`, `565c2907e7`, `f5cc9079e9`, `ffee553411`). The affected
product files at this branch's merge base were identical to that branch's base, so the final
patch is byte-identical for every file except `include/Song.h` / `src/core/Song.cpp`, where this
branch's unrelated new-p12-248 automation edits were preserved.

## What was wrong

A save writes `<name>.new` and only then replaces `<name>`. During that window a **complete**
temp belongs to a save that is still running in another instance, not to an interrupted one.
`DataFile::recoverPendingWrite()` used to inspect any `<name>.new` with no coordination, so a
second instance opening the project adopted the live temp: the writer's later publish failed
with its temp gone, and when recovery ran first it copied the recovered content over the older
`<name>.bak`. The writer itself also wrote its temp with no way for another instance to know a
save was in flight.

The RED run shows both halves before the fix (`logs/test-2-red.log`, exit 8):

```
FAIL!  : liveSaveInAnotherInstanceIsNotAdopted() 'report.contains("may still be in flight")' returned FALSE.
          captured: [Recovered interrupted save: ".../song.mmp.new" adopted as ".../song.mmp"]
FAIL!  : saveIsRefusedWhileAnotherInstanceHoldsTheLock() '!dataFile.writeFile(target)' returned FALSE.
          (a save that another instance is running was reported as a success)
FAIL!  : unusableSaveLockFailsClosed() '!writer.writeFile(target)' returned FALSE.
          (a save wrote its temp although it could not take the save lock)
```

## Fix (file:line, this tree)

- `include/DataFileSaveLock.h:63-98`, `src/core/DataFileSaveLock.cpp:39-94` - one RAII
  `DataFileSaveLock` over `<name>.save-lock`. `tryAcquire()` is a single non-blocking
  `QLockFile::tryLock(0)`; `setStaleLockTime(0)` makes staleness mean "the holder's pid is dead",
  never "the lock is old", so a save that runs for minutes keeps its lock. `refusalText()` names
  another live instance, a folder that refuses the lock file, or any other acquisition failure.
- `src/core/DataFile.cpp:196-225` - `lockForWrite()` uses a caller-held lock only for the file
  it names, otherwise takes its own, and **fails closed on every failed acquisition** (never
  writes a temp with no lock in hand).
- `src/core/DataFile.cpp:481-499` - `recoverPendingWrite()` asks for the same lock before it
  reads, moves or discards any temp. While a live writer holds it, the temp and canonical file
  are left alone and the open reports `may still be in flight`. A dead writer's lock is stale by
  pid, so the same temp is still adopted when the holder is really gone.
- `src/core/DataFile.cpp:1057-1069` - `writeFile()` holds the lock for exactly the window in
  which `<name>.new` exists and is published.
- `src/core/DataFile.cpp:797-791` - `DataFile::normalisedName()` is shared with
  `nameWithExtension()`, so a lock taken for a project path names the file a save will write.
- `include/DataFile.h:96-108`, `include/Song.h:490-493`, `src/core/Song.cpp:2480-2519` - a
  caller that already holds the lock hands it down through `writeFile()`/`saveProjectFile()`;
  `QLockFile` is not re-entrant for one process, so this avoids a second acquisition. With no
  held lock every existing caller behaves exactly as before.
- `src/core/ControlCommandsProjectFiles.cpp:274-302` - `project.save` takes the lock **before**
  `rotateProjectRevision()`, rotates and writes inside it, and releases it after the write.
  Contention is a typed `ControlErrorKind::Refused` result with no rotation, no `.new` and no
  `.bak` change.
- `src/core/ProjectRevisions.cpp:427-438` and `src/core/RevisionTimeline.cpp:441-453` - both
  restore paths take the lock before staging or rotating and refuse with a `NOT restored`
  message. The timeline's rotation-id branch delegates to `restoreProjectRevision()` before its
  own lock, so the lock is never taken twice.

## Regression test matrix

| path | test | what it pins |
| --- | --- | --- |
| normal adoption | `DataFileSaveIntegrityTest::liveSaveInAnotherInstanceIsNotAdopted` (second half) | once the holder is gone, the very same complete temp is adopted, becomes the project, is consumed, and leaves the previous project in `.bak` |
| failure: live save | `liveSaveInAnotherInstanceIsNotAdopted` | a complete temp and the project are untouched while a live (and deliberately aged) `QLockFile` holder exists; the open says `may still be in flight`; the holder keeps its lock |
| failure: writer | `saveIsRefusedWhileAnotherInstanceHoldsTheLock` | a save whose lock is held elsewhere returns false, reports `was NOT saved`, leaves project, `.bak`, temp and the aged holder's lock untouched; after release the save lands and leaves no lock |
| exhaustion/fail-closed | `unusableSaveLockFailsClosed` | an occupied sidecar name and (on UNIX) a folder that refuses the lock file both refuse the save and leave the recovery temp in place |
| lock type / handed lock | `aHeldSaveLockIsExclusiveAndHandedToWriteFile` | exclusivity, refusal text, `normalisedName()`, a held lock for another file is refused, the right held lock is used and stays held, release removes the sidecar |
| restore paths | `restoreIsRefusedBeforeRotatingWhileASaveHoldsTheLock` | both `restoreProjectRevision()` and `restoreTimelineRevision("backup")` refuse with nothing moved; both rotate normally once the lock is gone |
| command handler | `ProjectContainerSaveTest::aSaveRefusedByAHeldLockRotatesNothing` | `project.save` returns `Refused`, with live/rev0/rev1/`.bak` bytes unchanged, no rev2 and no `.new`; after release it lands with `revision_kept` true and no lock left |
| audio thread | none of these functions runs on the audio thread; they run on the opening/saving/control thread | no new allocation, lock, wait or log in a process/render path |
| platforms | `QLockFile` is Qt Core on Linux/macOS/Windows; `setStaleLockTime(int)` exists in Qt 5.15 and 6; the permission half is `Q_OS_UNIX`-guarded | built and run here on Linux x86_64, Qt 6 |

Deterministic: no sleeps, threads or wall-clock ordering. The "other instance" is a real
`QLockFile` held in the test process (QLockFile refuses a live pid), the sidecar is aged to
epoch second `1000000`, and file times are injected with `setModifiedTime()`.

## RED / GREEN and reverse-patch proof

All logs are in
`/home/kruzzzzy/.config/kads/zene/state/runs/new-p10-89-port.r4/logs/`.

| Step | Exact command | Exit |
| --- | --- | --- |
| baseline build | `cmake --build build --target DataFileSaveIntegrityTest -j4` (`build-1-baseline.log`) | 0 |
| baseline run | `ctest -R '^DataFileSaveIntegrityTest$' --output-on-failure` in `build/tests` (`test-1-baseline.log`) | 0 |
| RED build (tests only, unmodified product) | `cmake --build build --target DataFileSaveIntegrityTest -j4` (`build-2-red.log`) | 0 |
| RED run | `ctest -R '^DataFileSaveIntegrityTest$' --output-on-failure` in `build/tests` (`test-2-red.log`) | **8** (3 failed, quoted above) |
| GREEN build | `cmake --build build --target DataFileSaveIntegrityTest ProjectContainerSaveTest -j4` (`build-3-green.log`) | 0 |
| GREEN run | `ctest -R '^(DataFileSaveIntegrityTest|ProjectContainerSaveTest)$' --output-on-failure` in `build/tests` (`test-3-green.log`) | 0 |
| GREEN verbose | `./DataFileSaveIntegrityTest` in `build/tests` (`test-4-green-verbose.log`) | 0 (32 passed, 0 failed) |
| handler verbose | `./ProjectContainerSaveTest aSaveRefusedByAHeldLockRotatesNothing` in `build/tests` (`test-5-green-verbose.log`) | 0 |
| family build | `cmake --build build --target zene DataFileSaveIntegrityTest ProjectContainerSaveTest RevisionTimelineTest ProjectRevisionRestoreStagingTest ReversibilityUndoTest DataFileFormatTest DataFileContainerRecoveryTest ProjectOpenIntegrityTest SessionModelTest -j4` (`build-4-family.log`) | 0 |
| family run | the matching `ctest -R '^(...)$'` in `build/tests` (`test-6-family.log`) | 0 (9/9) |
| capture product diff | `git diff -- include/DataFile.h include/DataFileSaveLock.h include/Song.h src/core/CMakeLists.txt src/core/ControlCommandsProjectFiles.cpp src/core/DataFile.cpp src/core/DataFileSaveLock.cpp src/core/ProjectRevisions.cpp src/core/RevisionTimeline.cpp src/core/Song.cpp > .../logs/fix.patch` | 0 |
| reverse product diff | `git apply -R .../logs/fix.patch` (`apply-reverse.log`) | 0 |
| rebuild without fix | `cmake --build build --target DataFileSaveIntegrityTest ProjectContainerSaveTest -j4` (`build-5-red-reverse.log`) | **2** (`DataFileSaveLock.h: No such file or directory`, because the full reverse removes the new API the test includes) |
| run without fix | `ctest -R '^(DataFileSaveIntegrityTest|ProjectContainerSaveTest)$' --output-on-failure` in `build/tests` (`test-7-red-reverse.log`) | **8** (both "Not Run") |
| restore product diff | `git apply .../logs/fix.patch` (`apply-forward.log`) | 0 |
| rebuild with fix | `cmake --build build --target DataFileSaveIntegrityTest ProjectContainerSaveTest -j4` (`build-6-green-restored.log`) | 0 |
| rerun with fix | `ctest -R '^(DataFileSaveIntegrityTest|ProjectContainerSaveTest)$' --output-on-failure` in `build/tests` (`test-8-green-restored.log`) | 0 (2/2) |
| final build | `cmake --build build --target zene DataFileSaveIntegrityTest ProjectContainerSaveTest RevisionTimelineTest ProjectRevisionRestoreStagingTest ReversibilityUndoTest DataFileFormatTest DataFileContainerRecoveryTest ProjectOpenIntegrityTest SessionModelTest TrackFolderTest AafImportTest ArrangingCatchUpTest -j4` (`build-11-final.log`) | 0 |
| final regression | `ctest -R '^(DataFileSaveIntegrityTest|ProjectContainerSaveTest)$' --output-on-failure` in `build/tests` (`test-12-final-regression.log`) | 0 (2/2) |
| final family | `ctest -R '^(RevisionTimelineTest|ProjectRevisionRestoreStagingTest|ReversibilityUndoTest|DataFileFormatTest|DataFileContainerRecoveryTest|ProjectOpenIntegrityTest|SessionModelTest|TrackFolderTest|AafImportTest)$' --output-on-failure` in `build/tests` (`test-13-final-family.log`) | 0 (9/9) |

The literal reverse above removes the new header, so its rebuild fails to compile before a test
can run; that is recorded as a non-zero red (build 2, ctest 8). The stronger behavioural RED is
`test-2-red.log`: the tests were built and run against the **unmodified** product code and the
three assertions failed exactly on the defect. After restoring the fix, the same tests pass.

## Gates

`python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>`
(`gates-3-final.log`, unpiped exit **0**):

```
PASS  fork-sources            exit 0
PASS  all-sources-reproduce   exit 0
PASS  no-upstream-regression  exit 0
PASS  check-namespace         exit 0
PASS  no-tautology            exit 0
PASS  doc-figures             exit 0
PASS  complexity              no new item in your files
PASS  file-length             no new item in your files
RESULT: PASS
```

An earlier gate run (`gates-1.log`, exit 1) failed `fork-sources` / `all-sources-reproduce`
only because the new test header was still untracked and the manifest recipe therefore did not
derive it. After `git add -N` the same gate passed (`gates-2.log`, exit 0).

## Registration, realtime, undo, commands

- New product files `include/DataFileSaveLock.h`, `src/core/DataFileSaveLock.cpp` are in
  `tests/fork-sources.txt` and `tests/all-sources.txt`; the `.cpp` is in
  `src/core/CMakeLists.txt`. New test header
  `tests/src/core/DataFileSaveIntegrityTestParts3.h` is registered in both manifests.
  `src/core/DataFile.cpp` is declared in `tests/upstream-modifications.txt` for this cluster;
  the other inherited files were already declared.
- Nothing runs on the audio thread: these are open/save/restore/control-thread paths. No
  allocation, lock, wait, I/O or log was added to a process/render path.
- No control command was added and no command id or result schema changed; the
  `ControlCommandsSnapshot` canary is unmoved. `project.save` gains one refusal result and
  remains one honest save/undo step.
- Other `saveProjectFile`/`writeFile` callers (autosave, render, bounce, collab, mastering,
  `ControlCommandsAlternatives/Handoff/Documents/Project*`, `ScriptBindings`, `MainWindow`)
  pass no held lock and keep `writeFile()`'s own lock/release, exactly as before.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it (exit
  codes):** YES. With the unmodified product code, `test-2-red.log` exit **8** (three
  assertion failures quoted above). With the fix, `test-12-final-regression.log` exit **0**
  (2/2). The literal full reverse is `apply-reverse.log` (exit 0), `build-5-red-reverse.log`
  exit **2**, `test-7-red-reverse.log` exit **8**, then `apply-forward.log` exit 0,
  `build-6-green-restored.log` exit 0 and `test-8-green-restored.log` exit 0.
- **The test is deterministic:** YES. No sleeps, wall-clock reads, threads or timing races. The
  "other instance" is a real `QLockFile` held in the test process; the lock is aged with an
  injected mtime of epoch second `1000000`; file freshness is injected with
  `setModifiedTime(..., 1000000/2000000)`.
- **The fix is at the cause and complete:** YES. Every writer of `<name>.new` or the revision
  set now holds the same sidecar: `DataFile::writeFile`, `project.save` (before rotation), both
  restore paths, and recovery. Sibling callers that take no held lock keep the RAII default.
  The exact sibling list is in "Registration, realtime, undo, commands" above.
- **No collateral damage:** YES. With no contention, rotation, write, backup and replies are
  unchanged; the save/restore/revision/format family passes 9/9 (`test-13-final-family.log`),
  and old `.mmp`/`.mmpz` data is not touched by this patch (the sidecar is separate). The only
  new refusal is the one this finding asks for. `ArrangingCatchUpTest` fails here identically
  with the fix (`test-14-final-arranging.log`) and with the product change reverted
  (`test-10-baseline-arranging.log`); see Other findings.
- **Verdict evidence:** REAL, not ALREADY-FIXED/FALSE-POSITIVE. The pre-fix behaviour was
  reproduced RED (recovery adopted the live temp; both save paths ignored a held lock). This is
  not one of the AGENTS.md "Verified facts" intended behaviours.
- **Sibling commands:** the completion check lists `app.about`, `app.ask`, `app.help`,
  `app.online_help`, `app.task_cancel`, `app.tasks`, `app.tour`, `app.tours`,
  `app.update_check_get`, `app.update_check_run`, `app.update_check_set`, `app.version`,
  `app.whats_new`, `arrangement.fit_tracks`, `arrangement.get_state`. None is touched by this
  fix: the only changed command file is `src/core/ControlCommandsProjectFiles.cpp`, whose
  changed handler is `project.save`; its sibling `project.restore_revision` reaches the now
  locked `restoreProjectRevision()` and surfaces the existing error path, while
  `project.get_state` and `project.open` do not write a project file or rotate anything. The
  `app.*` and `arrangement.*` ids come from the train between the gate base and this branch,
  not from this patch.

## Other findings

- `ArrangingCatchUpTest` fails identically without this fix (`test-10-baseline-arranging.log`,
  exit 8) and with it (`test-14-final-arranging.log`, exit 8):
  `newDocumentsCarryTheirTypesTemplate()` cannot write
  `/home/kruzzzzy/.cache/ArrangingCatchUpTest/document-templates/mastering.mpt` because this
  sandbox mounts `~/.cache` read-only (`touch ~/.cache/...` -> `Read-only file system`), and
  `queuedInstrumentHandlesAreRemovedBeforeTheirTrackCanDie()` fails in the same baseline run.
  Environmental, unrelated.
- A full `cmake --build build` / `--target all` is refused by the lane guard
  (`bunny-guard: target 'all' not allowed`, exit 97). Instead the affected test targets were
  identified by searching for `writeFile`/`saveProjectFile` in `tests/` and rebuilt: the
  family above plus `TrackFolderTest`, `AafImportTest`, `ArrangingCatchUpTest`
  (`build-9-affected-tests.log`, exit 0).
