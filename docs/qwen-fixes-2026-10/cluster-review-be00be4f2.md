ALREADY-FIXED

# Cluster review-be00be4f2 - a failed project replace left no live project

Verdict: **ALREADY-FIXED**. The defect is real (a project restore/replace could
remove `song.mmp` before the new bytes were in place, so an out-of-disk or
failed copy left the user with no project at all), and it is already fixed on
this branch by commit `7f1af20295` with a shipped regression test. No product
code, test or gate was changed by this closure; this report is the only change.

## Why the reported defect was real

Background: in the parent of `7f1af20295`, `replaceProjectWithStaged` had a
delete-first body (`git show 7f1af20295^:src/core/ProjectRevisions.cpp`, lines
282-304):

```cpp
QFile::remove(pending);
if (!QFile::copy(staging, pending)) { QFile::remove(pending); return false; }
QFile::remove(staging);
const bool hadLive = QFileInfo::exists(projectPath);
QFile::remove(projectPath);                 // live project gone here
if (QFile::rename(pending, projectPath)) { return true; }
QFile::remove(pending);
if (hadLive) { QFile::copy(projectRevisionPath(projectPath, 0), projectPath); }
```

With the live file already deleted, a failed `rename` had to be recovered by
copying revision 0 back - the same copy that typically fails for the same
reason (full disk). The report's failure mode followed: "the failed
replacement emptied the live project".

## The fix that is already in the branch

Current code, `src/core/ProjectRevisions.cpp:465-481`:

```cpp
bool replaceProjectWithStaged(const QString& projectPath, const QString& staging)
{
    // Luna cluster 4352 / 2026-10-08 rework: the live file is never removed.
    const QString pending = stageCopy(staging, projectPath + QStringLiteral(".replace-XXXXXX")); // 472
    if (pending.isEmpty()) { return false; }
    if (!replaceWithStaged(pending, projectPath))                                              // 474
    {
        QFile::remove(pending);
        return false;
    }
    QFile::remove(staging);                                                                    // 479
    return true;
}
```

- Line 472: unique sibling staged by copy; a failed copy leaves the live file
  and the staging file exactly as they were.
- Line 474: exactly one `replaceWithStaged` (one replacing rename) over the live
  file; a failed rename leaves the live project in place, so no recovery copy
  is needed.
- Line 479: the staging file is removed only after the replacement succeeded,
  so a failure still has the bytes the restore took out of the revision set.

Intended contract, `include/ProjectRevisions.h:94-100`: "The live file is
replaced in one rename, so when the replacement cannot be made the live file is
left exactly as it was and a failed restore never empties the project path.
False when the replacement did not happen; staging is removed only once the
replacement is in place".

Commit `7f1af20295` "fix(rework be00be4f2): a failed project replace could
still leave no live project" also routes `RevisionTimeline.cpp:433` through the
same helper and registers the tests.

## Shipped regression tests

- `tests/src/core/RevisionTimelineTestParts1.h:326-346`
  `aFailedReplaceKeepsTheLiveProject` - no revision 0, no staging file; asserts
  the replace returns false, the live project still exists, and its bytes are
  unchanged.
- `tests/src/core/RevisionTimelineTestParts1.h:347-366`
  `aFailedReplacementAfterStagingKeepsTheLiveProject` - staging holds bytes and
  revision 0 is made unwritable; `setRevisionReplacementFailureForTest(true)`
  injects the failed rename; asserts false, live file exists, bytes unchanged,
  and staging preserved. This is the test that fails when the live-removal
  fault is injected (RED) and passes with the current body (GREEN).

## Evidence commands and exit codes (from the run logs)

| Step | Command (summary) | EXIT | Log |
| --- | --- | --- | --- |
| p1 | `git show 7f1af20295 -- src/core/ProjectRevisions.cpp include/ProjectRevisions.h` | 0 | logs/evidence-1.log |
| p1 | `git show 7f1af20295^:src/core/ProjectRevisions.cpp \| sed -n '280,320p'` | 0 | logs/evidence-1.log |
| p1 | `sed -n '326,366p' tests/src/core/RevisionTimelineTestParts1.h` | 0 | logs/evidence-1.log |
| p1 | `rg -n 'restoreProjectRevision\|replaceProjectWithStaged' src include tests` | 0 | logs/evidence-1.log |
| p2 RED | `cmake --build build --target zene RevisionTimelineTest` (live-removal fault injected from logs/fault.patch) | 0 | logs/build-red-1.log |
| p2 RED | `ctest -R ^RevisionTimelineTest$ --output-on-failure` | 8 | logs/test-red-1.log |
| p2 restore | `git apply logs/fault.patch` then `git apply -R logs/fault.patch` (inject, then undo) | 0 (APPLY_EXIT=0, REVERSE_EXIT=0) | logs/fault-reverse-1.log |
| p2 restore | `git status --porcelain` / `git diff --exit-code -- src/core/ProjectRevisions.cpp` (no fault hunks left) | 0 (empty porcelain, DIFF_EXIT=0) | logs/fault-reverse-1.log |
| p2 GREEN | `cmake --build build --target zene RevisionTimelineTest` | 0 | logs/build-green-1.log |
| p2 GREEN | `ctest -R ^RevisionTimelineTest$ --output-on-failure` | 0 | logs/test-green-1.log |
| p3 | `cmake --build build ...` (revision test family) | 0 | logs/build-green-2b.log |
| p3 | `ctest -R '<8-test revision family>'` (8 tests incl. `RevisionTimelineTest`, `ProjectRevIdsTest`, `RevisionsDocsTest`) - 8/8 passed | 0 | logs/test-green-2b.log |
| p4 | `python3 .../feature_check.py <worktree>` - RESULT: PASS | 0 | logs/feature-check-1.log |

