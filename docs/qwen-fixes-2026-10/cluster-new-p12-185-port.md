# Cluster new-p12-185-port

Verdict: **FIXED**

The fix accepted on `bugs/qwen-new-p12-185` (commits `9611baada` "preserve
staged restore targets" and `6a4bbfeb9` "find hidden restore stages") is
re-applied by hand to this branch, which is a fresh branch off the newest line
(`050/m0-train-5`). The newest line does **not** already fix the bug: the
current tree still staged a restore into the fixed path `<file>.rev<n>.restore`
and removed it before every attempt, so a retry of a failed restore staged the
bytes the rotation had put in the revision slot instead of the bytes the failed
attempt had staged.

The port could not be a cherry-pick: the two files the fix touches
(`include/ProjectRevisions.h`, `src/core/ProjectRevisions.cpp`) were rewritten
on the newest line by the Luna 5236 / 4352 rework (unique `QTemporaryFile`
staging, one replacing rename over the live file), so the old diff no longer
applied. The behaviour was therefore re-written against the current code, and
the parts of the old fix that the current code already provides were NOT
re-introduced:

* `replaceProjectWithStaged()` on this line already keeps the staging file
  when the replacement fails (it only removes the staging once the replacement
  is in place), so the old `preserveStagingOnFailure` parameter of that
  function was **not** added - the declaration in
  `include/ProjectRevisions.h:98` is unchanged and no target had to be rebuilt
  for an ABI change.
* `restoreProjectRevision()` is the only function whose behaviour changed.

## The bug on this line

`src/core/ProjectRevisions.cpp` before the fix (the lines the finding names,
`src/core/ProjectRevisions.cpp:267` in the historical numbering):

```cpp
const QString staging = projectRevisionPath(projectPath, revision) + QStringLiteral(".restore");
QFile::remove(staging);            // the only staged copy of the target is deleted
if (!QFile::copy(source, staging)) // and re-staged from the revision slot
```

A restore stages `.rev0`, rotates the live file in (which overwrites `.rev0`
with the replaced live file), and then fails. The requested bytes are then only
in `.rev0.restore`. Retrying the same revision removed that file and staged
the now-different `.rev0` - the file the restore was meant to replace came
back.

## The fix on this line

| Finding | Evidence |
| --- | --- |
| A pending stage of an earlier failed attempt is found and reused | [`src/core/ProjectRevisions.cpp:128`](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-185-port/src/core/ProjectRevisions.cpp:128) (`pendingRestoreStagingPath`), [`src/core/ProjectRevisions.cpp:316`](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-185-port/src/core/ProjectRevisions.cpp:316) |
| Every new attempt gets a staging path of its own | [`src/core/ProjectRevisions.cpp:154`](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-185-port/src/core/ProjectRevisions.cpp:154) (`newRestoreStagingPath`) |
| The revision's file name is matched LITERALLY, never as a wildcard | [`src/core/ProjectRevisions.cpp:141`](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-185-port/src/core/ProjectRevisions.cpp:141) (the prefix), [`src/core/ProjectRevisions.cpp:144`](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-185-port/src/core/ProjectRevisions.cpp:144) (the literal `startsWith` comparison) |
| Hidden stages (a dot-prefixed project file) are found too | [`src/core/ProjectRevisions.cpp:144`](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-185-port/src/core/ProjectRevisions.cpp:144) (`QDir::Files | QDir::Hidden`) |
| A refused rotation keeps the stage | [`src/core/ProjectRevisions.cpp:336`](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-185-port/src/core/ProjectRevisions.cpp:336) |
| The contract is documented on the declaration | [`include/ProjectRevisions.h:77`](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-185-port/include/ProjectRevisions.h:77) |
| Deterministic retry regressions | [`tests/src/core/ProjectRevisionRestoreStagingTest.cpp:119`](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-185-port/tests/src/core/ProjectRevisionRestoreStagingTest.cpp:119) (the fixed `.restore` name), [`tests/src/core/ProjectRevisionRestoreStagingTest.cpp:136`](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-185-port/tests/src/core/ProjectRevisionRestoreStagingTest.cpp:136) (a hidden `.restore.<uuid>` stage), [`tests/src/core/ProjectRevisionRestoreStagingTest.cpp:156`](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-185-port/tests/src/core/ProjectRevisionRestoreStagingTest.cpp:156) (a project file whose name holds glob characters, `song[1].mmp`) |

The legacy `.restore` name is still recognised, so a pending target left by an
attempt of the older code is recovered rather than overwritten.

## Round 2 (the review's two points)

**1. Pending stages are missed for filenames containing glob characters.** The
first round built the pending-stage lookup as a `QDir` name filter, so the
project's file name was interpolated into wildcard syntax: `song[1].mmp` became
the pattern `song[1].mmp.rev0.restore.*`, Qt 6.12 read `[1]` as a character
class, and the stage was never found. The lookup now matches the file name
LITERALLY (`startsWith` over the directory's entries, no pattern), and
`failedRestoreRetryFindsStageForANameWithGlobCharacters` proves it with a
bracketed project name.

**2. The ratchet acceptance.** The two new cases and the third one now live in
a NEW registered test source,
`tests/src/core/ProjectRevisionRestoreStagingTest.cpp`, so
`tests/src/core/RevisionTimelineTest.cpp` is back at the parent's 600 code
lines and this commit no longer grows a file the length gate already flags.
The one gate row that sat in a file this lane owns is gone as well:
`rotateProjectRevision` was the only function over the target in
`src/core/ProjectRevisions.cpp` (CCN 11), and it is now split into
`shiftRevisionsUp` (CCN 5) and `countRetainedRevisions` (CCN 3), which leaves
`rotateProjectRevision` at CCN 6. The split is behaviour-preserving - the same
steps in the same order, with the same refusal messages - and is covered by
the rotation cases of `RevisionTimelineTest` and by the save cases of
`ProjectContainerSaveTest`, `DataFileSaveIntegrityTest` and the other
container tests.

## Evidence

All commands ran in the worktree
`/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-185-port`;
the CTest commands ran from `build/tests`. Every log is in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-185-port.r1/logs`.

| Phase | Exact command | Exit | Result |
| --- | --- | ---: | --- |
| RED build (the new test source, the wildcard lookup still in the product) | `cmake --build build --target ProjectRevisionRestoreStagingTest > .../logs/build-11.log 2>&1` | 0 | The new target built. |
| RED on the wildcard lookup | `ctest -R '^ProjectRevisionRestoreStagingTest$' --output-on-failure > .../logs/test-17.log 2>&1` | 8 | 4 QtTest cases passed, 1 failed: `failedRestoreRetryFindsStageForANameWithGlobCharacters` - the live file held the rotated bytes (3 notes) instead of the requested document (2 notes). The other two cases passed: they do not depend on the lookup's matching. |
| GREEN build (app and test with the literal matching) | `cmake --build build --target zene ProjectRevisionRestoreStagingTest RevisionTimelineTest > .../logs/build-12.log 2>&1` | 0 | The header's dependents (`ControlCommandsProject.cpp`, `ControlCommandsProjectFiles.cpp`, `ControlCommandsRevisions.cpp`, `RevisionTimeline.cpp`, `ProjectRevisions.cpp`) were recompiled and `zene` relinked. |
| GREEN with the literal matching | `ctest -R '^ProjectRevisionRestoreStagingTest$' --output-on-failure > .../logs/test-18.log 2>&1` | 0 | 1/1 ctest case passed. |
| Affected-family count | `ctest -R '^ProjectRevisionRestoreStagingTest$' --output-on-failure -V > .../logs/test-19.log 2>&1` | 0 | **1 registered ctest case; 5 QtTest cases passed, 0 failed, 0 skipped.** |
| Save and reverse the round-2 product patch | `git diff HEAD -- include/ProjectRevisions.h src/core/ProjectRevisions.cpp > .../logs/fix5.patch && git apply -R .../logs/fix5.patch` | 0 | Only the product hunks of this round removed; the test sources stayed in place. |
| Negative-control build | `cmake --build build --target ProjectRevisionRestoreStagingTest RevisionTimelineTest > .../logs/build-19.log 2>&1` | 0 | Rebuilt with the round-2 hunks reversed. |
| Negative-control RED | `ctest -R '^(ProjectRevisionRestoreStagingTest|RevisionTimelineTest)$' --output-on-failure > .../logs/test-28.log 2>&1` | 8 | The bracketed-name assertion failed again (4 passed, 1 failed); the other two cases still pass, because the round-1 reuse logic is not part of this round's patch. |
| Reapply the round-2 product patch | `git apply .../logs/fix5.patch` | 0 | Fix restored (the product file was compared against a copy taken before the cycle: identical). |
| Final build of app and tests | `cmake --build build --target zene ProjectRevisionRestoreStagingTest RevisionTimelineTest > .../logs/build-20.log 2>&1` | 0 | App and both test targets rebuilt with the fix. |
| Final GREEN, whole affected family | `ctest -R '^(ProjectRevisionRestoreStagingTest\|RevisionTimelineTest\|ReversibilityContractTest\|ReversibilityUndoTest\|ReversibilityUndoDeviceTest\|ProjectContainerSaveTest\|ControlProjectArchiveTest\|DataFileSaveIntegrityTest\|ProjectContainerTest\|ProjectContainerEntriesTest)$' --output-on-failure > .../logs/test-29.log 2>&1` | 0 | **10/10 ctest cases passed.** |
| Final affected-family count | `ctest -R '^ProjectRevisionRestoreStagingTest$' --output-on-failure -V > .../logs/test-27.log 2>&1` | 0 | 1 registered ctest case; 5 QtTest cases passed, 0 failed, 0 skipped. |
| Whole-parent control (every product hunk of both rounds reversed) | `git diff ded355440 -- include/ProjectRevisions.h src/core/ProjectRevisions.cpp > .../logs/fix4.patch && git apply -R .../logs/fix4.patch` then `cmake --build build --target ProjectRevisionRestoreStagingTest RevisionTimelineTest > .../logs/build-16.log 2>&1` then `ctest -R '^(ProjectRevisionRestoreStagingTest\|RevisionTimelineTest)$' --output-on-failure > .../logs/test-25.log 2>&1` | 0 / 0 / 8 | With the whole product fix reversed, all THREE cases fail (2 passed, 3 failed): every one of them is a regression proof. (`ded355440` is the parent of this lane; the patch was written while the branch head was the round-1 commit, where `HEAD~1` named that same parent.) |
| Fork-source manifest gate | `bash tests/fork-sources-gate.sh > .../logs/gate-12.log 2>&1` | 0 | 1791 fork-NEW entries, 0 stale; the new test source is registered. |
| Unregistered-test gate | `bash tests/unregistered-tests-gate.sh > .../logs/gate-13.log 2>&1` | 0 | 516 test sources scanned, every one registered or declared. |
| Final build of the committed tree | `cmake --build build --target zene ProjectRevisionRestoreStagingTest RevisionTimelineTest > .../logs/build-21.log 2>&1` | 0 | Rebuilt from the commit. |
| A CTest run from the worktree ROOT | `ctest -R '^(ProjectRevisionRestoreStagingTest\|RevisionTimelineTest)$' --output-on-failure > .../logs/test-30.log 2>&1` | 97 | Refused by the lane guard (`bunny-guard: run ctest from <lane>/build/tests`); no test ran. The required invocation from `build/tests` is the one below. |
| Final GREEN of the committed tree, from `build/tests` | `ctest -R '^(ProjectRevisionRestoreStagingTest\|RevisionTimelineTest\|ReversibilityContractTest\|ReversibilityUndoTest\|ReversibilityUndoDeviceTest)$' --output-on-failure > .../logs/test-32.log 2>&1` | 0 | 5/5 ctest cases passed. |
| Final affected-family count of the committed tree | `ctest -R '^ProjectRevisionRestoreStagingTest$' --output-on-failure -V > .../logs/test-33.log 2>&1` | 0 | 1 registered ctest case; 5 QtTest cases passed, 0 failed, 0 skipped. |
| Final complexity gate on the committed tree | `bash tests/complexity-gate.sh --check > .../logs/gate-14.log 2>&1` | 1 | The same five rows as `gate-10.log` (the `diff` of the `REGRESSION:` lines is empty). |
| Final file-length gate on the committed tree | `bash tests/file-length-gate.sh --check > .../logs/gate-15.log 2>&1` | 1 | The same fourteen rows as `gate-11.log` (the `diff` of the `REGRESSION:` lines is empty). |
| Final build for the self-check run | `cmake --build build --target zene ProjectRevisionRestoreStagingTest RevisionTimelineTest > .../logs/build-22.log 2>&1` | 0 | Rebuilt from the committed tree. |
| Final GREEN of the whole affected family | `ctest -R '^(ProjectRevisionRestoreStagingTest\|RevisionTimelineTest\|ReversibilityContractTest\|ReversibilityUndoTest\|ReversibilityUndoDeviceTest\|ProjectContainerSaveTest\|ControlProjectArchiveTest\|DataFileSaveIntegrityTest\|ProjectContainerTest\|ProjectContainerEntriesTest)$' --output-on-failure > .../logs/test-34.log 2>&1` | 0 | 10/10 ctest cases passed. |
| Final affected-family count | `ctest -R '^ProjectRevisionRestoreStagingTest$' --output-on-failure -V > .../logs/test-35.log 2>&1` | 0 | 1 registered ctest case; 5 QtTest cases passed, 0 failed, 0 skipped. |
| Final complexity gate | `bash tests/complexity-gate.sh --check > .../logs/gate-16.log 2>&1` | 1 | The same five rows as `gate-14.log` (the `diff` of the `REGRESSION:` lines is empty). |
| Final file-length gate | `bash tests/file-length-gate.sh --check > .../logs/gate-17.log 2>&1` | 1 | The same fourteen rows as `gate-15.log` (the `diff` of the `REGRESSION:` lines is empty). |
| Final fork-source / unregistered-test gates | `bash tests/fork-sources-gate.sh > .../logs/gate-18.log 2>&1` and `bash tests/unregistered-tests-gate.sh > .../logs/gate-19.log 2>&1` | 0 / 0 | Every tracked source registered, every test source registered or declared. |

### Gates

Both ratchet gates still exit 1 on this branch, because the violations that
remain are in files this lane does not touch and they are red on the parent
commit too. What this lane could resolve, it resolved: the
`tests/src/core/RevisionTimelineTest.cpp` row is back at the parent's 600 code
lines, and the complexity row for `src/core/ProjectRevisions.cpp` is gone. The
evidence is the pair of runs on the pristine tree (the whole working tree
reverse-applied): `.../logs/gate-3.log` and `.../logs/gate-4.log`.

| Gate | Exact command | Exit | Result |
| --- | --- | ---: | --- |
| Complexity | `bash tests/complexity-gate.sh --check > .../logs/gate-10.log 2>&1` | 1 | **Five rows, one FEWER than the parent** (six): the `rotateProjectRevision@src/core/ProjectRevisions.cpp` row is gone. `diff` of the `REGRESSION:` lines of the pristine run (`gate-3.log`) and the final run (`gate-10.log`) differs by exactly that one deleted line. The five rows left are in `tests/src/gui/XyPadPairTest.cpp`, `src/core/DawProjectReadTracks.cpp`, `src/core/ControlCommandsMixerTools.cpp`, `src/core/ControlCommandsMixerStrip.cpp` and `src/gui/ControlCommandsShortcuts.cpp` - files this lane does not touch. Every function this change adds or touches is within the target of 10 (`lizard src/core/ProjectRevisions.cpp`: `pendingRestoreStagingPath` 4, `newRestoreStagingPath` 1, `shiftRevisionsUp` 5, `countRetainedRevisions` 3, `rotateProjectRevision` 6, `restoreProjectRevision` 9). |
| File length | `bash tests/file-length-gate.sh --check > .../logs/gate-11.log 2>&1` | 1 | **Fourteen rows, IDENTICAL to the parent's** (`diff` of the `REGRESSION:` lines of `gate-4.log` and `gate-11.log` is empty). `tests/src/core/RevisionTimelineTest.cpp` is back at the parent's 600 code lines - this commit does not grow it - and the new `tests/src/core/ProjectRevisionRestoreStagingTest.cpp` is under the limit and not flagged. The rows left are in files this lane does not touch; taking them under the limit would mean splitting or deleting unrelated sources, which is out of this lane's scope. |

The gates were not re-anchored, no limit was changed and no baseline was
written.

### A pre-existing failure outside this change

`ctest -R '^ControlReversibilityTranscript$'` fails on this branch
(`.../logs/test-8.log`, exit 8) with `section E could not run: plugin.list
offered no 'amplifier' device` - the LADSPA fixture plugin is not built in this
build tree. The same failure, with the same message, appears with the
production fix reversed (`.../logs/test-10.log`, exit 8), so it is not caused
by this change.

## Files committed

- `include/ProjectRevisions.h`
- `src/core/ProjectRevisions.cpp`
- `tests/src/core/ProjectRevisionRestoreStagingTest.cpp` (new)
- `tests/src/core/RevisionTimelineTest.cpp`
- `tests/CMakeLists.txt`
- `tests/fork-sources.txt`
- `tests/all-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p12-185-port.md`

## Self-check

### 1. The regression FAILS with the product change reverted and PASSES with it (exit codes)

| State | Exact command | Exit | Result |
| --- | --- | ---: | --- |
| Round-2 product hunks reverted | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-185-port.r1/logs/fix5.patch` (exit 0) → `cmake --build build --target ProjectRevisionRestoreStagingTest RevisionTimelineTest > .../logs/build-19.log 2>&1` (exit 0) → `ctest -R '^(ProjectRevisionRestoreStagingTest\|RevisionTimelineTest)$' --output-on-failure > .../logs/test-28.log 2>&1` | **8** | 1 of 5 cases failed: `failedRestoreRetryFindsStageForANameWithGlobCharacters` (4 passed, 1 failed). The live file held the rotated bytes (3 notes) instead of the requested document (2 notes). |
| Every product hunk of both rounds reverted (the patch `fix4.patch` is the diff against the lane's parent, `ded355440`) | `git apply -R .../logs/fix4.patch` (exit 0) → `cmake --build build --target ProjectRevisionRestoreStagingTest RevisionTimelineTest > .../logs/build-16.log 2>&1` (exit 0) → `ctest -R '^(ProjectRevisionRestoreStagingTest\|RevisionTimelineTest)$' --output-on-failure > .../logs/test-25.log 2>&1` | **8** | 3 of 5 cases failed (2 passed, 3 failed): all three retry regressions fail without the fix. |
| The fix in place | `ctest -R '^ProjectRevisionRestoreStagingTest$' --output-on-failure > .../logs/test-18.log 2>&1` and `ctest -R '^ProjectRevisionRestoreStagingTest$' --output-on-failure -V > .../logs/test-19.log 2>&1` | **0** | 1/1 ctest case; 5 QtTest cases passed, 0 failed, 0 skipped. |
| The fix in place, committed tree | `ctest -R '^(ProjectRevisionRestoreStagingTest\|RevisionTimelineTest\|ReversibilityContractTest\|ReversibilityUndoTest\|ReversibilityUndoDeviceTest)$' --output-on-failure > .../logs/test-32.log 2>&1` | **0** | 5/5 ctest cases passed. |
| The original wildcard lookup (the review's finding) | `ctest -R '^ProjectRevisionRestoreStagingTest$' --output-on-failure > .../logs/test-17.log 2>&1` | **8** | The bracketed-name case failed before the literal-matching change. |

### 2. The test is deterministic: no wall-clock timing, sleeps or races

The seam is the **on-disk state of a failed restore**, seeded directly: the
test writes the live file, `.rev0`, `.rev1` and the staging file into a
`QTemporaryDir` (`tests/src/core/ProjectRevisionRestoreStagingTest.cpp:88-110`,
`seedFailedRestore`), and the only product call is
`restoreProjectRevision()` - a synchronous file-level function. There is no
timer, no sleep, no thread and no clock read in the test, and it does not use
the `setRevisionReplacementFailureForTest()` failure seam at all: the state
after the failed replacement is written instead of provoked.

The injected values are fixed: the requested document `documentWith(2)`, the
rotated live document `documentWith(3)`, the project names `song.mmp`,
`.hidden.mmp` and `song[1].mmp`, and the stage suffix
`.restore.4c0c55e0-809d-4923-a9d0-c708fd0adf7c`. The product's own
`QUuid::createUuid()` (used only for the name of a NEW stage,
`src/core/ProjectRevisions.cpp:154`) never enters the assertions: the cases
assert on the seeded stage names, and a case that seeds one asserts that this
one is gone afterwards.

### 3. The fix is at the cause and complete: every caller and sibling checked

Callers of `restoreProjectRevision()` (the only function whose behaviour
changed), all checked and consistent:

* `src/core/RevisionTimeline.cpp:399` - a rotation id ("rev0".."rev2") delegates
  to the policy's own restore, so it gets the preserved stage.
* `src/core/ControlCommandsProjectFiles.cpp:353` (`project.restore_revision`)
  and its recorded undo/redo steps at `:367` and `:377`.
* `src/core/ControlCommandsRevisions.cpp:156` (the undo of a `revisions.restore`)
  and `:171` (its redo).
* `include/ProjectRevisions.h:89` - the declaration, whose contract now names
  the stage.

Siblings of the changed code:

* `replaceProjectWithStaged()` (`src/core/ProjectRevisions.cpp:376`) - the
  function that CONSUMES the stage. It already keeps the staging file until
  the replacement is in place, so a failed replacement leaves the staged bytes;
  its declaration (`include/ProjectRevisions.h:98`) is unchanged and no
  `preserveStagingOnFailure` parameter was invented for it.
* `restoreTimelineRevision()` (`src/core/RevisionTimeline.cpp:384`) - the
  sibling restore of the timeline's STABLE artefacts (`.bak`, autosave, git).
  It stages under its own name (`<file>.timeline-stage`, `:409`), which is not
  a revision stage and cannot be picked up by the pending-stage lookup; its
  source artefact is not rotated away, so a retry re-stages the same bytes.
  It is deliberately untouched - the finding and the accepted upstream fix are
  about the keep-3 restore, the only one that takes its target out of the set.
* `rotateProjectRevision()` - split into `shiftRevisionsUp` (CCN 5) and
  `countRetainedRevisions` (CCN 3) to clear the complexity row in this lane's
  own file; same steps, same order, same refusal messages, covered by the
  rotation and save cases of `RevisionTimelineTest`,
  `ProjectContainerSaveTest`, `DataFileSaveIntegrityTest`,
  `ProjectContainerTest`, `ProjectContainerEntriesTest` and
  `ControlProjectArchiveTest` (10/10 ctest cases green, `.../logs/test-29.log`).

### 4. No collateral damage

* **Old projects and saved data load as before.** The change touches only the
  side files next to a project (`<file>.rev<n>.restore`, and the new
  `<file>.rev<n>.restore.<uuid>`): the project file itself, its format and the
  keep-3 revision set (`.rev0`/`.rev1`/`.rev2`) are untouched, and
  `projectRevisionState()` still reports exactly the policy's slots. A project
  saved before this change has no stage file at all, so nothing about its
  contents or its revisions changes.
* **No limit or check was added.** There is no cap, threshold or new refusal
  path: the pending-stage lookup replaced a wildcard match with a literal
  prefix comparison over the same directory. For a file name without glob
  characters the two match the same files; for one with glob characters the
  new lookup matches the correct file, which the old one missed. The prefix is
  `<project>.<revision file>.restore.`, so it can only ever match a stage of
  that project and that revision - no other project or revision is affected.
* **The one deliberate trade-off**, inherited from the accepted upstream fix: a
  stage left by a failed attempt is now kept and reused by the next attempt at
  the same revision, instead of being overwritten. That is the fix's point
  (the staged bytes are the only copy of the requested target), the stage is a
  side file the user can delete to discard it, and the legacy `.restore` name
  is still recognised so a stage left by the older code is recovered rather
  than clobbered.
* **The gates' limits and baselines were not touched**, and the new test
  source is registered everywhere it must be (`tests/CMakeLists.txt`,
  `tests/fork-sources.txt`, `tests/all-sources.txt`, with the inherited
  `tests/CMakeLists.txt` change recorded in `tests/upstream-modifications.txt`):
  `bash tests/fork-sources-gate.sh` and `bash tests/unregistered-tests-gate.sh`
  both exit 0.

### 5. ALREADY-FIXED / FALSE-POSITIVE

Not applicable: the verdict is **FIXED**, not ALREADY-FIXED or
FALSE-POSITIVE. The newest line did not carry the fix - at the lane's parent
commit (`ded355440`) the restore removed the fixed staging path before every
attempt (`src/core/ProjectRevisions.cpp:279-285`, and again at `:296` on a
refused rotation), which is what the three regressions above are red against.
The fix lives at
`src/core/ProjectRevisions.cpp:128` (`pendingRestoreStagingPath`), `:141-148`
(the literal prefix match), `:154` (`newRestoreStagingPath`), `:316` (the
stage is reused instead of re-staged) and `:336` (a refused rotation keeps
the stage); the contract is stated on the declaration at
`include/ProjectRevisions.h:77`.
