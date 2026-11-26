FIXED

new-p16-175: FIXED - project identity now compares paths under the file system's case rule, so a recovery of the same file is offered even when the launch spells its path with a different case.

## Verdict: REAL on this branch, before the fix

- `src/core/ProjectRecovery.cpp:48-54` (before the fix, `git show HEAD:src/core/ProjectRecovery.cpp`): `comparablePath()` returned `QFileInfo(path).absoluteFilePath()`, which makes a path absolute and cleaned but keeps its case exactly as given. The finding's line 51-55 is this function.
- `src/core/ProjectRecovery.cpp:106-110` (before): `namesAnotherProject()` returned `!wanted.isEmpty() && wanted != comparablePath(recovery.sourceProject)`. `QString::operator!=` is `Qt::CaseSensitive`, so two spellings of one file were two projects. The finding's line 106-109 is this comparison.
- What that costs: `src/core/main.cpp:1700-1703` asks `ProjectRecovery::decideRecovery(recovery, fileToLoad)` and a verdict other than `Offer` means no prompt; `src/gui/MainWindow.cpp:1890-1894` keeps writing `recover.mmp` and its sidecar for the new session (`autoSave()`), so the crashed session's unsaved work is overwritten without ever having been offered.
- Reproduced in this session, unfixed code (`test-1-red.log`, ctest EXIT=8): `FAIL! : ProjectRecoveryTest::aRecoveryOfTheSameProjectIsOfferedWhenOnlyThePathCaseDiffers(file name) 'd.offered()' returned FALSE. (OtherProject: recovery belongs to 'Track.mmp', not to '/home/u/songs/track.mmp')`.
- The sibling with the same rule: `src/core/RevisionTimeline.cpp:228` (before) compared `QFileInfo(from).absoluteFilePath() == QFileInfo(projectPath).absoluteFilePath()`, under a comment that calls it "the same rule ProjectRecovery::decideRecovery applies". Reproduced: `FAIL! : RevisionTimelineTest::anAutosaveOfThisProjectIsFoundWhateverTheCaseOfItsPath() '!autosave.isEmpty()' returned FALSE. (an autosave of THIS project was hidden because its path was spelled with another case)`.
- The contract it breaks is the header's own (`include/ProjectRecovery.h:141-143`, before the fix): "A recovery file is offered only when it is this session's: a recovery naming a DIFFERENT project is never offered as the recovery of the project being opened". On Windows `C:\Music\Song.mmp` and `c:\music\song.mmp` are one file, so the second spelling names the SAME project and refusing it is a false "different project". The fix states the case rule in that paragraph (`include/ProjectRecovery.h:175-181`, after) rather than changing what the decision guarantees.

Not ALREADY-FIXED: there was no case handling anywhere in the identity path before this commit (`comparablePath`, `namesAnotherProject`, and the RevisionTimeline copy all compared raw strings). Not FALSE-POSITIVE: the RED runs above print the wrong verdict from the product code.

## The fix

One rule, decided by the file system, used by both places that ask "same project?".