The p2 RED run failed exactly the reported shape:

```
FAIL!  : RevisionTimelineTest::aFailedReplacementAfterStagingKeepsTheLiveProject()
         'QFileInfo::exists(m_project)' returned FALSE.
         (the failed replacement emptied the live project)
0% tests passed, 1 tests failed out of 1
EXIT=8
```

The fault injected for the RED run is a live-removal shape, not the historical
delete-first body: `logs/fault.patch` replaces the whole helper body with
`QFile::remove(projectPath); return false;`. The live project is deleted and the
replace reports failure, which is exactly the reported outcome. The delete-first
parent body above is background only; it was not the mutation used.

## Self-check

**ASK: the regression test FAILS with the product change reverted and PASSES
with it (exit codes).**
With the live-removal fault injected (`logs/fault.patch` replaces the body with
`QFile::remove(projectPath); return false;`), the build was
green (EXIT=0) and `RevisionTimelineTest` failed with
`aFailedReplacementAfterStagingKeepsTheLiveProject ... "the failed replacement
emptied the live project"`, RED **EXIT=8** (`logs/test-red-1.log`). The reverse
and restore were retained in `logs/fault-reverse-1.log`: `APPLY_EXIT=0`,
`REVERSE_EXIT=0`, empty `git status --porcelain`, and
`git diff --exit-code -- src/core/ProjectRevisions.cpp` `DIFF_EXIT=0`. After the
rebuild (EXIT=0), the same test passed, GREEN **EXIT=0**
(`logs/test-green-1.log`); the 8-test family also passed, EXIT=0
(`logs/test-green-2b.log`).

**ASK: the test is deterministic - no wall-clock timing, sleeps or races.**
`aFailedReplacementAfterStagingKeepsTheLiveProject` uses no sleeps, no timers
and no threads. It uses real files under the per-test temp fixture
(`buildFixture()`, `writeBytes`, `QFile::setPermissions`) and a deterministic
seam, `setRevisionReplacementFailureForTest(true/false)`, which forces the
single rename step to fail and is cleared immediately after the call. The
permissions change is restored by the `ReadableAgain` guard so it cannot
cascade. `aFailedReplaceKeepsTheLiveProject` needs no seam at all: it removes
revision 0 and passes a staging path that does not exist, so the failure is a
plain missing-file condition.

**ASK: the fix is at the cause and complete - every caller and sibling checked.**
`rg -n 'restoreProjectRevision|replaceProjectWithStaged' src include tests`
finds exactly these entry points to the fixed code:

- `src/core/RevisionTimeline.cpp:433` - `restoreProjectRevision` delegates to
  the replacement path at `:472`.
- `src/core/ControlCommandsProjectFiles.cpp:374`, `:394`, `:417` - project
  file restore/undo/redo all call `control::restoreProjectRevision`.
- `src/core/ControlCommandsRevisions.cpp:189`, `:224` - revisions undo/redo
  fall back to `control::restoreProjectRevision`.

There is no alternate copy/remove implementation: `replaceProjectWithStaged`
is the single implementation, and every caller reaches it through
`restoreProjectRevision` or directly. The revert copy on failure is no longer
needed because the live file is never removed; the fix therefore cannot be
half-applied at one call site.

**ASK: no collateral damage - old projects and saved data still load.**
This closure changes no product code - this job's only diff is this report;
the product code is inherited unchanged - so loading, rendering and save/load
behaviour are byte-for-byte what the branch already shipped. The fix itself only
reorders file operations on restore/replace and does not change the file format.
The 8-test revision family
(`RevisionTimelineTest`, `ProjectRevIdsTest`, `RevisionsDocsTest`, ...) passes
8/8, EXIT=0 (`logs/test-green-2b.log`); `feature_check.py` RESULT PASS, EXIT=0.

**ASK: verdict ALREADY-FIXED - the code or contract that shows it.**
`src/core/ProjectRevisions.cpp:465-481` (live file never removed; `stageCopy`
at 472; single `replaceWithStaged` at 474; staging removed only after success
at 479), the contract in `include/ProjectRevisions.h:94-100`, commit
`7f1af20295` with the delete-first parent body shown above, and the shipped
tests at `tests/src/core/RevisionTimelineTestParts1.h:326-346` and `:347-366`.

**ASK: sibling commands in the command files changed on the branch - confirm
each is consistent.**
The commands named by the check are `app.about`, `app.ask`, `app.help`,
`app.online_help`, `app.task_cancel`, `app.tasks`, `app.tour`, `app.tours`,
`app.update_check_get`, `app.update_check_run`, `app.update_check_set`,
`app.version`, `app.whats_new`, `arrangement.fit_tracks`,
`arrangement.get_state`. They come from earlier train commits on this branch,
not from this finding's diff, and none of them can reach the changed code: the
only references to `restoreProjectRevision` / `replaceProjectWithStaged` in
`src/`, `include/` and `tests/` are the five lines listed above (project-file
and revisions command families). The `app.*` (version/help/tour/update/task)
and `arrangement.*` handlers do not save, restore or replace project bytes, so
the reworked replace order cannot affect them; they need no change and remain
consistent.

## Other findings

- `aFailedReplaceKeepsTheLiveProject` also covers the no-revision-0 case, so a
  failure cannot be masked by a fallback copy even when no fallback exists.
