# Cluster new-p9-347-port

Verdict: **FIXED**

## What this job was

A port job. The fix for this finding was accepted on 2026-10 as branch `bugs/qwen-new-p9-347`
(commit `3c7289ede`, "fix: resolve unmatched relative paths"), but it could not be merged into
`050/m0-train-5`: it broke `ControlProjectArchiveTest` there. This tree is a fresh branch off
the newest line, so the fix is re-applied by hand, with the train-5 break removed.

## The finding

`PathUtil::toAbsolute()` can return a relative path.

- Before the fix, `src/core/PathUtil.cpp:185` was
  `return baseLocation(base, error) + upgraded.remove(0, basePrefix(base).length());`
  `baseLocation(Base::Absolute)` returns `QString("")` (the `default:` branch of the switch at
  `src/core/PathUtil.cpp:65`), so a relative path that matched no base came back unchanged and
  was resolved later against the process working directory - against the contract
  "Make this path absolute" (`include/PathUtil.h:61-63`).
- The path is still relative on this branch: the same line 185 is in place, so the bug is
  **REAL here**, not already fixed.

## The port, and why the first version of it did not merge

The accepted fix was re-applied as it stands (`src/core/PathUtil.cpp:185-190`): the absolute
base's fallback is now resolved through `QFileInfo::absoluteFilePath()`, so the result is the
working-directory-absolute path the contract promises. Every other base keeps its own
resolution.

That version alone breaks the newest line, exactly as the merge train reported
(`logs/test-3.log`, exit 8):

| Failing case in `ControlProjectArchiveTest` | What it asserts |
| --- | --- |
| `anIntactProjectReportsNoMissingAssets` | a project whose every reference is on disk reports an EMPTY missing list (the negative control) |
| `eachUnresolvableReferenceIsReportedWithItsStoredValue` | a legacy relative reference resolves via `project-dir`, to `<project dir>/gone.wav` |
| `presentButUnhashedIsNotDigestIndistinguishableFromMissing` | the digest of an intact project differs from the digest of the same project with its media taken away |

The reason is the project-asset reader: it resolved a legacy relative reference (no prefix, in
no user/factory sample base) against the **project's own directory** by relying on
`toAbsolute()` handing such a path back still relative - documented in the file's own header
(`src/core/ControlProjectAssets.cpp:51-54` before this change) and in the code
   (`src/core/ControlProjectAssets.cpp:151-159`). With `toAbsolute()` now resolving that path
against the working directory, the reference was taken away from the project that carries it:
the file was reported missing although it sits next to the project.

So the port is two changes, and only the second one is new:

1. `src/core/PathUtil.cpp:185-190` - the ported fix: an unmatched relative path is resolved
   against the working directory, so `toAbsolute()` always returns an absolute path.
2. `src/core/ControlProjectAssets.cpp:129-141` - the project-relative rule is applied
   **explicitly**, before `toAbsolute()` is asked, instead of falling out of it: a relative
   path with no prefix that names no user/factory sample is resolved against the project's own
   directory, and its `resolved_via` stays `project-dir`. The reader's behaviour is therefore
   unchanged - it is the dependency on `toAbsolute()`'s relative return that is gone.

The relink path shares the reader (`fillReference` -> `resolveAssetPath`,
`src/core/ControlProjectAssetsRelink.cpp:118`), so scan and relink stay consistent.

## Regression test

`tests/src/core/RelativePathsTest.cpp:73-89` - `UnmatchedRelativePathsBecomeAbsolute()`:
it proves the name is in no special base (user sample, factory sample, VST), then checks that
`toAbsolute()` returns an absolute path equal to the working-directory-absolute path of the
same name, and that the `error` out-parameter stays false (the path was converted
successfully). No timing, no sleeps, no races: the expected value is computed by the same Qt
call the product uses, from the process working directory the test runs in.

The archive half needs no new test - `ControlProjectArchiveTest` already pins the
project-relative rule, and it is what failed when the ported fix was applied alone.

## Evidence

