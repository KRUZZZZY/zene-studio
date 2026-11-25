FIXED

# cluster new-p10-89-port - recovery can adopt a live save from another process (round 5)

The finding itself (a second instance adopting the temp of a save that is still running)
was fixed in round 4 and stays fixed. Round 4's sidecar NAME introduced two regressions and
a Windows fixture defect; this round replaces the name scheme at its cause, and proves the
new behaviour with three regression tests that fail on round 4's code and pass on this one.

## The name scheme, and why it was wrong

Round 4 named the sidecar `<name>.save-lock` - a ten byte suffix on top of a project name
that already carries three derived names. That is eleven bytes more than the save itself
needs:

| derived name | bytes over `<name>` |
|---|---|
| `<name>.new` | 4 |
| `<name>.bak` | 4 |
| `<name>.bak.new` (longest a save writes) | 8 |
| `<name>.save-lock` (round 4) | 10 |
| `<name>.save-lock.rmlock` (QLockFile's own reclaim marker) | 17 |
| `<name>#` (this round) | 1 |
| `<name>#.rmlock` (this round) | 8 |

On a filesystem with `NAME_MAX` 255 (`pathconf(build, _PC_NAME_MAX)` = 255 here) a save
supports a 247 byte project name, because 247 + 8 = 255. Round 4's sidecar broke exactly
that: I saw it, twice, in this session (logs 18 and 25, quoted below) - a save at 247 bytes
is refused because its lock name is 257 bytes, and at 240 bytes the lock can be taken
(250 bytes) but the `.rmlock` Qt needs to reclaim a dead holder's lock is 257 bytes, so a
complete interrupted save stays pending forever.

The fix is in the constructor of the name, `DataFileSaveLock::lockPathFor`
(`src/core/DataFileSaveLock.cpp:63-91`): the same directory, and the full file name with one
ASCII byte (`#`) appended, so

- `<lock>` + `.rmlock` is never longer than `<name>` + `.bak.new` (8 = 8, asserted by
  `saveLockNameFitsTheSavesOwnNameBudget`);
- the lock is one per project file, in the project's own directory;
- no project extension ends in `#`, so the sidecar can never be read back as a project or
  data file (`DataFile::normalisedName` on the lock path differs from it, asserted);
- the length limit is documented where the name is built and where it is declared
  (`include/DataFileSaveLock.h:88-104`).

The design the reviewer asked me to keep is untouched: the lock is taken at the save, at
recovery, at `project.save` and at both restores, rotation happens inside it, and a save that
cannot take it refuses. Only `lockPathFor`'s output changed. Every caller already asked it
for the path (`src/core/DataFile.cpp:226`, `src/core/DataFileSaveLock.cpp:57,91`), so there
was no second copy of the suffix to update; the two comments that still named
`<name>.save-lock` are updated (`src/core/DataFile.cpp:199-201` and `:486-489`), as are the
header's class comment, `tests/upstream-modifications.txt` (the registration reason for
`DataFile.cpp`) and the `.save-lock` wording in the tests.

## Regression tests (RED before, GREEN after)

All three are in `tests/src/core/DataFileSaveIntegrityTestParts3.h`:

1. `saveLockNameFitsTheSavesOwnNameBudget()` (`:403`) - same directory, full name + one byte,
   not a name any save writes, and `lockPathFor(x).size() + len(".rmlock") <= x.size() + len(".bak.new")`.
2. `saveAtTheNameLengthLimitLandsAndLeavesNoLock()` (`:430`) - the longest name the save
   supports, derived from `pathconf(_PC_NAME_MAX) - 8` = 247 here: two saves must land (the
   second one writes `<name>.bak.new` at exactly `NAME_MAX` bytes), the backup must be the
   previous generation, and no lock or `.rmlock` may be left.
3. `deadWritersLockIsReclaimedAtTheNameLengthLimit()` (`:476`, Unix only) - a REAL dead
   holder: a forked child takes the lock with `QLockFile::tryLock` and `_exit()`s without
   unlocking, so what stays on disk is Qt's own lock file naming a pid that no longer exists.
   At `pathconf - 15` = 240 bytes the next open must reclaim it and adopt the complete
   interrupted `.new`. No sleep, no wall-clock: the child is reaped with `waitpid` before the
   parent probes, and file times are injected (`setModifiedTime(target, 1000000)`,
   `setModifiedTime(temp, 2000000)`), never taken from the clock.

