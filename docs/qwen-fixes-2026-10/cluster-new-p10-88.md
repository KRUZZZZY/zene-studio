FIXED

# Cluster new-p10-88: an unparsable root version derives from creatorversion

Verdict: FIXED. A root `version` attribute that does not parse as a file version
number (invalid text, a value above 32 bits, or a dotted value such as `1.2`) no
longer selects file version 0. It derives the version from `creatorversion`, the
same way a file with no `version` attribute does.

## Mechanism

The finding's stated mechanism is inverted. Before the fix, the parse-failure
branch of `DataFile::loadData` (original `src/core/DataFile.cpp:2834`) assigned
`toUInt()`'s 0 to `m_fileVersion`. The upgrade gate (original 2875, now
`src/core/DataFile.cpp:2886`) is `if (m_fileVersion < UPGRADE_METHODS.size()) { upgrade(); }`.
A version of 0 is below every table size, so every upgrade ran on such a file.
The visible symptom is the one the probe fixture describes
(`tests/src/core/DataFileFormatTest.cpp:120-121`): the file's
`<sampletrack vol="1"/>` is rescaled by 100 by `upgrade_0_2_1_20070501`.

The sibling branch (`src/core/DataFile.cpp:2825-2830`, no `version` attribute or
`version="1.0"`) already sets the version with `legacyFileVersion()`. The fix
makes the parse-failure branch use the same derivation.

## Change

- `src/core/DataFile.cpp:2831-2846`: the parse-failure branch assigns
  `legacyFileVersion()` instead of the `toUInt()` result. The success path
  assigns the parsed value unchanged. `success` is now initialised to `false`.
- `tests/src/core/DataFileFormatTest.cpp:273-337`: seven test functions (below).
- `tests/upstream-modifications.txt`: one ledger line for `src/core/DataFile.cpp`
  naming the change and the three `...FallsBackToCreatorVersion` tests.

No change to `upgrade()` (`src/core/DataFile.cpp:2638`), to the constructors, or
to the save path. `git diff -U2 src/core/DataFile.cpp` shows one hunk,
`@@ -2831,7 +2831,18 @@`.

## Tests

- `currentVersionSkipsUpgrades` (line 273): `version="31"` keeps
  `originalFileVersion()` 31, `sourceFormatChanged()` false, and `vol` 1.0.
- `invalidVersionFallsBackToCreatorVersion` (line 283): `version="invalid"`
  yields the same `originalFileVersion()` as a no-version file with
  `creatorversion="1.3.0"` (17), and `vol` stays 1.0.
- `overflowingVersionFallsBackToCreatorVersion` (line 294): `version="4294967296"`,
  same comparison.
- `dottedVersionFallsBackToCreatorVersion` (line 305): `version="1.2"` with
  `creatorversion="1.2.0"` yields the no-version companion's value (16), and
  `vol` stays 1.0.
- `invalidVersionDoesNotRunLegacyUpgrades` (line 316),
  `overflowingVersionDoesNotRunLegacyUpgrades` (line 323),
  `dottedVersionDoesNotRunLegacyUpgrades` (line 331): the same three inputs
  leave `<sampletrack vol="1"/>` at 1.0.

## Commands and results (this session)

| Label | Command | Exit | Log |
|---|---|---|---|
| p5-red-hunk-reversed | `git diff HEAD~1^ HEAD -- src/core/DataFile.cpp > logs/fix.patch`, then `git apply -R logs/fix.patch`, then `cmake --build build --target zene DataFileFormatTest` and `ctest -R '^DataFileFormatTest$' --output-on-failure` | build 0; ctest 8 (11 passed, 6 failed of 17 test functions, see below) | `logs/evidence-1-patch.log`, `logs/build-7-red.log`, `logs/test-7-red.log` (ends `EXIT=8`) |
| p5-green-restored | after `git apply logs/fix.patch` (`logs/evidence-2-restore.log`, EXIT=0), same build and ctest | build 0; ctest 0 (1/1 passed) | `logs/build-8-green.log`, `logs/test-8-green.log` (ends `EXIT=0`) |
| p5-family | ctest run over eight tests (DataFileFormatTest, DataFileSaveIntegrityTest, DataFileContainerRecoveryTest, ProjectOpenIntegrityTest, ProjectVersionTest, LanesUpconvertTest, ScenesUpconvertTest, StableTrackIdsTest); the log records no exit code | 8/8 Passed | `logs/test-4-family.log` (corroboration) |

All paths above are under `/home/kruzzzzy/.config/kads/zene/state/runs/new-p10-88.r1/`.

Runs in the run folder `logs/` also record `DataFileFormatTest` results (for example
`test-3-green.log` and `test-6-restored.log` at 100%, `test-5-reverted.log` with 6
failures). Those logs do not record exit codes, so they are cited as corroboration only.
Gate logs `gate-1-upstream.log` (PASS) and `gate-2-fork-sources.log` (PASS) are from
the earlier run; `gates-3-feature-check.log` ends with `RESULT: PASS`.

