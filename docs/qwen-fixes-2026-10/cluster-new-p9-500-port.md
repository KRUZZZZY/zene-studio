# Cluster new-p9-500-port: version numbers silently wrap or reset

**Verdict: FIXED** (port of bugs/qwen-new-p9-500 onto 050/m0-train-5; written by hand, not merged).

Caveat: both ratchet gates exit 1 on this branch, but they fail identically on a clean `HEAD` export
(see the gate section). This change adds no gate entries.

## The defect (real on this tree, before the fix)

`src/gui/modals/VersionedSaveDialog.cpp` (committed `HEAD`, lines 114-117) parsed the version into a
`ushort` and asserted success:

- `Mix-65535.wav` + increment: `65535 + 1` stored into `ushort` wraps to `0`, so the name becomes `Mix-00.wav`.
- `Mix-65536.wav` + increment: `toUShort` fails; `Q_ASSERT` compiles out in this build, so the value is `0`
  and the name becomes `Mix-01.wav`.

## The fix

`src/gui/modals/VersionedSaveDialog.cpp`:

- `:34` adds `#include <limits>`.
- `:45-51` adds a file-local `static bool parseIncrementableVersion(...)` that parses with `toULongLong`
  and returns false for an unparseable number, or for an increment at `numeric_limits<qulonglong>::max()`.
  It is a helper rather than an inline branch because an inline check took `changeFileNameVersion` to CCN 11
  (above the target of 10); with the helper it is CCN 9 (measured with lizard; the helper is CCN 3).
- `:126-127` `changeFileNameVersion` uses `qulonglong version` and returns false before the name is touched.
  The `Q_ASSERT` is gone; the normal increment/decrement path is unchanged.

No header changed; the public signature `changeFileNameVersion( QString &, bool )` is the same.

## Regression tests

`tests/src/gui/VersionedSaveDialogTest.cpp` (already registered in `tests/CMakeLists.txt` and
`tests/all-sources.txt`, so no new file was registered). Four slots added beside the existing
`aDottedParentDoesNotAffectExtensionlessVersionInsertion`, which is unchanged:

- `:42` `incrementsPastUshortMaximum` (`Mix-65535.wav` -> `Mix-65536.wav`)
- `:49` `incrementsPastPreviouslyUnparseableValue` (`Mix-65536.wav` -> `Mix-65537.wav`)
- `:56` `refusesIncrementOverflowWithoutChangingName` (`Mix-18446744073709551615.wav` refused, name unchanged)
- `:63` `refusesUnparseableNumberWithoutChangingName` (`Mix-18446744073709551616.wav` refused, name unchanged)

## Manifest

`tests/upstream-modifications.txt` gains one reason line for `src/gui/modals/VersionedSaveDialog.cpp`
(the file already had entries from earlier clusters; duplicate paths are normal in that file).

