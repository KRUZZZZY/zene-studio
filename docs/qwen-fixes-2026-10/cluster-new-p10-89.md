FIXED

# cluster new-p10-89 — recovery can adopt a live save from another process (round 2)

Verdict: **REAL**, fixed at the cause in `src/core/DataFile.cpp`. Round 1 (`8421bcb81f`)
coordinated saves and recovery through `<file>.save-lock`; this round fixes the two things the
review sent back:

1. **The lock could be taken away from a live save.** `QLockFile`'s staleness age defaults to 30
   seconds, and on the Qt versions whose `isApparentlyStale()` applies the age rule a lock file
   older than that is reported stale and *removed* by the next contender. A save that legitimately
   runs longer (a bundle copying resources, a large project, a slow share) would lose its own lock,
   and the window this fix closes reopens. Both contenders now pin the criterion to "the holder's
   process is gone": `disableLockAge()` (`src/core/DataFile.cpp:228`) calls
   `setStaleLockTime(0)`, and it is called at `:510` (recovery) and `:1073` (save).
2. **The save side failed open.** Round 1 refused only on `QLockFile::LockFailedError` and went on
   to write `<name>.new` for any other lock error. That is the state this fix exists to prevent, so
   `writeFile()` now **fails closed on every failed acquisition** (`:1072`-`:1086`): it refuses,
   reports the reason (`saveLockRefusal()`, `:234`) and names the sidecar so a lock left by a
   crashed writer on another host can be cleared by hand. Recovery is fail-closed on the same
   terms and leaves the temp for the next open (`:509`-`:517`).

## The bug in the branch's base (`21c2f3ee2e:src/core/DataFile.cpp`)

- `:950` `const QString fullNameTemp = fullName + ".new";` — the temp name.
- `:996` `if (!writePayload(fullNameTemp, fullName, showError)) { return false; }` — writes the
  **complete** document into it.
- `:1070` `if( ! renameOverwrite( fullNameTemp, fullName ) )` — publishes it, later.
- `:433` `void recoverPendingWrite( const QString & fileName )`, which starts from
  `:442` `const QString pending = fileName + QStringLiteral( ".new" );` and adopts any complete
  temp (`:489` `renameOverwrite( pending, fileName )`), with `:1046`
  `replaceBackup( fullName, fullNameBak )` on the writer's side copying whatever the canonical path
  holds at that moment.

So in the window between `:996` and `:1070` a complete, unowned `.new` sits on disk. A second
instance opening the project adopts it; the writer's publish then fails with its temp gone and, if
recovery ran first, the writer copies the *recovered* content over the previous `.bak`. Reproduced
in the RED run below with no timing at all (`Recovered interrupted save: ".../song.mmp.new" adopted
as ".../song.mmp"` while a live holder owned the sidecar).

## What round 2 changed

- `src/core/DataFile.cpp:228` `disableLockAge()` — `setStaleLockTime(0)`: no age-based reclaim, so
  a save of any duration keeps its own lock. The PID check stays, which is what keeps the crash path
  open (measured below).
- `src/core/DataFile.cpp:234` `saveLockRefusal()` — the refusal says *why* (another instance is
  saving / the folder refuses the lock file / the lock file could not be created).
- `src/core/DataFile.cpp:1072` — the save's guard: `tryLock(0)` after `disableLockAge()`, and
  **any** failure refuses the save (`:1081` "… was NOT saved: …"). No temp is written.
- `src/core/DataFile.cpp:509` — the recovery guard: same lock, same staleness setting, still
  `tryLock(0)`; a refusal leaves both the temp and the project untouched, before any branch that
  adopts, discards, or refuses an oversized/unreadable temp.
- Callers/siblings unchanged from round 1 and re-checked: `recoverPendingWrite`'s one caller is
  `:672` (the `QString` constructor; the `QByteArray` constructor never reaches it);
  `writeFile`'s callers are `Song.cpp:2512`, `InstrumentTrackWindow.cpp:442`,
  `PianoRoll.cpp:5965`, `ControlCommandsTrackPresets.cpp:73`, all of which already treat `false`
  as "not saved"; `replaceBackup()`'s `<name>.bak.new` is not read by recovery and lies inside the
  same locked window; `writePayload()` is the only product writer of `<file>.new`.