The Windows fixture defect the reviewer found is also fixed: `setModifiedTime`
(`tests/src/core/DataFileSaveIntegrityTestParts2.h:298-323`) no longer opens the file for
`ReadWrite` (which QLockFile's `FILE_SHARE_READ` handle refuses). It now writes the timestamp
through metadata-only access - `utimensat` on `QFile::encodeName(path)` on Unix,
`CreateFileW(FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ|WRITE|DELETE, OPEN_EXISTING)` +
`SetFileTime` on Windows - and each aged-lock test then asserts the age really landed:
`isAgedBefore1971(lockPath)` (`:44`), i.e. `QFileInfo::lastModified()` is before 1971. The
three `.save-lock` literals in the tests are replaced by `lockPathFor(target)`, the pinned
`QCOMPARE` now expects `target + '#'` (`:320`), and every other assertion is unchanged.
`tests/src/core/ProjectContainerSaveTestParts1.h:458` (the sibling that asserted no lock was
left after `project.save`) asks `lockPathFor` too, so it stays a real assertion instead of
becoming vacuous.

## Commands and EXIT codes (this session)

Log folder: `/home/kruzzzzy/.config/kads/zene/state/runs/new-p10-89-port.r5/logs`.

RED - the tests against round 4's product code (`lockPathFor` returning `.save-lock`):

| # | command | exit |
|---|---|---|
| 17 | `cmake --build build --target DataFileSaveIntegrityTest -j4` | 0 |
| 18 | `ctest -R DataFileSaveIntegrityTest --output-on-failure` (from `build/tests`) | **8** |

`18-test-red.log`: `Totals: 31 passed, 4 failed`; the four failures are exactly the four
assertions this round owns:

```
FAIL!  : aHeldSaveLockIsExclusiveAndHandedToWriteFile() Compared values are not the same
   Actual   (lockPath)                   : ".../song.mmp.save-lock"
   Expected (target + QLatin1Char( '#' )): ".../song.mmp#"
FAIL!  : saveLockNameFitsTheSavesOwnNameBudget() Compared values are not the same
   Actual   (lockInfo.fileName())                       : "song.mmp.save-lock"
   Expected (targetInfo.fileName() + QLatin1Char( '#' )): "song.mmp#"
FAIL!  : saveAtTheNameLengthLimitLandsAndLeavesNoLock() 'lockPath.size() <= target.size() + 1' returned FALSE
FAIL!  : deadWritersLockIsReclaimedAtTheNameLengthLimit() 'recorder.messages().contains(
          QStringLiteral( "Recovered interrupted save" ) )' returned FALSE. (the dead writer's lock
          was not reclaimed, so the interrupted save stayed pending; captured: [Found ".../xxx....new",
          left by a save of ".../xxx....mmp" that may still be in flight ...])
```

The last line is the reviewer's `[not-fixed]` item reproduced: the lock of a dead writer at a
240 byte name is not reclaimed, and a complete interrupted save stays pending.

GREEN - with the fix:

| # | command | exit |
|---|---|---|
| 19 | `cmake --build build --target zene DataFileSaveIntegrityTest ProjectContainerSaveTest DataFileContainerRecoveryTest DataFileFormatTest -j4` | 0 |
| 20 | `ctest -R "DataFileSaveIntegrityTest\|ProjectContainerSaveTest\|DataFileContainerRecoveryTest\|DataFileFormatTest"` | 0 (4/4) |
| 21 | `./DataFileSaveIntegrityTest saveLockNameFitsTheSavesOwnNameBudget saveAtTheNameLengthLimitLandsAndLeavesNoLock deadWritersLockIsReclaimedAtTheNameLengthLimit aHeldSaveLockIsExclusiveAndHandedToWriteFile` | 0 (6 passed, 0 failed, 0 skipped) |
| 22 | rebuild after the `#ifdef Q_OS_UNIX` restructure of test 3 | 0 |
| 23 | `./DataFileSaveIntegrityTest` (whole binary) | 0 (35 passed, 0 failed, 0 skipped) |

4b - prove the tests catch the bug (`git diff` over product files only):

| # | command | exit |
|---|---|---|
| - | `git diff -- include/DataFileSaveLock.h src/core/DataFileSaveLock.cpp src/core/DataFile.cpp > logs/fix.patch` | 0 (96 lines) |
| - | `git apply -R logs/fix.patch` | 0 |
| 24 | `cmake --build build --target DataFileSaveIntegrityTest -j4` | 0 |
| 25 | `./DataFileSaveIntegrityTest` | **4** (31 passed, 4 failed - the four above) |
| - | `git apply logs/fix.patch` | 0 |
| 26 | `cmake --build build --target zene DataFileSaveIntegrityTest -j4` | 0 |
| 27 | `./DataFileSaveIntegrityTest` | 0 (35 passed, 0 failed, 0 skipped) |
| 28 | `ctest -R "DataFileSaveIntegrityTest\|ProjectContainerSaveTest\|DataFileContainerRecoveryTest\|DataFileFormatTest"` | 0 (100% passed out of 4) |

## Gates

`python3 .../qwen/feature_check.py <worktree>` printed a traceback and exited 1
(log `29-feature-check.log`): the script caches one base-commit listing per train, the base
of this branch is now `9913d36d53` (no entry in the cache), and refreshing it needs
`git -C ~/.local/share/zene-bunny-fix/qwen/gate-baselines/tree checkout --detach 9913d36d53`,
which the lane guard refuses:

```
bunny-guard: 'git -C .../gate-baselines/tree checkout -q --detach 9913d36d53...' is not allowed
in a Bunny lane (AGENTS.md rules 1, 2, 11, 12).
```

The cache directory itself is on a read-only mount in this sandbox (`OSError: [Errno 30]
Read-only file system` when I tried to write the entry), so the tool cannot run here at all.
I did not work around the guard. Instead I ran the same eight gates the script runs, each one
unpiped, and produced the base listing it would have compared against:

| gate | command | exit |
|---|---|---|
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 |
| check-namespace | `tests/scripted/check-namespace` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 |
| complexity | `bash tests/complexity-gate.sh --check` | **2** (pre-existing: `lizard is not installed`) |
| file-length | `bash tests/file-length-gate.sh --check` | 0 (2789 sources, 4 grandfathered, no regression) |

Logs `33-gates-individual.log` and `33-gate-*.log`. The two ratchet gates compare against the
base listing; I produced it by extracting the base commit read-only
(`git archive 9913d36d53 | tar -x -C <run tmp>/baseline`, exit 0) and running both gates
there (`30-base-complexity.log` exit 2 - same missing `lizard` - and
`31-base-file-length.log` exit 0). Both base runs print **zero** `REGRESSION:` lines, so the
base listing is empty for both gates, and the comparison each ratchet then performs is the
`--check` result above: no item is new in my files. Nothing about the gates' limits,
baselines or tsv files was touched (`git status` lists no `tests/*-baseline.tsv`).

Registration: no new product file this round; the two fork-new files were registered in round
4 and stay in `tests/fork-sources.txt` / `tests/all-sources.txt`, and the changed upstream
files (`src/core/DataFile.cpp`, `tests/src/core/DataFileSaveIntegrityTestParts2.h`,
`tests/src/core/ProjectContainerSaveTestParts1.h`) are registered in
`tests/upstream-modifications.txt`, whose `DataFile.cpp` reason now names the new sidecar.
`fork-sources` and `all-sources-reproduce` both pass, which is what those two lists verify.

## Test matrix

| path | covered by |
|---|---|
| normal save | `successfulBackupReplacesTheOlderBackupAtomically`, `saveAtTheNameLengthLimitLandsAndLeavesNoLock` (twice, so the backup copy runs) |
| recovery of a real orphan | `orphanedNewFileIsAdoptedWithReport`, `deadWritersLockIsReclaimedAtTheNameLengthLimit` (dead pid, not the same process) |
| live save of another instance | `liveSaveInAnotherInstanceIsNotAdopted` (adopt nothing while held, adopt after `unlock()`) |
| failure - lock unusable | `unusableSaveLockFailsClosed` (occupied sidecar name; folder that refuses the lock file, `Q_OS_UNIX` half) |
| failure - save cannot publish | `failedBackupCopyKeepsThePreviousBackup`, `unremovableBackupDoesNotBlockANewProject`, `saveStoppedMidFlightKeepsCanonicalComplete` (fault seam `ZENE_TEST_SAVE_FAULT`) |
| reconfiguration | `adoptionHonoursTheDisableBackupConfiguration` (`app/disablebackup` both ways) |
| exhaustion / boundary | the two name-length tests at `NAME_MAX-8` and `NAME_MAX-15` |
| audio thread | untouched: the diff is one string concatenation in the save-lock path plus comments; no process/render code, no allocation or lock added on the audio path |
| platform build | Unix: `utimensat`, `fork`/`waitpid`, `pathconf` under `#ifdef Q_OS_UNIX`; Windows: `CreateFileW`/`SetFileTime` under `#ifdef Q_OS_WIN`, and test 3's Unix-only body is `#ifdef`ed with a `QSKIP` body on Windows, so neither side's code is visible to the other compiler. NOT verified here: this session built Linux only - the MSVC/macOS builds were not run, so the Windows branch is reasoned about, not compiled |

Test 3 skips on Windows by design - a `MAX_PATH` filesystem does not have this failure mode,
and a dead holder there needs a different fixture; test 1 (the name budget) runs on every
platform and is the portable statement of the rule.

## Self-check

- **The pre-submit check's one FAIL is the gate tool, not a gate: `gates (feature_check.py)`.**
  `submit_check.py` (log `34-submit-check.log`, exit 1) reports every other item PASS and
  re-runs `feature_check.py`, which cannot start here: its per-train base listing is missing
  for base `9913d36d53`, refreshing it needs a `git checkout` in
  `~/.local/share/zene-bunny-fix/qwen/gate-baselines/tree` that the lane guard refuses, and
  the cache directory is a read-only mount (`OSError: [Errno 30] Read-only file system`, log
  `29-feature-check.log`). The same eight gates the script runs were run individually
  (table above): six exit 0, `file-length` exit 0, `complexity` exit 2 for a missing `lizard`
  at both the base commit and the worktree. The fix for the tool is host-side (pre-warm
  `gate-baselines/<base>.json`, or give a lane a writable cache); I did not touch the gate
  limits, the tsv baselines or the guard.
- **The regression test FAILS with the product change reverted and PASSES with it (exit codes).**
  Reverted: `git apply -R logs/fix.patch` exit 0, build 24 exit 0, `./DataFileSaveIntegrityTest`
  exit **4** (31 passed, 4 failed). Restored: `git apply logs/fix.patch` exit 0, build 26 exit 0,
  same binary exit **0** (35 passed, 0 failed, 0 skipped). Logs 24/25 and 26/27.
- **The test is deterministic: no wall-clock timing, sleeps or races.** No sleep and no clock
  read anywhere. The recovery ordering is injected (`setModifiedTime(target, 1000000)` older
  than `setModifiedTime(temp, 2000000)`); the dead holder is synchronised with `waitpid`
  before the parent probes, so the pid is provably gone when the reclaim runs; the live holder
  is the test's own `QLockFile`, whose recorded pid is this live process. Length limits come
  from `pathconf(dir, _PC_NAME_MAX)` on the directory the test writes into, not from a
  constant, so the boundary is the filesystem's real one.
- **The fix is at the cause and complete.** The cause is the name built in `lockPathFor`
  (`src/core/DataFileSaveLock.cpp:63-91`); that is the single place the suffix exists. All
  three callers (`:57` construction, `:91` `tryAcquireForProject` refusal, `src/core/DataFile.cpp:226`
  `lockForWrite` refusal) already go through it, so no sibling keeps the old string. Every
  test that pinned or read the old name now derives it from `lockPathFor`
  (`DataFileSaveIntegrityTestParts3.h`, `ProjectContainerSaveTestParts1.h:458`), including the
  one `QCOMPARE` that pinned it literally. Registration reasons updated.
- **No collateral damage.** The lock is still taken at the same five places, still non-blocking,
  still released by RAII, and the sidecar is still removed when the last holder goes. The only
  observable difference is its name, which nothing but this module, its tests and two comments
  ever spelled out (`grep save-lock` now finds only historical references in comments and this
  report). Project and data files are untouched, so old projects load exactly as before - the
  recovery, revision-restore, `project.save` and container-save suites all pass (log 28).
  The limit the new name asserts is not a limit on callers: it is one byte shorter on the
  longest name, so every name a save already supported stays supported, and the 247 byte
  boundary is now proven to save rather than to fail.
- **ALREADY-FIXED / FALSE-POSITIVE evidence.** Not claimed: the verdict is FIXED, and the
  behaviour the finding described (a live save adopted by recovery) stays covered by
  `liveSaveInAnotherInstanceIsNotAdopted`, which passes here (log 23).

## Other findings

- Reproduce the base listing used above with: `git archive 9913d36d53 | tar -x -C <dir> &&
  (cd <dir> && bash tests/complexity-gate.sh --check; bash tests/file-length-gate.sh --check)`. The
  extracted tree was scratch and has been removed; the two gate outputs are logs 30 and 31.

- `tests/complexity-gate.sh --check` exits 2 in this sandbox: `lizard is not installed`. This
  is environment state, identical at the base commit and in the worktree, so it measures
  nothing today (the gate's comparison sees no `REGRESSION:` line either way). New functions
  in this round are `isAgedBefore1971`, `longestProjectBasename`, `projectNameOfLength`,
  `leaveLockOfDeadProcess` and the reworked `setModifiedTime` - all straight-line test
  fixtures with no branching beyond one `if`; the product change adds no function at all.
- `qwen/feature_check.py` cannot refresh a base listing for a train it has not seen before:
  its cache tree needs a `git checkout` the lane guard refuses, and the cache directory is a
  read-only mount. Any lane cut from `050/m0-train-53` hits this. The tool needs its cache
  pre-warmed on the host, or a writable cache dir per lane.
- QLockFile's reclaim marker is `.rmlock` (7 bytes), which is why the budget is 8 bytes over the
  project name and why one byte of suffix is the largest that is safe.
