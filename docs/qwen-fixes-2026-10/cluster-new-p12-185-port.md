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
* `restoreProjectRevision()` is the only function changed.

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
| Every new attempt gets a staging path of its own | [`src/core/ProjectRevisions.cpp:147`](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-185-port/src/core/ProjectRevisions.cpp:147) (`newRestoreStagingPath`) |
| Hidden stages (a dot-prefixed project file) are found too | [`src/core/ProjectRevisions.cpp:139`](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-185-port/src/core/ProjectRevisions.cpp:139) (`QDir::Files | QDir::Hidden`) |
| A refused rotation keeps the stage | [`src/core/ProjectRevisions.cpp:336`](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-185-port/src/core/ProjectRevisions.cpp:336) |
| The contract is documented on the declaration | [`include/ProjectRevisions.h:77`](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-185-port/include/ProjectRevisions.h:77) |
| Deterministic retry regressions | [`tests/src/core/RevisionTimelineTest.cpp:440`](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-185-port/tests/src/core/RevisionTimelineTest.cpp:440) (the fixed `.restore` name), [`tests/src/core/RevisionTimelineTest.cpp:460`](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-185-port/tests/src/core/RevisionTimelineTest.cpp:460) (a hidden `.restore.<uuid>` stage) |

The legacy `.restore` name is still recognised, so a pending target left by an
attempt of the older code is recovered rather than overwritten.

## Evidence

All commands ran in the worktree
`/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-185-port`;
the CTest commands ran from `build/tests`. Every log is in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-185-port.r1/logs`.

| Phase | Exact command | Exit | Result |
| --- | --- | ---: | --- |
| RED build (regression only, original product code) | `cmake --build build --target RevisionTimelineTest > .../logs/build-1.log 2>&1` | 0 | Regression built against the original product code. |
| RED on the original product code | `ctest -R '^RevisionTimelineTest$' --output-on-failure > .../logs/test-1.log 2>&1` | 8 | 23 QtTest cases passed, 2 failed: `failedRestoreRetryUsesPendingStagedTarget` and `failedRestoreRetryFindsHiddenUuidStage` - the live file held the rotated bytes (3 notes) instead of the requested document (2 notes). |
| GREEN build (app and test with the fix) | `cmake --build build --target zene RevisionTimelineTest > .../logs/build-2.log 2>&1` | 0 | The header's dependents (`ControlCommandsProject.cpp`, `ControlCommandsProjectFiles.cpp`, `ControlCommandsRevisions.cpp`, `RevisionTimeline.cpp`, `ProjectRevisions.cpp`) were recompiled and `zene` relinked. |
| GREEN with the fix | `ctest -R '^RevisionTimelineTest$' --output-on-failure > .../logs/test-2.log 2>&1` | 0 | 1/1 ctest case passed. |
| Save and reverse the production patch | `git diff -- include/ProjectRevisions.h src/core/ProjectRevisions.cpp > .../logs/fix.patch && git apply -R .../logs/fix.patch` | 0 | Only the product fix removed; the regression stayed in place. |
| Negative-control build | `cmake --build build --target RevisionTimelineTest > .../logs/build-3.log 2>&1` | 0 | Rebuilt with the fix reversed. |
| Negative-control RED | `ctest -R '^RevisionTimelineTest$' --output-on-failure > .../logs/test-4.log 2>&1` | 8 | The same two assertions failed again: 23 passed, 2 failed. The regression catches the bug. |
| Reapply the production patch | `git apply .../logs/fix.patch` | 0 | Fix restored. |
| Final build of app and test | `cmake --build build --target zene RevisionTimelineTest > .../logs/build-4.log 2>&1` | 0 | App and test rebuilt with the fix. |
| Final GREEN | `ctest -R '^RevisionTimelineTest$' --output-on-failure > .../logs/test-5.log 2>&1` | 0 | 1/1 ctest case passed. |
| Affected-family count | `ctest -R '^RevisionTimelineTest$' --output-on-failure -V > .../logs/test-6.log 2>&1` | 0 | **1 registered ctest case; 25 QtTest cases passed, 0 failed, 0 skipped.** |
| Neighbouring reversibility families | `ctest -R '^(RevisionTimelineTest|ReversibilityContractTest|ReversibilityUndoTest|ReversibilityUndoDeviceTest)$' --output-on-failure > .../logs/test-7.log 2>&1` | 0 | 4/4 ctest cases passed. |
| Final build after the last edit | `cmake --build build --target zene RevisionTimelineTest > .../logs/build-9.log 2>&1` | 0 | Rebuilt from the final tree. |
| Final GREEN after the last edit | `ctest -R '^RevisionTimelineTest$' --output-on-failure > .../logs/test-13.log 2>&1` | 0 | 1/1 ctest case passed. |
| Final affected-family count | `ctest -R '^RevisionTimelineTest$' --output-on-failure -V > .../logs/test-14.log 2>&1` | 0 | 1 registered ctest case; 25 QtTest cases passed, 0 failed, 0 skipped. |
| Fork-source manifest gate | `bash tests/fork-sources-gate.sh > .../logs/gate-7.log 2>&1` | 0 | Every tracked source is registered; 0 stale entries. No new file had to be registered (all three changed files are already in `tests/fork-sources.txt`). |

The full cycle was repeated a second time on the final tree, to check the
neighbouring transcript test against the pristine product code as well: `git
diff > .../logs/work.patch && git apply -R .../logs/work.patch` (exit 0) put the
pristine product code back, `cmake --build build --target zene >
.../logs/build-7.log 2>&1` (exit 0) rebuilt the app without the fix, `ctest -R
'^ControlReversibilityTranscript$' --output-on-failure > .../logs/test-10.log
2>&1` (exit 8) failed there for the same reason as with the fix, and `git apply
.../logs/work.patch` (exit 0) restored the tree.

### Gates

Both ratchet gates FAIL on this branch, and they fail **identically without
this change**: the violations are in files this fix does not touch, and the
baseline on this branch does not grandfather them. The evidence is the pair of
runs on the pristine tree (the fix and the regression both reverse-applied):
`.../logs/gate-3.log` and `.../logs/gate-4.log`.

| Gate | Exact command | Exit | Result |
| --- | --- | ---: | --- |
| Complexity | `bash tests/complexity-gate.sh --check > .../logs/gate-5.log 2>&1` | 1 | Six pre-existing regressions, none of them a function this change added or raised. `diff` of the `REGRESSION:` lines of the pristine run (`gate-3.log`) and the final run (`gate-5.log`) differs in one line only: `rotateProjectRevision@151-222` (CCN 11) became `rotateProjectRevision@182-253` (CCN 11) - the same untouched function, shifted by the 31 added lines above it. The functions this change adds are `pendingRestoreStagingPath` (CCN 3), `newRestoreStagingPath` (CCN 1) and `restoreProjectRevision` (CCN 9, was 8) - all within the target of 10 (`lizard src/core/ProjectRevisions.cpp`). |
| File length | `bash tests/file-length-gate.sh --check > .../logs/gate-6.log 2>&1` | 1 | Fourteen pre-existing violations. `diff` of the `REGRESSION:` lines of the pristine run (`gate-4.log`) and the final run (`gate-6.log`) differs in one line only: `tests/src/core/RevisionTimelineTest.cpp (600)` became `(634)` - that file was already over the 500-line limit (and already reported) before this change, and the regression was placed there because it is the test file that covers this source file. The gate limit and the baseline were not touched. |

The gates were not re-anchored and no limit was changed.

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
- `tests/src/core/RevisionTimelineTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p12-185-port.md`