## Measurements from this session (log folder)

`probe-lock-run.log` (a standalone Qt6Core probe, not part of the tree):

```
holder tryLock=1
default staleLockTime contender tryLock=0 (true means the live lock was reclaimed)
staleLockTime(0) contender tryLock=0 error=LockFailedError   (aged live lock still held)
directory-at-path tryLock=0 error=LockFailedError
```

So on **Qt 6.12** here the default age does not reclaim a live same-host lock, while the Qt
documentation states the 30 second default; the setting removes the age rule outright, which is
what makes the guarantee hold on every Qt the project builds against (5.15 included).

`probe-contend.log` — a holder process was killed with `kill -9` (no destructor, lock file left
with a dead pid, `-rw-r--r-- x.lock` still on disk), then a contender with `setStaleLockTime(0)`:

```
holder pid=40451 tryLock=1        (then: kill -9, KILL_EXIT=0, file still present)
contender tryLock=1 error=0 (1 means the dead holder's lock was reclaimed)
```

So `staleLockTime(0)` keeps the crash path open: a crashed writer's lock is reclaimed and adoption
still runs. (The test suite covers the "holder gone" half; a test that leaves a lock file behind
would have to build `QLockFile`'s private on-disk format, which differs between Qt 5.15 and Qt 6.)
Local TSan does not run on this box, so the concurrency argument is the design argument above plus
these measurements.

## Tests

`tests/src/core/DataFileSaveIntegrityTest.cpp` (declarations, `#include
"DataFileSaveIntegrityTestParts3.h"`), bodies in the new
`tests/src/core/DataFileSaveIntegrityTestParts3.h` (the round-1 bodies lived in
`...Parts2.h`, which the file-length ratchet then counted at 513 lines; Parts2 is back to its
480-line state and the new file is 228 lines):

| path | test | what it pins |
| --- | --- | --- |
| live save in the window, aged lock | `liveSaveInAnotherInstanceIsNotAdopted` | recovery refuses while the holder's sidecar is aged to 1970, reports it, keeps the temp/project/absent-bak, and the aged lock is still the holder's; once the holder goes the very same temp is adopted and the previous version becomes `.bak` |
| save beside a live save, aged lock | `saveIsRefusedWhileAnotherInstanceHoldsTheLock` | the save returns false, reports "was NOT saved", touches neither project nor `.bak`, writes no second temp, and does not take the aged lock of a live writer; once the holder goes the save lands and leaves no lock or temp |
| unusable lock, both sides | `unusableSaveLockFailsClosed` | an obstructed sidecar (a directory at the name) and, on `Q_OS_UNIX`, a folder that refuses the lock file: the save returns false, writes no temp, backs nothing up, and reports "was NOT saved"; recovery leaves a complete temp in place and keeps the project |
| normal path / reconfiguration / crash | existing slots: `ordinarySaveStillWritesAndKeepsABackup`, `adoptionHonoursTheDisableBackupConfiguration`, and every recovery test | unchanged behaviour |
| audio thread | none of this runs there (open/save only) | — |
| platforms | `QLockFile` is Qt Core on Linux/macOS/Windows; `setStaleLockTime(int)` is the Qt 5.15+ overload; no `flock`/`fcntl`, no `windows.h`, no std::filesystem | builds here, no platform code added |

Determinism: no sleeps, no threads, no wall-clock. The "other process" is a real `QLockFile` held
on the real sidecar in the test process (QLockFile refuses a lock whose recorded pid is a live
process); age is *injected* with `setModifiedTime(path, 1000000)`; adoption order likewise.

## Commands and EXIT codes (this session; logs in the r2 folder)

Green with the round-2 fix:
```
cmake --build build --target DataFileSaveIntegrityTest -j4 > logs/build-1-green.log 2>&1        EXIT=0
cd build/tests && ctest -R '^DataFileSaveIntegrityTest$' --output-on-failure > logs/test-1-green.log 2>&1   EXIT=0
cd build/tests && ./DataFileSaveIntegrityTest > logs/test-2-verbose-green.log 2>&1               EXIT=0
   PASS liveSaveInAnotherInstanceIsNotAdopted / saveIsRefusedWhileAnotherInstanceHoldsTheLock /
        unusableSaveLockFailsClosed      Totals: 30 passed, 0 failed
```
The fail-closed change is load-bearing (guard put back to round 1's `LockFailedError`-only form):
```
cmake --build build --target DataFileSaveIntegrityTest -j4 > logs/build-3-failopen.log 2>&1      EXIT=0
cd build/tests && ./DataFileSaveIntegrityTest unusableSaveLockFailsClosed liveSaveInAnotherInstanceIsNotAdopted \
    saveIsRefusedWhileAnotherInstanceHoldsTheLock > logs/test-4-failopen-red.log 2>&1            EXIT=1
   FAIL! unusableSaveLockFailsClosed: Not all expected messages were received
```
and the age pinning is called on both sides: removing both calls does not compile —
`build-2-round1-guard.log`: `DataFile.cpp:228: error: 'disableLockAge(QLockFile&)' defined but not used
[-Werror=unused-function]` (EXIT=2), so no build can silently drop it.

Step 4b, the whole product fix reverted (both rounds, `git diff 21c2f3ee2e -- src/core/DataFile.cpp`):
```
git diff 21c2f3ee2e -- src/core/DataFile.cpp > logs/fix.patch                     DIFF_EXIT=0   (124 lines)
git apply -R logs/fix.patch                                                       REVERT_EXIT=0
cmake --build build --target DataFileSaveIntegrityTest -j4 > logs/build-6-reverted-relink.log 2>&1   BUILD_EXIT=0
cd build/tests && ctest -R '^DataFileSaveIntegrityTest$' --output-on-failure > logs/test-7-reverted-red.log 2>&1   TEST_EXIT=8
   Totals: 27 passed, 3 failed - the three new-p10-89 slots, and only those
git apply logs/fix.patch                                                          APPLY_EXIT=0
cmake --build build --target DataFileSaveIntegrityTest -j4 > logs/build-7-restored.log 2>&1   BUILD_EXIT=0
cd build/tests && ctest -R '^DataFileSaveIntegrityTest$' --output-on-failure > logs/test-8-restored-green.log 2>&1   TEST_EXIT=0
```
(The first reverted build was cut short by the tool's 300 s limit while linking and left the binary
without its execute bit; `build-5-reverted.log` shows it stopping at "Linking CXX executable …".
I removed the binary and relinked — `logs/build-6-reverted-relink.log`, `BUILD_EXIT=0` — before the
RED run above.)

Collateral, `zene` plus the save-touching family:
```
cmake --build build --target zene DataFileFormatTest DataFileContainerRecoveryTest ProjectContainerSaveTest RoutingGraphTest SessionModelTest -j4 > logs/build-8-collateral.log 2>&1   BUILD_EXIT=0
cd build/tests && ctest -R '^(DataFileSaveIntegrityTest|DataFileFormatTest|DataFileContainerRecoveryTest|ProjectContainerSaveTest|RoutingGraphTest|SessionModelTest)$' --output-on-failure > logs/test-9-collateral.log 2>&1   TEST_EXIT=0   (6/6 passed)
```
After moving the three bodies to `Parts3.h`:
```
cmake --build build --target DataFileSaveIntegrityTest -j4 > logs/build-9-parts3.log 2>&1        EXIT=0
cd build/tests && ctest -R '^DataFileSaveIntegrityTest$' --output-on-failure > logs/test-10-parts3-green.log 2>&1   EXIT=0
```
Gates:
```
FEATURE_CHECK_LOG_DIR=logs/feature-check python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > logs/gates-1.log 2>&1   EXIT=1
   FAIL file-length: NEW: new file over 500 lines: tests/src/core/DataFileSaveIntegrityTestParts2.h (513)
   -> the three bodies moved to DataFileSaveIntegrityTestParts3.h (Parts2 back to 480 lines)
FEATURE_CHECK_LOG_DIR=logs/feature-check python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > logs/gates-2.log 2>&1   EXIT=0
   PASS fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology,
        doc-figures, complexity, file-length      RESULT: PASS
```

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.** Reverted:
  `test-7-reverted-red.log` `TEST_EXIT=8`, `Totals: 27 passed, 3 failed` — exactly
  `liveSaveInAnotherInstanceIsNotAdopted`, `saveIsRefusedWhileAnotherInstanceHoldsTheLock`,
  `unusableSaveLockFailsClosed`. Restored: `test-8-restored-green.log` `TEST_EXIT=0`. The test
  compiles without the fix (it holds a real `QLockFile` on the documented sidecar name), so RED is
  an assertion failure, not a build error.
- **The two round-2 defects are each pinned.** Fail-open: with the guard narrowed back to
  `LockFailedError`, `unusableSaveLockFailsClosed` fails (`test-4-failopen-red.log`, EXIT=1). Age:
  the three fixtures age the held sidecar to 1970 before the load/save and assert the refusal plus
  `holder.isLocked()`; `disableLockAge()` is what makes that hold on any Qt, and the compiler
  proves it is called (`-Werror=unused-function`, `build-2-round1-guard.log`).
- **Deterministic.** No wall-clock, sleeps, threads or races: the "other process" is a real
  `QLockFile` held in this process (refused because its recorded pid is alive), and every time
  value is injected with `setModifiedTime()`.
- **Fix at the cause and complete.** One sidecar name (`saveLockPath`, `:210`), one staleness
  policy (`disableLockAge`, `:228`), taken by the only writer of `<file>.new` (`:1072`, before
  `writePayload` at `:1088`) and the only reader of it (`:509`, before every recovery branch).
  Callers: `:672` for recovery; `Song.cpp:2512`, `InstrumentTrackWindow.cpp:442`,
  `PianoRoll.cpp:5965`, `ControlCommandsTrackPresets.cpp:73` for saves, all already
  `false`-means-not-saved. Sibling temp `<name>.bak.new` is outside recovery and inside the lock.
- **No collateral damage.** With no `<file>.new` recovery returns before the lock, so every
  existing load is unchanged (27 pre-existing slots plus `DataFileFormatTest`,
  `DataFileContainerRecoveryTest`, `ProjectContainerSaveTest`, `RoutingGraphTest`,
  `SessionModelTest` pass). A completed save leaves neither `.save-lock` nor `.new`. The new
  refusal covers *all* lock failures, which is the requested fail-closed behaviour: the message
  names the sidecar so the one case it can over-refuse (a lock left by a crash on another host on
  a share, which `staleLockTime(0)` no longer expires on time) is clearable by hand; on the same
  host a dead pid is still reclaimed, as `probe-contend.log` shows.
- **Realtime / undo / commands.** Nothing added on the audio path (no allocation, lock, wait,
  signal or I/O in `process*`/`play*`); the changed functions are `DataFile::writeFile`,
  `recoverPendingWrite` and three file-local helpers on the open/save path. One save is still one
  honest save: no undo step, no control command, no `ControlCommandsSnapshot` change.
- **Verdict is FIXED, not ALREADY-FIXED or FALSE-POSITIVE**, so that ASK does not apply; the
  pre-fix code showing the defect is quoted above from `21c2f3ee2e`.
- **Sibling commands: none.** This round touches `src/core/DataFile.cpp`,
  `tests/src/core/DataFileSaveIntegrityTest.cpp`, `...Parts2.h`, the new `...Parts3.h`,
  `tests/upstream-modifications.txt` and this report — no `ControlCommands*`,
  `ControlReversibilityTable*` or `ControlRegistry` file, no command id. The `app.*` /
  `arrangement.*` siblings the pre-submit check lists come from the train between the base and this
  branch, not from this fix; a file-save lock is below the command surface and changes no command's
  behaviour, error class or reversibility.

## Other findings

- `MainCliStartupTest::failedControlSocketStartupClearsBothSessionMarkers` fails on this box with
  `QLocalServer::listen: Unknown error 25` for its own `zene-qwen-5110-<uuid>` socket
  (`tests/src/core/MainCliStartupTest.cpp:154`). Round 1 measured it failing with this fix reverted
  and with a shorter `TMPDIR`, so it is environmental to this sandbox; not touched here.
- The first reverted build in this session was interrupted by the 300 s tool limit while linking
  `DataFileSaveIntegrityTest` and left the binary non-executable (`ls -l` showed `-rw-r--r--`),
  which made ctest report `BAD_COMMAND`; deleting and relinking fixed it (`build-6-reverted-relink.log`).
