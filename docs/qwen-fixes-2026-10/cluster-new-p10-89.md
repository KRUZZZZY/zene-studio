FIXED

# cluster new-p10-89 — recovery can adopt a live save from another process

Verdict: **REAL**, fixed at the cause in `src/core/DataFile.cpp`, with two deterministic
regression tests in `tests/src/core/DataFileSaveIntegrityTest.cpp` (+
`tests/src/core/DataFileSaveIntegrityTestParts2.h`).

## The bug, in the branch's own code

A save writes `<name>.new` and publishes it later, in two separate steps:

- `src/core/DataFile.cpp:950` (`git show HEAD:src/core/DataFile.cpp`) — `const QString fullNameTemp = fullName + ".new";`
- `src/core/DataFile.cpp:996` (`HEAD`) — `if (!writePayload(fullNameTemp, fullName, showError)) { return false; }` writes the **complete** document into the temp.
- `src/core/DataFile.cpp:1070` (`HEAD`) — `if( ! renameOverwrite( fullNameTemp, fullName ) )` publishes it.

Between those two statements a complete `.new` exists and the canonical file is still the old
one, and nothing says the temp belongs to a save that is still running. Recovery adopts any
complete temp it finds:

- `src/core/DataFile.cpp:433` (`HEAD`) — `void recoverPendingWrite( const QString & fileName )`
- `src/core/DataFile.cpp:442` (`HEAD`) — `const QString pending = fileName + QStringLiteral( ".new" );` — the only condition it starts from.
- `src/core/DataFile.cpp:483` (`HEAD`) — `replaceBackup( fileName, fileName + ".bak" )`, then
- `src/core/DataFile.cpp:489` (`HEAD`) — `renameOverwrite( pending, fileName )` — the temp is moved over the project.

So a second instance opening the project in that window consumes the writer's temp. The
writer's own `renameOverwrite( fullNameTemp, fullName )` then fails because the temp is gone
(`:1070`), it reports a failed save, and — when recovery ran *before* the writer's backup step
(`:1046` in the pre-fix file, `replaceBackup( fullName, fullNameBak )`) — the writer then copies
the *recovered* content over the previous `.bak`, destroying the only older copy. That is
exactly the finding's "the writer may then copy the new file as its backup and fail its final
rename because the temp is gone, losing the previous backup and reporting save failure".

Reproduced this session without any timing: the RED run below shows recovery adopting the temp
of a holder that still owns the file
(`Recovered interrupted save: ".../song.mmp.new" adopted as ".../song.mmp"`), and a save
completing while another instance held the file.

## The fix

One coordination primitive, both sides of the same cause (`src/core/DataFile.cpp`):

- `:210` `saveLockPath()` — `<file>.save-lock`: the sidecar a save of that file holds while a
  temp of its own exists. `QLockFile` (Qt Core, Qt 5.1+/Qt 6, no platform code) because it
  records the holder's pid: a writer that **crashed** leaves a lock whose pid is gone, which the
  next `tryLock()` reports as stale and removes, so adoption still runs for real orphans and no
  dead marker has to be cleaned by hand.
- `:1041` in `writeFile()` — the lock is taken immediately before `writePayload()` (`:1057`),
  i.e. before the only code that creates the temp, and is held by an RAII local until the
  function returns, so it covers the whole window in which `<file>.new` exists. If the lock is
  held elsewhere the save is **refused** and says so (`:1047`), instead of writing a second temp
  over a save already in flight. A lock failure for any *other* reason (a directory that refuses
  the lock file also refuses the temp) is not turned into a refusal: the payload write reports
  its own, accurate error.
- `:479` in `recoverPendingWrite()` — before anything is read, adopted or discarded, recovery
  takes the same lock; if it cannot, it reports that a save is in progress (`:483`) and leaves
  the temp **and** the project untouched. This one guard covers all recovery branches (adopt,
  discard-incomplete, oversized, unreadable), so a half-written temp of a live save is not
  deleted either.

Neither side waits (`tryLock(0)`): recovery runs inside an open, and a save that blocks a
session is a save that hangs.

Callers/siblings checked:

- `recoverPendingWrite` has one caller, `src/core/DataFile.cpp:641` (the `QString` constructor);
  the `QByteArray` constructor never reaches it, and its empty-name guard still returns before
  any temp path is derived.
- `writeFile` callers: `src/core/Song.cpp:2512`, `src/gui/instrument/InstrumentTrackWindow.cpp:442`,
  `src/gui/editors/PianoRoll.cpp:5965`, `src/core/ControlCommandsTrackPresets.cpp:73`. All treat
  `false` as "not saved", exactly as the pre-existing backup/rename refusals do; `Song::save`
  returns the flag (`Song.cpp:2512-2514`).
- The sibling temp `replaceBackup()` creates (`<name>.bak.new`) is never read by recovery, and it
  is created inside the same locked window.