With the hunk reversed, the six failing functions were:
`invalidVersionFallsBackToCreatorVersion`, `overflowingVersionFallsBackToCreatorVersion`,
and `dottedVersionFallsBackToCreatorVersion` ("Compared values are not the same");
`invalidVersionDoesNotRunLegacyUpgrades`, `overflowingVersionDoesNotRunLegacyUpgrades`,
and `dottedVersionDoesNotRunLegacyUpgrades` ("Compared doubles are not the same").

## Self-check

(1) Red/green with the product hunk reversed and restored. `git diff HEAD~1^ HEAD --
src/core/DataFile.cpp` was saved to `logs/fix.patch` and reversed with `git apply -R`
(`logs/evidence-1-patch.log`, both steps EXIT=0). With the hunk reversed, the build
exited 0 (`logs/build-7-red.log`), and `ctest -R '^DataFileFormatTest$'` exited 8 with
the six failures listed above (`logs/test-7-red.log`, ends `EXIT=8`). The
`sampleTrackVolume( doc )` failures show actual 100 against expected 1.0, and the
`originalFileVersion()` failures show actual 0 against expected 17 or 16. The hunk was
then restored with `git apply` (`logs/evidence-2-restore.log`, EXIT=0). `git status
--porcelain` printed nothing directly after that restore, before the rebuild. The
rebuild exited 0 (`logs/build-8-green.log`), and the green run exited 0 with 1/1
passed (`logs/test-8-green.log`, ends `EXIT=0`). `git diff --stat HEAD~1^ HEAD --
src/core/DataFile.cpp` gave 14 insertions and 3 deletions.

(2) Determinism: `probeProject` (`tests/src/core/DataFileFormatTest.cpp:122-130`)
builds an in-memory `QByteArray` from its argument. The new tests call the
`DataFile(const QByteArray &, ...)` constructor (`src/core/DataFile.cpp:553`), so they
do no file I/O. `git diff tests/src/core/DataFileFormatTest.cpp | grep` for
`sleep|qWait|QTimer|Thread|usleep|msleep|QTest::` found no added lines (exit 1). The
only inputs are the injected `version` and `creatorversion` strings.

(3) Cause and sibling completeness: the two load entry points are
`DataFile(const QString &, ...)` (`src/core/DataFile.cpp:543` and `:547`) and
`DataFile(const QByteArray &, ...)` (`:562`), and all three call `loadData`
(defined at `:2764`). The `DataFile(Type)` constructor (`:449`) does not load a file.
`legacyFileVersion` is called only at `src/core/DataFile.cpp:2829` and `:2845`,
both in `loadData`; its declaration is `include/DataFile.h:141` and its definition
`src/core/DataFile.cpp:2910`. `grep -rn 'legacyFileVersion\|loadData(' src include`
exited 0. `upgrade()` at `:2638` is not in the diff (see Change). The absent or
`"1.0"` branch at `:2825-2830` is unchanged.

(4) Collateral:
- Version 31: `currentVersionSkipsUpgrades` passes in the green run (`logs/test-8-green.log`,
  DataFileFormatTest 1/1, which contains this function).
- Versions 40 and 75: `DataFileContainerRecoveryTest` (version 40) and `LanesUpconvertTest`
  (version 75) pass in `logs/test-4-family.log`.
- Absent or `"1.0"` legacy files: the branch is unchanged, and the existing
  `DataFileFormatTest` fixture at `tests/src/core/DataFileFormatTest.cpp:262` uses
  `version="1.0"`. All pre-existing `DataFileFormatTest` functions pass in the green
  run (`logs/test-8-green.log`).
- `version="1.2"`: now derives from creatorversion (`dottedVersionFallsBackToCreatorVersion`,
  green in `logs/test-8-green.log`). `StableTrackIdsTest` passes (`logs/test-4-family.log`).
- Saved bytes: `DataFileSaveIntegrityTest` passes (`logs/test-4-family.log`). I did not read its
  assertions, so I do not claim more than that it passes.

(5) ALREADY-FIXED / FALSE-POSITIVE item: not applicable. The verdict is FIXED, so
no already-fixed or false-positive contract is cited. The change is the parse-failure
branch at `src/core/DataFile.cpp:2831-2846`, which before this commit read
`toUInt()`'s 0 at `:2834`.

Verdict evidence lines:
- FIXED: red exit 8 with the product hunk reversed, six functions failing (`logs/test-7-red.log`).
- FIXED: restored tree green, exit 0, 1/1 (`logs/test-8-green.log`); family run 8/8 Passed (`logs/test-4-family.log`).
- FIXED: `git diff` shows only the one hunk in `src/core/DataFile.cpp`.