- `include/ProjectRecovery.h:133-143`: `enum class PathCase { Sensitive, Insensitive }`.
- `include/ProjectRecovery.h:145-156`: `PathCase platformPathCase()` - the rule this file system compares names with. Windows file systems (NTFS/FAT/exFAT) compare names case-insensitively, POSIX ones do not; `ZENE_PATH_CASE=sensitive|insensitive` names the rule for a mount where the platform default is wrong (a case-insensitive FAT/CIFS share on Linux, a macOS volume, a case-sensitive NTFS directory). Any other value is ignored, and it is read per decision, not cached.
- `include/ProjectRecovery.h:158-165`: `bool sameProjectPath(left, right, PathCase)` - both paths made absolute the way the decision already did it (symlinks deliberately NOT resolved), then compared under the rule; false when either is empty, which is the old "no filename, no identity" reading.
- `src/core/ProjectRecovery.cpp:61-67`: `sameUnderRule()` - `QString::compare(..., Qt::CaseInsensitive)` or `==`. `src/core/ProjectRecovery.cpp:118-126`: `namesAnotherProject()` takes the rule instead of assuming one. `src/core/ProjectRecovery.cpp:275-298`: `platformPathCase()`, with the Windows branch under `#ifdef Q_OS_WIN` and the POSIX branch documented as deliberately case-sensitive (folding there would offer ANOTHER project's unsaved work - the one thing the identity check exists to prevent). `src/core/ProjectRecovery.cpp:300-307`: `sameProjectPath()`. `src/core/ProjectRecovery.cpp:335`: the decision passes `platformPathCase()`.
- `src/core/RevisionTimeline.cpp:225-231`: the sibling now calls `ProjectRecovery::sameProjectPath(from, projectPath, ProjectRecovery::platformPathCase())` instead of its own string comparison, so the two cannot drift apart again.

Callers and siblings checked:

- `decideRecovery` has one product caller, `src/core/main.cpp:1703`; its arguments and verdicts are unchanged, only the comparison changed.
- The other reader of the identity sidecar is `RevisionTimeline.cpp:213` (`applyAutosaveIdentity`, one caller: `findRevision` at `src/core/RevisionTimeline.cpp:296`), fixed by the same helper. That covers `listProjectRevisions`, `findRevision`, `revisionTimelineState` and `revisions.list`/`revisions.restore`.
- `readRecoveryInfo()`'s freshness half (`recoveryIsOlderThanTheProject`, `stampsCannotOrderTheWrites`) is untouched: this bug is identity, not staleness.
- `src/core/ControlCommandsDocuments.cpp:277-280` has a third path comparison (`sameFile`, used to hide the session's own file from `recentOthers()`). It is not this rule and not this finding; see Other findings.
- No command id, no snapshot, no undo step, no audio-thread code is touched: `decideRecovery` runs at startup (`main.cpp`) and `applyAutosaveIdentity` on the UI/control thread, both before any audio period.

## Regression tests

`tests/src/core/ProjectRecoveryTest.cpp` (existing file for the area):

- `aRecoveryOfTheSameProjectIsOfferedWhenOnlyThePathCaseDiffers` (`:284`, its three data rows at `:269`: file name, directory, Windows drive letter + whole path). The file system's case rule is injected (`ScopedPathCaseRule`, `:88`, over `ZENE_PATH_CASE`), because this host's file system is case-sensitive and cannot show the Windows rule. RED without the fix, GREEN with it.
- `readingRealFilesOffersARecoveryWhoseProjectPathCaseChanged` (`:332`): the same case through the real sidecar (`writeRecoveryIdentity` + `readRecoveryInfo`) and a `QTemporaryDir`.
- `anotherProjectStaysAnotherProjectWhateverTheCase` (`:301`): under the same rule a genuinely different project is still refused. Control: passes before and after.
- `thePlatformCaseRuleDecidesWhenNoOverrideIsSet` (`:315`): with no override the platform rule decides (Windows: offered; POSIX: `OtherProject`). This is also the reconfiguration check - it runs after the two slots that set the override, so a leaked override would fail it, and the RAII guard restores the previous environment either way.

`tests/src/core/RevisionTimelineTest.cpp` + `tests/src/core/RevisionTimelineTestParts3.h` (the sibling's family):

- `anAutosaveOfThisProjectIsFoundWhateverTheCaseOfItsPath` (body `RevisionTimelineTestParts3.h:125`, declaration `RevisionTimelineTest.cpp:538`, helper `RevisionTimelineTest.cpp:119`): a real autosave with a real `.info` sidecar whose `project=` line is the same path in another case; the timeline must list the autosave as this project's revision. RED without the fix, GREEN with it.

Test matrix recorded before editing:

| Path | Where covered | Result |
| --- | --- | --- |
| Normal: same project, same spelling | `freshRecoveryOfTheProjectBeingOpenedIsOffered`, `readingRealFilesFindsTheSidecarAndTheProject` (existing) | pass, unchanged |
| Normal: same project, different case | the three new data rows + the real-file case | RED 8 -> GREEN 0 |
| Failure: another project, any case | `aRecoveryOfAnotherProjectIsNotOfferedAsRecoveryOfThisOne` (existing), `anotherProjectStaysAnotherProjectWhateverTheCase` (new control) | pass |
| Failure: stale recovery, empty recovery, missing recovery, no sidecar | existing slots `aRecoveryOlderThanTheProjectIsNotOffered`, `anEmptyRecoveryFileIsNotOffered`, `aMissingRecoveryFileIsNotOffered`, `aRecoveryFileWithNoSidecarIsReadAndOffered`, `anOversizedIdentitySidecarIsReadAsAbsent` | pass, unchanged |
| Exhaustion / limits | not applicable: this fix adds no size, count or rate bound (the one bound, `MaxRecoveryIdentityBytes`, is untouched) | - |
| Reconfiguration: rule set, cleared, unknown | `ScopedPathCaseRule` scopes plus `thePlatformCaseRuleDecidesWhenNoOverrideIsSet`; an unknown value falls through to the platform rule (`platformPathCase`, `ProjectRecovery.cpp:277-297`) | pass |
| Audio thread | no code on the audio path is touched; the decision runs at startup and the timeline on the UI thread | reasoned, not measured (nothing to measure) |
| Platforms | Windows branch is under `#ifdef Q_OS_WIN`, not built on this Linux host; syntax-checked against the real Qt headers (log `win-branch-syntax.log`, EXIT 0, `#ifdef Q_OS_WIN` -> `#if 1` in a scratch copy). API used: `QString::compare`, `Qt::CaseInsensitive`, `qEnvironmentVariable`, `QFileInfo::absoluteFilePath` - all present in Qt 5.15 and Qt 6 | syntax check EXIT 0 |

## Commands and EXIT codes

`L` = `/home/kruzzzzy/.config/kads/zene/state/runs/new-p16-175.r1/logs`. Builds were run through the shared build guard; the guard made two of them wait, which is why they were started as background processes and their `EXIT=` read from the process output.

| Step | Command (worktree root unless said otherwise) | EXIT |
| --- | --- | --- |
| RED build (unfixed product) | `cmake --build build --target ProjectRecoveryTest RevisionTimelineTest -j4 > $L/build-1-red.log 2>&1` | 0 |
| RED run | from `build/tests`: `ctest -R '^(ProjectRecoveryTest\|RevisionTimelineTest)$' --output-on-failure > $L/test-1-red.log 2>&1` | **8** - 2/2 families failed, only the new assertions (see Verdict) |
| GREEN build (header changed, so `zene` too) | `cmake --build build --target zene ProjectRecoveryTest RevisionTimelineTest -j4 > $L/build-2-green.log 2>&1` | 0 |
| GREEN run | `ctest -R '^(ProjectRecoveryTest\|RevisionTimelineTest)$' --output-on-failure > $L/test-2-green.log 2>&1` | 0 - 2/2 passed |
| GREEN case count | `ctest -R '^ProjectRecoveryTest$' -V > $L/test-3-green-verbose.log 2>&1` | 0 - `Totals: 36 passed, 0 failed`; the three new data rows plus the two new slots PASS |
| Family build | `cmake --build build --target DataFileContainerRecoveryTest ProjectRevisionRestoreStagingTest RevisionRestoreCreatedFileUndoTest RecordingRecoveryMenuTest RevisionsDocsTest AudioAlsaResumeRecoveryTest -j4 > $L/build-3-family.log 2>&1` | 0 |
| Family run | `ctest -R 'Recovery\|Revision' --output-on-failure > $L/test-5-family-green.log 2>&1` | 0 - 9/9 passed (`ProjectRecoveryTest`, `RevisionTimelineTest`, `DataFileContainerRecoveryTest`, `ProjectRevisionRestoreStagingTest`, `RevisionRestoreCreatedFileUndoTest`, `RecordingRecoveryMenuTest`, `RevisionsDocsTest`, `AudioAlsaResumeRecoveryTest`, `ControlRecordingRecovery`). The earlier `test-4-family.log` (EXIT=8) is the same run before those six binaries were built - they were reported "Not Run (Unable to find executable)", not failed |
| 4b: save the product diff | `git diff -- include/ProjectRecovery.h src/core/ProjectRecovery.cpp src/core/RevisionTimeline.cpp > $L/fix.patch` | DIFF_EXIT=0 (182 lines) |
| 4b: revert it | `git apply -R $L/fix.patch` | REVERT_EXIT=0 |
| 4b: rebuild reverted | `cmake --build build --target zene ProjectRecoveryTest RevisionTimelineTest -j4 > $L/build-4-reverted.log 2>&1` | 0 (the test files still compile without the fix: they use no new symbol) |
| **4b: reverted run** | `ctest -R '^(ProjectRecoveryTest\|RevisionTimelineTest)$' --output-on-failure > $L/test-6-4b-red.log 2>&1` | **8** - the four new ProjectRecoveryTest assertions plus the RevisionTimelineTest one fail, same text as the RED run |
| 4b: re-apply | `git apply $L/fix.patch` | APPLY_EXIT=0 |
| 4b: rebuild re-applied | `cmake --build build --target zene ProjectRecoveryTest RevisionTimelineTest -j4 > $L/build-5-reapplied.log 2>&1` | 0 |
| **4b: re-applied run** | `ctest -R '^(ProjectRecoveryTest\|RevisionTimelineTest)$' --output-on-failure > $L/test-7-4b-green.log 2>&1` | 0 - 2/2 passed |
| Gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > $L/feature-check-1.log 2>&1` | 0 - all rows PASS (`fork-sources`, `all-sources-reproduce`, `no-upstream-regression`, `check-namespace`, `no-tautology`, `doc-figures`, `complexity` "no new item in your files", `file-length` "no new item in your files") |
| Windows branch syntax | scratch copy of `src/core/ProjectRecovery.cpp` with `#ifdef Q_OS_WIN` -> `#if 1`, compiled with the flags from `build/compile_commands.json` plus `-fsyntax-only` (`$L/win-branch-syntax.log`) | EXIT 0 (scratch copy deleted) |
| Windows-only syntax attempt with `-DQ_OS_WIN` | same flags plus `-DQ_OS_WIN` (`$L/win-syntax.log`) | EXIT 1 - not a code error: forcing `Q_OS_WIN` on this Linux Qt 6 makes Qt's own headers take Windows paths (`qmetatype.h`, `qstring.h` "incomplete type"). Reported, not used as evidence |

Registration: no file is new. `include/ProjectRecovery.h`, `src/core/ProjectRecovery.cpp`, `src/core/RevisionTimeline.cpp` and the test files are already fork files (`tests/fork-sources.txt:1326` lists the header). `fork-sources` and `no-upstream-regression` both exit 0 above.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it (exit codes).** Reverted: `test-6-4b-red.log`, `ctest -R '^(ProjectRecoveryTest|RevisionTimelineTest)$'` EXIT=**8**, `FAIL!` on `aRecoveryOfTheSameProjectIsOfferedWhenOnlyThePathCaseDiffers` (all three rows), `readingRealFilesOffersARecoveryWhoseProjectPathCaseChanged` and `anAutosaveOfThisProjectIsFoundWhateverTheCaseOfItsPath`. Restored: `test-7-4b-green.log` EXIT=**0**, 2/2. The reverted build itself exited 0, so the RED is a test failure, not a compile failure.
- **The test is deterministic: no wall-clock timing, sleeps or races.** No clock, sleep, thread or real "now" is consulted. The identity decision is a pure function of hand-built `RecoveryInfo` values with fixed timestamps (`at(11, 12)` / `at(11, 11)`), plus one case that reads a real sidecar and files stamped with `setFileTime()` in a `QTemporaryDir`. The seam is `ZENE_PATH_CASE` through `ScopedPathCaseRule`, which names the file system's case rule for one scope and restores the previous environment in its destructor; the case-sensitive default is asserted separately in `thePlatformCaseRuleDecidesWhenNoOverrideIsSet`.
- **The fix is at the cause and complete: every caller and sibling checked and consistent.** The comparison lives once, in `sameProjectPath`/`sameUnderRule` (`src/core/ProjectRecovery.cpp:61-67`, `:300-307`); both places that ask "same project?" use it - `decideRecovery` (`:335`, called from `src/core/main.cpp:1703`) and `applyAutosaveIdentity` (`src/core/RevisionTimeline.cpp:231`, called from `findRevision` at `:296`). Staleness, sidecar parsing and the identity writer are untouched, so the two halves of the decision still agree. No command, no snapshot, no undo step, no audio-thread code.
- **No collateral damage.** The sidecar format, the writer, the bound (`MaxRecoveryIdentityBytes`) and every verdict except the case-folded identity are unchanged; old projects and old recoveries (including upstream ones with no sidecar) load exactly as before, which the untouched existing slots prove in `test-3-green-verbose.log` (36 passed). On a case-sensitive file system with no override the behaviour is exactly the old one - `thePlatformCaseRuleDecidesWhenNoOverrideIsSet` asserts it, and the full `Recovery|Revision` family passes (9/9). Nothing here adds a limit that another caller could exceed.
- **ALREADY-FIXED / FALSE-POSITIVE contract item.** Not applicable: the verdict is FIXED, and the contract it enforces is quoted from `include/ProjectRecovery.h:141-143` (before the fix; restated at `:175-181` after it) above.

## Other findings

- `src/core/ControlCommandsDocuments.cpp:277-280` (`sameFile`) compares project paths the same case-sensitive way, to hide the session's own file from the `recentOthers()` list. On Windows a casing change can therefore list the open project as "another recent project". Not fixed here: different subsystem, different severity, and the fix would change `documents.recent` output beyond this finding's scope.
- `platformPathCase()` deliberately does not fold case on macOS. Qt cannot say whether a volume is case-insensitive, and folding on a case-sensitive APFS volume would offer another project's unsaved work; `ZENE_PATH_CASE=insensitive` is the documented answer for a macOS user who hits it. A future fix could detect the volume by probing, which would be a design change, not a bug fix.
