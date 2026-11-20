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
| p5-green-now | `cmake --build build --target DataFileFormatTest && ctest -R "^DataFileFormatTest$"` on the fixed tree | 0 (1/1 passed) | `duet/12-worker/tmp/001-p5-green-now.log` |
| p5-red-hunk-reversed | same, with `src/core/DataFile.cpp` replaced by `git show HEAD:src/core/DataFile.cpp` | 8 (6 failed of 17 test functions, see below) | `duet/12-worker/tmp/002-p5-red-hunk-reversed.log` |
| p5-green-restored-family | after `cp` restored the fixed file (`cmp` identical), `cmake --build` of four targets and `ctest -R "DataFile\|StableTrackIds"` | 0 (4/4 passed) | `duet/12-worker/tmp/003-p5-green-restored-family.log` |

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

(1) Red/green with the product hunk reversed and restored: the green run before
the reversal exited 0 (`p5-green-now`, log 001). With the hunk reversed, the same
build and ctest exited 8, with the six failures listed above (`p5-red-hunk-reversed`,
log 002). The fixed file was then copied back, and `cmp` against the saved copy
reported identical. `git diff --stat src/core/DataFile.cpp` gave 14 insertions and
3 deletions, with exit 0. The restored green run exited 0 with 4/4 tests passed
(`p5-green-restored-family`, log 003).

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
- Version 31: `currentVersionSkipsUpgrades` passes in the green runs (log 001 and
  log 003). Versions 40 and 75 are not exercised by any test in this change, so
  they are unverified here.
- Absent or `"1.0"` legacy files: the branch is unchanged, and the existing
  `DataFileFormatTest` fixture at `tests/src/core/DataFileFormatTest.cpp:262` uses
  `version="1.0"`. All pre-existing `DataFileFormatTest` functions pass in the green
  run (log 001).
- `version="1.2"`: now derives from creatorversion (`dottedVersionFallsBackToCreatorVersion`,
  green in log 001). `StableTrackIdsTest` passes (log 003, 4/4 with `DataFile|StableTrackIds`).
- Saved bytes: `DataFileSaveIntegrityTest` passes (log 003). I did not read its
  assertions, so I do not claim more than that it passes.

(5) ALREADY-FIXED / FALSE-POSITIVE item: not applicable. The verdict is FIXED, so
no already-fixed or false-positive contract is cited. The change is the parse-failure
branch at `src/core/DataFile.cpp:2831-2846`, which before this commit read
`toUInt()`'s 0 at `:2834`.

Verdict evidence lines:
- FIXED: green exit 0 before reversal (log 001).
- FIXED: red exit 8 with the product hunk reversed, six functions failing (log 002).
- FIXED: restored tree green, exit 0, 4/4 (log 003).
- FIXED: `git diff` shows only the one hunk in `src/core/DataFile.cpp`.