| Item | Command | Exit / result |
| --- | --- | ---: |
| RED (original code, new test) | `ctest -R '^RelativePathsTest$' --output-on-failure` | **8** - `FAIL! UnmatchedRelativePathsBecomeAbsolute() 'QFileInfo(absolute).isAbsolute()' returned FALSE.` |
| GREEN (ported fix) | `ctest -R '^RelativePathsTest$' --output-on-failure` | **0** - 1/1 test, 5 QtTest cases |
| Train-5 break reproduced (ported fix only) | `ctest -R '^ControlProjectArchiveTest$' --output-on-failure` | **8** - 17 passed, 3 failed |
| GREEN (port complete) | `ctest -R '^ControlProjectArchiveTest$' --output-on-failure` | **0** - 1/1 test, 20 QtTest cases |
| RED proof (product patch reversed) | `ctest -R '^(RelativePathsTest\|ControlProjectArchiveTest\|UndoHistoryPersistenceTest)$' --output-on-failure` | **8** - RelativePathsTest FAILS again, ControlProjectArchiveTest passes |
| GREEN proof (product patch restored) | `ctest -R '^(RelativePathsTest\|ControlProjectArchiveTest)$' --output-on-failure` | **0** - 2/2 tests |
| Wider sweep (path, project, sample, undo families) | `ctest -R '<23 test names>' --output-on-failure` | **8** - 22/23 passed, 1 pre-existing failure |
| Gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | **0** - RESULT: PASS (all 8 gate rows PASS) |