- `writePayload` (`:1057`) is the only product writer of `<file>.new` for both the legacy and the
  v2-container path, so the container save is covered by the same lock.

Realtime / undo / commands: no audio-path function touched (this is the open/save path only), no
undo step added, no control command added or changed.

## Test matrix

| path | test | result |
| --- | --- | --- |
| normal save | `DataFileSaveIntegrityTest::ordinarySaveStillWritesAndKeepsABackup` (existing) + `:saveIsRefusedWhileAnotherInstanceHoldsTheLock` second half (save lands once the holder is gone, no lock or temp left) | PASS |
| failure: live save in the window | `:liveSaveInAnotherInstanceIsNotAdopted` (temp kept, project unchanged, no backup made, message printed) | PASS |
| failure: save while another instance holds the file | `:saveIsRefusedWhileAnotherInstanceHoldsTheLock` (returns false, project and previous `.bak` intact, no second temp) | PASS |
| exhaustion | none added — the change allocates no resource and adds no limit | n/a |
| reconfiguration | `:adoptionHonoursTheDisableBackupConfiguration` (existing, `app/disablebackup`) | PASS |
| crash / stale coordination | `:liveSaveInAnotherInstanceIsNotAdopted` second half (holder gone → the very same temp is adopted) — the lock left by a *crashed* writer is not exercised by a test: building one means writing `QLockFile`'s private on-disk format, which differs between Qt 5.15 and Qt 6; QLockFile documents that a lock whose pid is gone is stale and removed by the next `tryLock`. Said plainly rather than claimed. | PASS (indirect) |
| audio thread | none of this runs on it (load/save paths only) | n/a |
| platforms | `QLockFile` is Qt Core on Linux/macOS/Windows; no `flock`/`fcntl`, no `windows.h`, no std::filesystem added | builds here, no platform code added |

Determinism: no sleeps, no threads, no wall-clock. The "other process" is simulated by holding
the **real** `<file>.save-lock` with a real `QLockFile` in the test process — QLockFile refuses a
lock whose recorded pid is a live process, and the test process is one, so this is the same
on-disk state another instance's save produces. Adoption order is injected with
`setModifiedTime()` as the other recovery tests do.

## Commands and exit codes (all from this session; logs in the run's log folder)

Baseline before touching anything:
```
cmake --build build --target DataFileSaveIntegrityTest -j4 > logs/build-0-baseline.log 2>&1   EXIT=0
cd build/tests && ctest -R '^DataFileSaveIntegrityTest$' --output-on-failure > logs/test-0-baseline.log 2>&1   EXIT=0
```
RED (tests added, product untouched):
```
cmake --build build --target DataFileSaveIntegrityTest -j4 > logs/build-1-red.log 2>&1   EXIT=0
cd build/tests && ctest -R '^DataFileSaveIntegrityTest$' --output-on-failure > logs/test-1-red.log 2>&1   EXIT=8
  FAIL! liveSaveInAnotherInstanceIsNotAdopted: recovered the live save's temp
  FAIL! saveIsRefusedWhileAnotherInstanceHoldsTheLock: a save ran while another instance held it
```
GREEN (fix applied):
```
cmake --build build --target DataFileSaveIntegrityTest -j4 > logs/build-2-green.log 2>&1   EXIT=0
cd build/tests && ctest -R '^DataFileSaveIntegrityTest$' --output-on-failure > logs/test-2-green.log 2>&1   EXIT=0
```
Step 4b, proof that the tests catch the bug:
```
git diff -- src/core/DataFile.cpp > logs/fix.patch            EXIT=0   (93 lines)
git apply -R logs/fix.patch                                   EXIT=0
cmake --build build --target DataFileSaveIntegrityTest MainCliStartupTest -j4 > logs/build-4-reverted.log 2>&1   EXIT=0
cd build/tests && ctest -R '^DataFileSaveIntegrityTest$' --output-on-failure > logs/test-5-reverted-red.log 2>&1   EXIT=8
  Totals: 27 passed, 2 failed   (the two new tests, and only those)
git apply logs/fix.patch                                      EXIT=0
cmake --build build --target DataFileSaveIntegrityTest -j4 > logs/build-5-restored.log 2>&1   EXIT=0
cd build/tests && ctest -R '^DataFileSaveIntegrityTest$' --output-on-failure > logs/test-7-restored-green.log 2>&1   EXIT=0
```
Collateral (product binary and the save-touching test family):
```
cmake --build build --target zene -j4 > logs/build-6-zene.log 2>&1                            EXIT=0
cmake --build build --target DataFileFormatTest DataFileContainerRecoveryTest ProjectContainerSaveTest MainCliStartupTest -j4 > logs/build-3-family.log 2>&1   EXIT=0
cd build/tests && ctest -R '^(DataFileSaveIntegrityTest|DataFileFormatTest|DataFileContainerRecoveryTest|ProjectContainerSaveTest|MainCliStartupTest)$' -j2 --output-on-failure > logs/test-3-family.log 2>&1   EXIT=8
  (4 of 5 pass; MainCliStartupTest fails - see Other findings, it fails with the fix reverted too:
   logs/test-6-maincli-reverted.log EXIT=8, same assertion, so it is not caused by this change)
cmake --build build --target RoutingGraphTest TrackRouteDropTest SessionModelTest -j4 > logs/build-7-collateral.log 2>&1   EXIT=0
cd build/tests && ctest -R '^(DataFileSaveIntegrityTest|DataFileFormatTest|DataFileContainerRecoveryTest|ProjectContainerSaveTest|RoutingGraphTest|TrackRouteDropTest|SessionModelTest)$' --output-on-failure > logs/test-8-collateral.log 2>&1   EXIT=0   (7/7 passed)
```
Gates:
```
FEATURE_CHECK_LOG_DIR=logs/feature-check python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > logs/gates-1.log 2>&1   EXIT=0
  PASS fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology,
       doc-figures, complexity (no new item in your files), file-length (no new item in your files)
  RESULT: PASS
```

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.** Reverted:
  `ctest -R '^DataFileSaveIntegrityTest$'` EXIT=8, 27 passed / 2 failed, both failures being the
  new slots (`logs/test-5-reverted-red.log`); restored: EXIT=0 (`logs/test-7-restored-green.log`).
  The compile of the test does not depend on the fix (it uses `QLockFile` and the sidecar name
  directly), so RED is an assertion failure, not a build error.
