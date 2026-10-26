# Cluster new-p14-15: SMF export overwrite guard misses a dangling symlink

**Verdict: FIXED**

Finding BUG 4 (`src/core/ControlCommandsInterchange.cpp`, historical line 244) is REAL.

## Evidence

- **Before the fix**, the guard was `QFileInfo::exists(path) && !overwrite` (`src/core/ControlCommandsInterchange.cpp:244`
  before the change). `QFileInfo::exists` is false for a symlink whose target is gone, so the refusal was skipped.
- The export then goes through `interchange::writeConductorTrack` → `writeBytes` (`src/core/SmfInterchange.cpp:298-311`),
  which commits with `QSaveFile`. The commit replaces whatever occupies the path, so the dangling link was overwritten.
  The regression run below shows this: the export returned `ok` with the link in place.
- Sibling checks that already count a dangling link as occupied:
  - `src/core/ControlCommandsHandoff.cpp:125` (`isTaken`: `QFileInfo::exists(file) || QFileInfo(file).isSymLink()`)
  - `src/core/ControlCommandsLibraryClips.cpp:273` (`previous.existed = QFileInfo::exists(path) || QFileInfo(path).isSymLink()`)
- **Fix** (`src/core/ControlCommandsInterchange.cpp:244-245`): the refusal condition becomes
  `(QFileInfo::exists(path) || QFileInfo(path).isSymLink()) && !overwrite`, with a one-line comment. The refusal kind,
  message, `overwrite: true` behaviour and the write path are unchanged.

## Regression test

`tests/src/core/SmfInterchangeTest.cpp:345`, `aDanglingLinkAtTheExportPathIsOccupied()`, placed beside the existing
`refusalsAreTyped()` (`:256`). It is deterministic: it creates a dangling link with `QFile::link` to a target that is
never created, then checks state (`ok`, `errorKind`, the message, that the link is still a link, that the target was not
written). No timing, sleeps or races. It is Unix-only and `QSKIP`s on Windows, the same pattern as
`tests/src/core/BounceInPlaceTest.cpp`.

## Test matrix

| Path | Covered by |
| --- | --- |
| Dangling link, `overwrite` false | new test: refused with `Refused`, link kept, target not written |
| Regular existing file, `overwrite` false | existing `refusalsAreTyped` (passes before and after) |
| Regular existing file, `overwrite` true | existing `refusalsAreTyped` (unchanged) |
| Dangling link, `overwrite` true | not changed by the fix (the `||` only affects the `!overwrite` branch); not separately tested |
| Audio thread | N/A: control command handler, no audio-path code touched |
| Platform | Unix runs the test; Windows skips it (`QSKIP`), as `BounceInPlaceTest` does |

## Commands and exit codes

Builds used `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/ccache-bunny/tmp` because the default ccache temp directory
under the sandbox home returned "Permission denied" (`build-1.log`, EXIT=2). This sets an environment variable only and
changes no config.

| Step | Command (run from the worktree unless noted) | EXIT | Log (run folder `logs/`) |
| --- | --- | --- | --- |
| Build, first try (ccache failure, not a code error) | `cmake --build build --target SmfInterchangeTest -j4` | 2 | build-1.log |
| Build before fix (test added, product unchanged) | `CCACHE_TEMPDIR=... cmake --build build --target SmfInterchangeTest -j4` | 0 | build-2.log |
| **RED** (before fix) | `ctest -R '^SmfInterchangeTest$' --output-on-failure` in `build/tests` | **8** | test-1-red.log |
| Build after fix | `CCACHE_TEMPDIR=... cmake --build build --target zene SmfInterchangeTest -j4` | 0 | build-3.log |
| **GREEN** (after fix) | `ctest -R '^SmfInterchangeTest$' --output-on-failure` in `build/tests` | **0** | test-2-green.log |
| 4b: save product diff | `git diff -- src/core/ControlCommandsInterchange.cpp > logs/fix.patch` | 0 | fix.patch |
| 4b: revert fix | `git apply -R logs/fix.patch` | 0 | |
| 4b: rebuild unfixed | `CCACHE_TEMPDIR=... cmake --build build --target zene SmfInterchangeTest -j4` | 0 | build-4-unfixed.log |
| 4b: **RED** (fix reverted) | `ctest -R '^SmfInterchangeTest$' --output-on-failure` in `build/tests` | **8** | test-3-unfixed-red.log |
| 4b: reapply fix | `git apply logs/fix.patch` | 0 | |
| 4b: rebuild fixed | `CCACHE_TEMPDIR=... cmake --build build --target zene SmfInterchangeTest SmfInterchangeRoundTripTest -j4` | 0 | build-5-restored.log |
| 4b: **GREEN** (fix restored, family) | `ctest -R SmfInterchange -V` in `build/tests` | **0** | test-4-family-green.log |