The one failure in the wider sweep is `UndoHistoryPersistenceTest`
(`aSavedHistoryUndoesToEveryPreEditState`: "the baseline needs a real instrument
(tripleoscillator)"). It is **pre-existing and unrelated**: the same assertion fails with the
product patch reversed (`logs/test-6.log`, exit 8, same message), because the tripleoscillator
plug-in is not built in this tree.

Affected family: 2 CTest tests - `RelativePathsTest` (5 QtTest cases) and
`ControlProjectArchiveTest` (20 QtTest cases) - plus a 23-test sweep over the path, project,
sample and undo families (22 pass, 1 pre-existing environment failure).

## Commands and exit codes

Build and test output went to new numbered files in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-347-port.r1/logs`.
Commands were run from the worktree root unless a working directory is stated.

### Build commands

| Log | Command | Exit |
| --- | --- | ---: |
| `build-1.log` | `cmake --build build --target RelativePathsTest` | 0 |
| `build-2.log` | `cmake --build build --target RelativePathsTest` | 0 |
| `build-3.log` | `cmake --build build --target ControlProjectArchiveTest` | 0 |
| `build-4.log` | `cmake --build build --target ControlProjectArchiveTest` | 0 |
| `build-5.log` | `cmake --build build --target zene` | 0 |
| `build-7.log` | `cmake --build build --target RelativePathsTest ControlProjectArchiveTest UndoHistoryPersistenceTest` | 0 |
| `build-8.log` | `cmake --build build --target RelativePathsTest ControlProjectArchiveTest zene` | 0 |
| `build-10.log` | `cmake --build build --target RelativePathsTest ControlProjectArchiveTest ControlBounceRenderTest ControlSampleOperatorTest ClipImportFileTest ProjectOpenIntegrityTest ProjectRecoveryTest DataFileFormatTest DataFileSaveIntegrityTest UndoHistoryPersistenceTest SessionCheckpointTest ClipSerialisationTest LibraryProjectsTest ProjectContainerSaveTest RecordingRecoveryMenuTest` | 0 |
| `build-11.log` | `cmake --build build --target EnvelopeAndLfoParametersTest SampleBufferResampleTest ClipConsolidateTest ControlBrowserCommandsTest ProjectContainerEntriesTest ProjectVersionTest UndoBoundsTest ReversibilityUndoTest` | 0 |
| `build-12.log` | `cmake --build build --target zene RelativePathsTest ControlProjectArchiveTest` (final, after the commit) | 0 |

(`build-6.log` and `build-9.log` are the same builds as `build-10.log`/`build-11.log` plus two
target names that do not exist in this tree - `AudioFileProcessor`, `ControlCollectMediaTest` -
so `make` stopped with "No rule to make target" and exited 2 after building every real
target. The clean runs are `build-10.log` and `build-11.log`, both exit 0.)

### CTest commands

Working directory for each command:
`/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-347-port/build/tests`.

| Log | Command | Result / exit |
| --- | --- | --- |
| `test-1.log` | `ctest -R '^RelativePathsTest$' --output-on-failure` | RED, exit **8** |
| `test-2.log` | `ctest -R '^RelativePathsTest$' --output-on-failure` | GREEN, 1/1, exit **0** |
| `test-3.log` | `ctest -R '^ControlProjectArchiveTest$' --output-on-failure` | the train-5 break, 3 cases failed, exit **8** |
| `test-4.log` | `ctest -R '^ControlProjectArchiveTest$' --output-on-failure` | GREEN, 1/1, exit **0** |
| `test-5.log` | `ctest -R '<15 test names>' --output-on-failure` | 14/15, 1 pre-existing failure, exit **8** |
| `test-6.log` | `ctest -R '^(RelativePathsTest\|ControlProjectArchiveTest\|UndoHistoryPersistenceTest)$' --output-on-failure` | RED proof (product patch reversed), exit **8** |
| `test-7.log` | `ctest -R '^(RelativePathsTest\|ControlProjectArchiveTest)$' --output-on-failure` | GREEN proof, 2/2, exit **0** |
| `test-8.log` | `ctest -R '<23 test names>' --output-on-failure` | 22/23, 1 pre-existing failure, exit **8** |
| `test-9.log` | the same 23 tests, final run after the commit | 22/23, 1 pre-existing failure, exit **8** |

### Regression proof (step 4b)

```
git diff -- src/core/PathUtil.cpp src/core/ControlProjectAssets.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-347-port.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-347-port.r1/logs/fix.patch
```
exit **0** (the product files only - the test file stays in place), then
`cmake --build build --target RelativePathsTest ControlProjectArchiveTest UndoHistoryPersistenceTest`
(`build-7.log`, exit 0) and `test-6.log`: exit **8**, `RelativePathsTest` FAILS again
(`'QFileInfo(absolute).isAbsolute()' returned FALSE`).

```
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-347-port.r1/logs/fix.patch
```
exit **0**, then `cmake --build build --target RelativePathsTest ControlProjectArchiveTest zene`
(`build-8.log`, exit 0) and `test-7.log`: exit **0**, 2/2 tests pass.

### Gates

| Log | Command | Exit | Note |
| --- | --- | ---: | --- |
| `gate-feature-check.log` | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | **0** | `RESULT: PASS`; `complexity` and `file-length` rows read "no new item in your files" |
| `gate-complexity.log` | `bash tests/complexity-gate.sh --check` | **1** | pre-existing train items only (e.g. `dotCentreAlong@tests/src/gui/XyPadPairTest.cpp`); none of them is a file this change touches |
| `gate-file-length.log` | `bash tests/file-length-gate.sh --check` | **1** | pre-existing train items only (e.g. `tests/src/core/RackMacrosTest.cpp`); none of them is a file this change touches |

Both ratchets were run **after** the final edit, with the change in place: they measured the
changed files and reported no new item in them. `PathUtil::toAbsolute` is at CCN 5 and
`projectassets::resolveAssetPath` at CCN 8 (target 10); `src/core/ControlProjectAssets.cpp` is
at 489 code lines of the 500 the ratchet allows.

## Files in the commit

- `src/core/PathUtil.cpp` - the ported fix: an unmatched relative path is resolved against the
  working directory (inherited upstream code; registered in `tests/upstream-modifications.txt`).
- `src/core/ControlProjectAssets.cpp` - the project-relative rule is applied explicitly, so
  the project-asset reader keeps resolving a legacy relative reference against the project's
  own directory (fork-NEW code; already in `tests/fork-sources.txt`).
- `tests/src/core/RelativePathsTest.cpp` - the regression.
- `tests/upstream-modifications.txt` - the divergence-ledger entries for the inherited file and
  the test.
- `docs/qwen-fixes-2026-10/cluster-new-p9-347-port.md` - this report.

No new file was added, so `tests/fork-sources.txt` is unchanged; `tests/all-sources.txt`
already names `tests/src/core/RelativePathsTest.cpp`.

## Self-check

1. **The regression test FAILS with the product change reverted and PASSES with it (exit codes).**
   RED: `ctest -R '^RelativePathsTest$' --output-on-failure` on the untouched tree, exit **8**
   (`test-1.log`). RED with the product patch reversed: the same family, exit **8**
   (`test-6.log`). GREEN: exit **0** (`test-2.log`, `test-7.log`). The test is the one the
   accepted fix shipped, plus an `isAbsolute()` assertion and the `error` out-parameter check.
2. **The test is deterministic.** No wall-clock timing, no sleeps, no races: the expected value
   is `QFileInfo(relativePath).absoluteFilePath()` - the same Qt call the product uses - and
   the three "not in any special base" checks are `QFileInfo::exists()` probes on names that
   this test never creates. The test's own name (`__zene_relativepaths_unmatched_recording_347.wav`)
   is not present in the user sample, factory sample or VST base, which the test asserts before
   it relies on them.
3. **The fix is at the cause and complete.** Every caller of `PathUtil::toAbsolute()` was
   checked: `DataFile::copyResources` (`src/core/DataFile.cpp:964`), `SampleBuffer::fromFile`
   (`src/core/SampleBuffer.cpp:78`), `SampleClip` (`src/core/SampleClip.cpp:817`),
   `SessionAudioCells` (`src/core/SessionAudioCells.cpp:183`), `RackZones`
   (`src/core/RackZones.cpp:84`), `ConfigManager` (`src/core/ConfigManager.cpp:622,627`), the
   file dialog and the file browser, and the plug-ins (Sf2Player, GigPlayer, Vestige, VstPlugin,
   Patman, SlicerT, AudioFileProcessor, TripleOscillator, LfoController,
   EnvelopeAndLfoParameters). All of them open the file, and a working-directory-relative name
   and its absolute twin name the same file, so they are unaffected. The one caller that
   depended on the *relative* return was the project-asset reader, and it is the second half of
   this change. The siblings inside `PathUtil` are consistent: `relativeOrAbsolute()` and
   `toShortestRelative()` both build on `toAbsolute()`, and `toShortestRelative()` now honours
   its own documented default ("Defaults to an absolute path if all bases fail") for a name that
   is under no base.
4. **No collateral damage.** The project-asset reader resolves a legacy relative reference
   exactly as before - same path (`<project dir>/name`), same `resolved_via` (`project-dir`),
   same `resolved` flag - which is what `ControlProjectArchiveTest` pins (three of its cases
   were the train-5 break and they are green again). Old projects load as before: a legacy
   relative reference still points at the same file, and the engine's own loading path
   (`DataFile::copyResources`, `SampleBuffer::fromFile`) opens the same file as before. The one
   visible consequence of the contract fix is that `toShortestRelative()` now returns the
   absolute working-directory path for a name that is under no base (it used to return the
   relative name, which only worked while the working directory stayed the same) - that is
   the contract it documents, and it concerns a name that is in no sample, VST or project base.
   No limit or check was added, so no caller can be hit by one.
5. **Realtime, undo and commands.** Nothing on the audio thread was touched:
   `PathUtil::toAbsolute()` is called when a file is loaded or a project is scanned, never per
   period; the change adds no allocation, no lock, no wait, no logging. No control command was
   added or changed, so the `ControlCommandsSnapshot` canary does not move. No undo step was
   added - the fix changes how a path is resolved, not what a command does.
6. **Qt 5.15 / MSVC / macOS portability.** The fix uses `QFileInfo::absoluteFilePath()`, which
   exists in every supported Qt; `QFileInfo::isAbsolute()` already treats a `:`-prefixed
   QResource path as absolute, so resource paths still return unchanged at
   `src/core/PathUtil.cpp:177-181`. The reader's companion change uses `QFileInfo`,
   `QDir::cleanPath` and `PathUtil::oldRelativeUpgrade` - all already used in that file. No
   `windows.h` macro names, no `std::filesystem`.
7. **The "ALREADY-FIXED or FALSE-POSITIVE" question does not apply here.** The verdict is
   FIXED: on this branch `src/core/PathUtil.cpp:185` was
   `return baseLocation(base, error) + upgraded.remove(0, basePrefix(base).length());` and
   `baseLocation(Base::Absolute)` returns `QString("")` (`src/core/PathUtil.cpp:65`), so the
   contract "make this path absolute" (`include/PathUtil.h:61-63`) was broken on this tree -
   proven by the RED run on the untouched code (`test-1.log`, exit 8).