- **Deterministic, no wall-clock.** No sleeps, no threads, no timing assertions. The "live
  writer" is a real `QLockFile` held on the real sidecar path in the test process (QLockFile
  treats a lock whose recorded pid is a live process as held, and this process is one); the
  recovery's own "older/newer" comparison uses `setModifiedTime()` injected epochs, as
  `olderPendingSaveDoesNotReplaceANewerProject` already does.
- **Fix at the cause and complete.** One primitive (`saveLockPath`, `DataFile.cpp:210`) used by
  the only writer of `<file>.new` (`writeFile`, `:1041`, before `writePayload` at `:1057`) and by
  the only reader of it (`recoverPendingWrite`, `:479`, before every branch that adopts, discards
  or refuses). `recoverPendingWrite`'s one caller is `:641`; `writeFile`'s callers are
  `Song.cpp:2512`, `InstrumentTrackWindow.cpp:442`, `PianoRoll.cpp:5965`,
  `ControlCommandsTrackPresets.cpp:73`, and all of them already treat `false` as "not saved"
  (that is the existing contract for a refused backup or rename). `replaceBackup`'s `<name>.bak.new`
  sibling is not part of recovery and lies inside the same locked window.
- **No collateral damage.** With no `<file>.new` present, recovery returns at `:464` and never
  touches the lock — every existing load is byte-for-byte the old path (27 pre-existing slots in
  `DataFileSaveIntegrityTest` still pass, plus `DataFileFormatTest`, `DataFileContainerRecoveryTest`,
  `ProjectContainerSaveTest`, `RoutingGraphTest`, `TrackRouteDropTest`, `SessionModelTest`).
  A completed save leaves no `.save-lock` and no `.new` (`:saveIsRefusedWhileAnotherInstanceHoldsTheLock`
  asserts both), so no later save is refused. The refusal is scoped to `LockFailedError` only: a
  directory that cannot hold the lock file cannot hold the temp either, and that case keeps the
  old, accurate "could not open … for writing" report instead of a new refusal.
- **Verdict evidence (REAL).** Pre-fix lines quoted above: temp written at `HEAD:950`/`996`,
  published at `HEAD:1070`; recovery adopts on "complete" alone at `HEAD:433/442/483/489`. The RED
  run prints recovery adopting a temp owned by a live holder.
- **Realtime / undo / commands.** Nothing added on the audio path (no allocation, lock, wait,
  signal or I/O in any `process*`/`play*` function - the changed functions are
  `DataFile::writeFile`, `recoverPendingWrite` and a path helper, all on the open/save path);
  one save is still one honest save (no new undo step, no new command, no
  `ControlCommandsSnapshot` change).

## Other findings

- `MainCliStartupTest::failedControlSocketStartupClearsBothSessionMarkers` fails on this box with
  `QLocalServer::listen: Unknown error 25` for its own `zene-qwen-5110-<uuid>` socket, before any
  CLI run (`tests/src/core/MainCliStartupTest.cpp:154`). It fails identically with this fix
  reverted (`logs/test-6-maincli-reverted.log`, EXIT=8) and with a shorter `TMPDIR`
  (`logs/test-10-maincli-shorttmp.log`, EXIT=8), so it is environmental to this sandbox and
  unrelated to this cluster. Not touched.
