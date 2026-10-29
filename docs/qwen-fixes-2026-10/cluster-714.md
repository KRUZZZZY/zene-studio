FIXED

# Cluster 714 - An empty revision can replace a valid project (integration onto 050/m0-train-22)

Integration job, not a re-implementation. Accepted patch: `bugs/qwen-714` @ `bda8f5052f20735fb5bac71ff660f8502b1c99ae`
(which already contains the round-1 fix `d9c29620fe` and the QA correction `bda8f5052f`). Target: `050/m0-train-22` @
`4ee5321ffc929edc8260b4764ffe3fec5ed0ff54`. Branch: `integration/714-4ee5321ffc`.

## Verdict: REAL, integrated and fixed

The defect and the accepted fix are unchanged from the accepted branch. A zero-byte artefact reads back as success with
no bytes (`src/core/RevisionTimeline.cpp:396-403`, `readFileBytes()` at `:161-183`), and `readRevisionBytes()` only fails
when `error` is set. Restore then staged the empty bytes, rotated the live project into the keep-3 set and installed the
empty file over it. The same gap exists on the rotation path via `control::restoreProjectRevision()`.

## The conflict, and the resolution

The driver applied the accepted patch three-way. Three of the four files applied cleanly
(`src/core/RevisionTimeline.cpp`, `tests/src/core/RevisionTimelineTest.cpp`, `docs/.../cluster-714.md`). One conflict, in
`src/core/ProjectRevisions.cpp`, because this train grew `stageRestoreTarget()` (cluster new-p12-185): the restore's
staging is now a helper that checks the per-revision cap on the bytes that would be installed, REUSES a pending stage
left by an earlier failed attempt, and only then makes a fresh copy. The accepted patch added its own emptiness refusal
around the older inline staging block, so both sides changed the same region.