RED assertion, both runs (`test-1-red.log`, `test-3-unfixed-red.log`):

    FAIL!  : SmfInterchangeTest::aDanglingLinkAtTheExportPathIsOccupied() '!exported.ok' returned FALSE. (an export replaced a dangling link without overwrite)
    Totals: 12 passed, 1 failed, 0 skipped

GREEN (`test-4-family-green.log`):

    336: Totals: 13 passed, 0 failed, 0 skipped   (SmfInterchangeTest, incl. the new case)
    337: Totals: 18 passed, 0 failed, 0 skipped   (SmfInterchangeRoundTripTest)
    100% tests passed, 0 tests failed out of 2

Affected family (`ctest -R SmfInterchange`): 2 ctest tests, 31 QtTest cases, all passing with the fix.

## Gates

**The `feature_check.py` wrapper cannot run in this sandbox.** It exits 1 with `PermissionError` at `logs.mkdir`, because it
writes its logs to `<worktree>-feature-check`, a sibling of the worktree, which is outside the writable area
(`logs/gates-1.log`). This is an environment limit, not a product failure. I did not edit the gate.

I ran the same commands the wrapper runs (its `SIMPLE` and `RATCHET` lists), in the worktree, each logged:

| Gate | Command | EXIT | Note |
| --- | --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 | |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 | |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 | |
| check-namespace | `tests/scripted/check-namespace` | 0 | |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 | |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 | |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 | 7 REGRESSION lines, all in other files (`WasmSandbox.cpp`, `XyPadPairTest.cpp`, `DawProjectReadTracks.cpp`, `ControlCommandsMixerTools.cpp`, `ControlCommandsSessionRecordLand.cpp`, `ControlCommandsMixerStrip.cpp`, `ControlCommandsShortcuts.cpp`). None in the two changed files. |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 | 19 REGRESSION lines, all in other files. None in the two changed files. |

The ratchets exit 1 on items in files this branch did not touch. The wrapper only fails on items in changed files, and
none of those appear (`grep` for `ControlCommandsInterchange` and `SmfInterchangeTest` in both logs finds nothing), so
under the wrapper's rule both ratchets pass.

## Self-check

1. **Regression test fails with the change reverted and passes with it:** yes. RED exit 8 (`test-1-red.log`, and again after the 4b revert in `test-3-unfixed-red.log`); GREEN exit 0 (`test-2-green.log`, `test-4-family-green.log`).
2. **Acceptance criteria proven by a test run here:** the one criterion (a dangling link at the export path without `overwrite` is refused and left intact) is `aDanglingLinkAtTheExportPathIsOccupied`, GREEN exit 0 in `test-4-family-green.log` (13 passed in `SmfInterchangeTest`).
3. **Deterministic:** yes. Fixture is a `QFile::link` to a never-created target. No timing, sleeps or races.
4. **Fix at the cause and complete (callers and siblings):**
   - `writeConductorTrack` has one caller, `src/core/ControlCommandsInterchange.cpp:253`. The guard at `:245` is the only overwrite check on the SMF export path.
   - The SMF read/import paths (`ControlCommandsInterchange.cpp:327`, `:403`) use `QFileInfo::exists` in `missingFile`. They read and write nothing, so a dangling link is correctly "missing". They are unchanged.
   - Other `overwrite` guards in the tree use the same `QFileInfo::exists`-only check: `src/core/ControlCommandsDawProject.cpp:372`, `src/core/ControlDeviceState.cpp:237` (`controlWriteFileBytes`), `src/core/ControlCommandsChain.cpp:152`, `src/core/ControlCommandsExportPresets.cpp:139`, `src/core/ControlCommandsChainEdit.cpp:231`. They are separate write paths, outside this finding, and I did not verify whether each one replaces a dangling link. **Not changed here.** Each needs its own finding if confirmed.
5. **No collateral damage:** `overwrite: true` and the regular-file refusal are unchanged (`refusalsAreTyped` passes). No file format, no saved data, no command registration changed, so old projects and saved data are unaffected.
6. **Realtime, undo, commands:** the handler runs on the control path. Nothing on the audio thread. No undo entry (the export writes a file, not the project). No new control command. The control-command snapshot is not affected.
7. **Registration:** no new files were added, so nothing needed registering in `tests/fork-sources.txt` or `tests/upstream-modifications.txt`.
8. **Gates:** the wrapper could not run (see Gates). The six simple gates exit 0, and both ratchets show no item in the changed files, so both pass under the wrapper's rule. Raw ratchet exit codes are 1 on other files.
9. **Completion check (`submit_check.py`):** see the result below.