## RED / GREEN evidence

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-500-port.r1/logs`

| Step | Exact command | Exit | Result |
|---|---|---:|---|
| Build test, first attempt | `cmake --build build --target VersionedSaveDialogTest -j2 > .../build-1-red.log 2>&1` | 2 | Failed in ccache (`failed to create temporary file ... /run/user/1000/ccache-tmp ... Permission denied`); no code error. Infrastructure, not a test result. |
| Build test with ccache temp in /tmp | `CCACHE_TEMPDIR=/tmp/ccache-tmp-new-p9-500 cmake --build build --target VersionedSaveDialogTest -j2 > .../build-2-red.log 2>&1` | 0 | Built on the **unfixed** product code. |
| **RED** (unfixed code, test as written) | `cd build/tests && ctest -R '^VersionedSaveDialogTest$' --output-on-failure > .../test-1-red.log 2>&1` | **8** | `0% tests passed, 1 tests failed`. Four assertions fail: `Actual "Mix-00.wav"` (expected `Mix-65536.wav`); `Actual "Mix-01.wav"` (expected `Mix-65537.wav`); `refusesIncrementOverflow...` and `refusesUnparseable...` fail because `changeFileNameVersion` returned true (the name was changed instead of refused). |
| GREEN (fix applied) | `cd build/tests && ctest -R '^VersionedSaveDialogTest$' -V > .../test-9-final-green.log 2>&1` | **0** | `100% tests passed out of 1`; QTest `Totals: 7 passed, 0 failed` (`initTestCase`, the existing dotted-parent slot, the four new slots, `cleanupTestCase`). |
| Prove test depends on fix: revert | `git diff -- src/gui/modals/VersionedSaveDialog.cpp > .../fix.patch && git apply -R .../fix.patch` | 0 | Product fix reverted. |
| Rebuild on reverted code | `cmake --build build --target VersionedSaveDialogTest -j2 > .../build-9-revert.log 2>&1` | 0 | |
| RED with revert | `cd build/tests && ctest -R '^VersionedSaveDialogTest$' -V > .../test-8-final-red.log 2>&1` | **8** | QTest `Totals: 3 passed, 4 failed`; the same four new slots fail. |
| Re-apply fix | `git apply .../fix.patch` | 0 | |
| Rebuild with fix | `cmake --build build --target VersionedSaveDialogTest -j2 > .../build-10-reapply.log 2>&1` | 0 | |
| GREEN with fix | `cd build/tests && ctest -R '^VersionedSaveDialogTest$' -V > .../test-9-final-green.log 2>&1` | **0** | `Totals: 7 passed, 0 failed`. |

The earlier RED on the unfixed tree (`test-1-red.log`, exit 8) was taken before the helper refactor; the
revert pair above (`test-8-final-red.log`, exit 8) repeats it on the final diff.

## Affected family

| Command | Exit | Count |
|---|---:|---|
| `cmake --build build --target ProjectContainerSaveAsGuiTest -j2 > .../build-7-family.log 2>&1` then `cd build/tests && ctest -R '^ProjectContainerSaveAsGuiTest$' -V > .../test-6-family.log 2>&1` | 0 | `Totals: 6 passed, 0 failed`. (An earlier ctest run before this build failed with `Could not find executable`; the binary had not been built in this tree.) |
| `cd build/tests && ctest -R '^(VersionedSaveDialogTest\|ProjectContainerSaveAsGuiTest)$' -V > .../test-7-helper.log 2>&1` | 0 | 2/2 ctests; 7 + 6 QTest entries passed. |

Build of the product: `cmake --build build --target zene -j2 > .../build-11-zene-final.log 2>&1` -> exit **0**.

## Ratchet gates

| Command | Exit on this branch | Exit on clean `HEAD` export |
|---|---:|---:|
| `bash tests/complexity-gate.sh --check > .../gate-complexity-final.log 2>&1` | **1** | **1** |
| `bash tests/file-length-gate.sh --check > .../gate-file-length-final.log 2>&1` | **1** | **1** |

Both gates fail with the **same** REGRESSION lines on the branch and on a clean `HEAD` export
(`git archive HEAD` extracted to `/tmp/head-gate-check`; `gate-complexity-HEAD-baseline.log`,
`gate-file-length-HEAD-baseline.log`). The complexity regressions are in unrelated functions
(`XyPadPairTest.cpp`, `DawProjectReadTracks.cpp`, `ControlCommandsMixerTools.cpp`, `ControlCommandsMixerStrip.cpp`,
`ProjectRevisions.cpp`, `ControlCommandsShortcuts.cpp`); the file-length regressions are unrelated files
(e.g. `src/core/TakeLane.cpp`, `tests/src/core/DeviceDspTest.cpp`). None of the reported entries is this
change's file or function. The all-scope complexity check (`GATE_SCOPE=all`, `gate-complexity-all-final.log`)
also exits 1 without naming `changeFileNameVersion` or the helper.

I did **not** fix those unrelated gate entries and did not re-anchor or change any limit: they belong to other
clusters and are not part of this bug. The owner should decide whether they are cleared on this line.

## Commands and results at a glance

- Verdict-bearing tests: `VersionedSaveDialogTest` GREEN 7/7, `ProjectContainerSaveAsGuiTest` 6/6.
- Gates: both exit 1, identical to `HEAD`; this change introduces no new entries.
- Not run: the full `ctest`, `run-all-gates.sh`, and the other gates.