Resolution (new design NOT needed; the accepted semantics are kept and the train's helper is kept):

- `src/core/ProjectRevisions.cpp:168-176` - `emptyRevisionRefusal()`: the accepted refusal message, extracted to one
  helper so the "empty" wording a client sees is identical on both paths.
- `:178-207` - `copyNonEmptyRevision()`: refuses a zero-byte source BEFORE copying it (the accepted QA correction), and
  additionally checks the STAGED bytes after a successful copy, removing the stage and refusing if it is empty. That
  keeps the accepted patch's "check the bytes that would be installed" property for a source that is not a regular file
  (a FIFO or device copies as zero bytes without being empty by `isFile()`).
- `:220-252` - `stageRestoreTarget()`: keeps the train's cap check first (unchanged), then refuses and REMOVES a
  zero-byte pending stage, then stages a fresh copy through `copyNonEmptyRevision()`. Both orderings are pre-rotation,
  so the live file is still never rotated out for a restore that would install nothing.
- `:432-434` - `restoreProjectRevision()` calls the helper, with the accepted patch's comment.
- `src/core/RevisionTimeline.cpp` - accepted patch applied unchanged: `readRestorableBytes()` at `:243-251`, called at
  `:438`. No semantic difference from the accepted patch.

No header change, no new file, no new control command, no cap change, no registration change. `revisions.compare`
(`src/core/ControlCommandsRevisions.cpp:316-321`) still calls `readRevisionBytes()` unchanged, so comparing against an
empty revision still works. `rotateProjectRevision`, save and the rotation set are untouched.

## Affected failure paths and platform hazards

| Path | Caller | Behaviour now |
| --- | --- | --- |
| timeline source (`backup`, `autosave`, `autosave_prev`, `git:<sha>`) | `revisions.restore` -> `restoreTimelineRevision` | typed Refused before anything is staged; live file and undo history unchanged |
| rotation id `rev0`..`rev2`, fresh empty slot | `revisions.restore` -> `restoreProjectRevision` -> `stageRestoreTarget` | typed Refused before the rotation; nothing staged |
| rotation id, empty PENDING stage from an earlier attempt | same | typed Refused; the empty stage is removed so a retry cannot install it |
| rotation id over `MaxRevisionBytes` (fresh or pending) | same | still refused by the train's cap check, BEFORE the empty check and before any copy - unchanged |
| rotation id exactly at the cap | same | still restores (existing `aRevisionExactlyAtTheCapStillRestores` passes) |
| undo/redo of a restore (`undoRestoreStep` `:189`, `redoRestoreStep` `:224`) | `restoreProjectRevision` | refused typed when the revision to install is empty; reported through the action-checkpoint failure path |
| `project.restore` (`ControlCommandsProjectFiles.cpp:374, 394, 417`) | `restoreProjectRevision` | refused typed when the revision is empty |
| `revisions.compare` | `readRevisionBytes` | unchanged on purpose |

Platform hazards checked, not by running them: the new code is plain `QFileInfo`/`QFile::remove` with no
platform-specific call, no path encoding, no rename, no `std::filesystem`, so the Windows wide-path and macOS
`std::filesystem` deployment-target concerns of `replaceWithStaged()` are untouched. No audio-thread code: the whole
change is in control-command file I/O, off the render path; nothing new allocates/locks/logs per period. Undo stays the
one existing action checkpoint - the refusal happens before the checkpoint is recorded
(`ControlCommandsRevisions.cpp:400-407`), so a refused restore adds no undo step.

## Regression tests

- `tests/src/core/RevisionTimelineTest.cpp:690` - `anEmptyRevisionIsRefusedAndTheLiveProjectKept()` (from the accepted
  patch, unchanged). Writes a zero-byte `backup`, `autosave` and `rev0`, asserts each `revisions.restore` is Refused with
  "empty" in the message and the live bytes and directory listing are unchanged, then calls
  `restoreProjectRevision(rev0)` directly. No timing, sleeps or threads.
- `tests/src/core/ProjectRevisionRestoreStagingTest.cpp:246` - `anEmptyPendingStageIsRefusedAndTheLiveProjectKept()`
  (added here). The train's `stageRestoreTarget()` REUSES pending stages, so this is the sibling path the accepted
  branch did not have: it writes an empty legacy `.restore` stage and an empty unique `.restore.<uuid>` stage in turn,
  and asserts the restore is refused with "empty", the live file and revision 0 keep their bytes, and the empty stage is
  removed. No timing, sleeps or threads.

## Evidence (all commands run from the worktree; unpiped EXIT codes)

Logs: the designated LOG FOLDER `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/714.r2/logs` is NOT writable in
this sandbox (`touch .../logs/probe` -> `Permission denied`, EXIT 1; the same denial hits every
`~/.local/share/zene-bunny-fix/qwen/runs/*` directory, and `/home/kruzzzzy/Documents/...` and `/var/tmp`; only `$TMPDIR`
and the build tree are writable). All logs below are therefore in
`$TMPDIR/714.r2-logs` = `/home/kruzzzzy/.cache/zene-tmp/zene-integrate-714-4ee5321ffc/714.r2-logs`, which is on disk
(`$TMPDIR` is under `~/.cache`). This is an environment limitation, not a missing step.

| Step | Command | EXIT | Result |
| --- | --- | --- | --- |
| build | `CCACHE_TEMPDIR=$TMPDIR/ccache-714 /usr/bin/cmake --build build --target zene RevisionTimelineTest -j4` (`build-1.log`) | 0 | both targets built; no C++ warnings/errors in the log |
| GREEN (accepted test, integrated code) | `cd build/tests && ctest -R '^RevisionTimelineTest$' --output-on-failure` (`test-1.log`) | 0 | 1/1 passed |
| 4b RED, both product files | `git diff HEAD -- src/core/RevisionTimeline.cpp src/core/ProjectRevisions.cpp > $TMPDIR/714.r2-logs/fix.patch` then `git apply -R fix.patch`; rebuild; `ctest -R 'RevisionTimelineTest|ProjectRevisionRestoreStagingTest'` (`build-2-red.log`, `test-2-red.log`) | 8 | `RevisionTimelineTest`: `FAIL! ... (restoring the empty 'backup' succeeded)`; 0/1 passed |
| 4b RED, rotation sibling only | `git diff HEAD -- src/core/ProjectRevisions.cpp > fix-rotation-only.patch`; `git apply -R`; rebuild `ProjectRevisionRestoreStagingTest`; same ctest (`build-7-rot-red.log`, `test-7-rot-red.log`) | 8 | `FAIL! : anEmptyPendingStageIsRefusedAndTheLiveProjectKept() '!restoreProjectRevision(project, 0, &error)' returned FALSE. ()` |
| 4b GREEN, final patch | `git apply fix.patch`; rebuild `zene RevisionTimelineTest ProjectRevisionRestoreStagingTest ReversibilityUndoTest RevisionRestoreCreatedFileUndoTest`; same ctest family (`build-10-4b-green.log`, `test-10-4b-green.log`) | 0 | 4/4 passed |
| 4b RED, final patch (both tests) | reverse the final `fix.patch` (140 lines), rebuild, `ctest -R 'RevisionTimelineTest|ProjectRevisionRestoreStagingTest'` (`build-9-4b-red.log`, `test-9-4b-red.log`) | 8 | BOTH regressions fail: `RevisionTimelineTest` at empty `backup`, `ProjectRevisionRestoreStagingTest` at the empty pending stage; 0/2 passed |
| affected family GREEN | `ctest -R 'RevisionTimelineTest|ProjectRevisionRestoreStagingTest|ReversibilityUndoTest|RevisionRestoreCreatedFileUndoTest' --output-on-failure` (`test-8-family-final.log`) | 0 | 4 targets, all passed: ReversibilityUndoTest, RevisionTimelineTest, ProjectRevisionRestoreStagingTest, RevisionRestoreCreatedFileUndoTest |

Note on the first family run (`test-4-family.log`, EXIT 8): the three sibling binaries had never been built in this
build tree, so ctest reported them "Not Run". They were built explicitly (`build-5-siblings.log`, EXIT 0) and the
identical command then passed (`test-5-family.log`, EXIT 0). No test failed.

## Gates

| Gate | Command | EXIT | Result |
| --- | --- | --- | --- |
| as written | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (`gate/feature-check-direct.log`) | 1 | no check ran: `PermissionError ... mkdir '<worktree>-feature-check'` - the script creates its log directory beside the worktree, which is outside the writable area of this sandbox |
| with only the log dir redirected | `python3 $TMPDIR/714.r2-logs/gate/feature_check_logdir.py <worktree>` (`gate/feature-check-1.log`, `gate/feature-check-2-final.log`) | 0 | all 8 rows PASS: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity ("no new item in your files"), file-length ("no new item in your files") - `RESULT: PASS` |

`feature_check_logdir.py` is a copy of `feature_check.py` with exactly two lines changed: `logs = ...` points into
`$TMPDIR/714.r2-logs/gate/logs`, and `ROOT` (which resolves `FIX-BASE`, `gate-baselines` and the sibling checkout) points
at the original qwen directory. Gate rules, the cached `4ee5321ffc...` base listing and the pass/fail logic are
unchanged. The base listing for `4ee5321ffc929edc8260b4764ffe3fec5ed0ff54` was already present.

## Supported platforms

Only Linux was built and run (this machine). Reviewed for portability: no new API, no
`QString::midRef`/`std::filesystem`/wide-path code, `QFileInfo::size()` is `qint64` on every platform, and the new
`emptyRevisionRefusal()` returns `QString` by value. The Windows-wide-path and macOS tests already in
`ProjectRevisionRestoreStagingTest` are untouched and still compile under their `#ifdef Q_OS_WIN` guard.

## Committed files

- `src/core/ProjectRevisions.cpp` (conflict resolution; both helpers; call site)
- `src/core/RevisionTimeline.cpp` (accepted patch, applied cleanly)
- `tests/src/core/RevisionTimelineTest.cpp` (accepted regression test, applied cleanly)
- `tests/src/core/ProjectRevisionRestoreStagingTest.cpp` (pending-empty-stage regression, added for the train's reused-stage path)
- `docs/qwen-fixes-2026-10/cluster-714.md` (this report)

## Self-check

- **The accepted design is retained.** `readRestorableBytes()` (timeline) and the typed "empty" refusal wording are
  byte-identical to the accepted branch; the rotation path still refuses before staging and before the rotation; the
  cap check from this train is kept ahead of it, and a pending stage is still reused. Verified by
  `git diff bda8f5052f -- src/core/RevisionTimeline.cpp` being empty and by the table above.
- **Regression fails without the product change and passes with it.** `test-2-red.log` / `test-9-4b-red.log` EXIT 8
  against the reverted product files; `test-1.log`, `test-10-4b-green.log`, `test-8-family-final.log` EXIT 0 after
  re-applying. Both new tests fail independently: the rotation-only reversal (`test-7-rot-red.log`, EXIT 8) leaves the
  timeline test green and fails only the staging test.
- **Deterministic.** No clock, sleep or thread. Fixed byte fixtures (`documentWith(n)`) in the test's own
  `QTemporaryDir`; zero-byte files are written explicitly; the recovery path is pinned by the existing
  `ZENE_RECOVERY_FILE` override. The "nothing changed" checks compare bytes and sorted directory listings.
- **At the cause and complete.** The cause is accepting zero bytes as a restorable revision. Fixed at
  `readRestorableBytes()` (timeline sources, git included) and at `stageRestoreTarget()`/`copyNonEmptyRevision()`
  (rotation slot and pending stage). Callers are enumerated in the failure-path table.
- **No collateral damage.** No header, cap, command or registration change. A revision of exactly
  `MaxRevisionBytes` still restores (`aRevisionExactlyAtTheCapStillRestores`), an over-cap revision or pending stage is
  still refused by the train's unchanged check (`anOversizedRevisionIsRefusedBeforeTheRetainedSetMoves`,
  `anOversizedPendingStageIsNotInstalled`), and a valid pending stage is still consumed on retry
  (`failedRestoreRetryUsesPendingStagedTarget`). `revisions.compare` and save/rotation are unchanged.
  The one behaviour change carried over from the accepted patch: an undo of a restore whose replaced live file was
  zero bytes is now refused and reported instead of silently putting the empty file back.
- **Realtime / undo / commands.** No audio-thread code touched; the refusal runs before the action checkpoint, so undo
  stays one honest step and a refusal records none. No new control command; `commands_snapshot.json` is untouched.
- **Registration.** Both test files are already registered in `tests/fork-sources.txt` (lines for
  `tests/src/core/ProjectRevisionRestoreStagingTest.cpp` and `tests/src/core/RevisionTimelineTest.cpp`); no new product
  file was added, so `tests/fork-sources.txt` needs no edit and `tests/upstream-modifications.txt` needs none for the
  changed inherited sources (the `fork-sources` gate passes, EXIT 0).
- **Gates.** All 8 rows PASS (EXIT 0) with only the gate's own log path redirected; the unmodified script cannot run in
  this sandbox because it writes beside the worktree (`gate/feature-check-direct.log`, EXIT 1, PermissionError). The
  earlier round-1 note about the same limitation still applies, unchanged by this job.
