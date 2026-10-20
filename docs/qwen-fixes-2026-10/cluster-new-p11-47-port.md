# Cluster new-p11-47-port — revision undo/redo discarded file-operation failures

**Verdict: FIXED** — the bug was real on this branch, it is fixed, and the fix is
proven by a RED→GREEN regression (the reverted tree fails, the fixed tree passes).

## The finding

`src/core/ControlCommandsRevisions.cpp` recorded its inverse pair with closures that
dropped the `bool` result from the restore helpers, so an unsuccessful file restore
was not reported through the action: `control.undo` answered `undone: true` and
`control.redo` answered `redone: true` over a project file that was NOT put back.

The finding's line (`src/core/ControlCommandsRevisions.cpp:296-307`, historical) is the
recorded pair of `revisions.restore`; the defect was still open there when this round
started (`git show ded355440:src/core/ControlCommandsRevisions.cpp`).

## The fix

A side channel with the discipline the device channel already keeps — clear before
the step runs, read after, so the reply describes exactly the step that just ran.

- `include/ControlReversibility.h:525-530` + `src/core/ControlReversibility.cpp:177-191`:
  `recordActionRestoreFailure()` / `takeActionRestoreFailure()` / `clearActionRestoreFailure()`,
  declared beside `addUndoStep()`.
- `src/core/ControlCommandsRevisions.cpp:147-220`: the two recorded halves are the named
  `undoRestoreStep()` / `redoRestoreStep()`, and each records the failure its helper
  reported (including a failed removal of a restore-created file, even a dangling symlink).
- `src/core/ControlCommandsControl.cpp:327-337,352,360,570,577`: `actionRestoreFailureOf()`
  wraps the record into the typed failure `ControlErrorKind::Refused`; `control.undo` and
  `control.redo` clear before the journal step and read after.
- `src/core/ControlCommandsProjectFiles.cpp:369-399` (this round): the SIBLING path into the
  same helper — `project.restore_revision` records its own undo step — now reports its file
  operation the same way. Before this round it discarded the result exactly like the
  finding describes (`QString ignored; control::restoreProjectRevision(target, 0, &ignored);`)
  and its redo half returned silently when the selected bytes were no longer retained.

## Evidence table

All logs are in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-47-port.r4/logs/`.
`ctest` was run from `build/tests` only.

| Step | Command | Exit | Result |
|---|---|---|---|
| baseline build | `cmake --build build --parallel 12 --target zene RevisionTimelineTest` | 0 | built (`build-1.log`) |
| baseline run | `ctest -R RevisionTimelineTest --output-on-failure` | 0 | 1/1 target passed (`test-1.log`) |
| RED build (new sibling regressions, product code untouched) | `cmake --build build --parallel 12 --target RevisionTimelineTest` | 0 | built (`build-2.log`) |
| RED run | `ctest -R RevisionTimelineTest --output-on-failure` | 8 | `Totals: 28 passed, 2 failed` — both new sibling tests FAIL (`test-2.log`) |
| fix build | `cmake --build build --parallel 12 --target zene RevisionTimelineTest` | 0 | built (`build-3.log`) |
| GREEN run | `ctest -R RevisionTimelineTest --output-on-failure` | 0 | 1/1 target passed (`test-3.log`) |
| patch (product files only) | `git diff ded355440 -- include/ControlReversibility.h src/core/ControlReversibility.cpp src/core/ControlCommandsControl.cpp src/core/ControlCommandsRevisions.cpp src/core/ControlCommandsProjectFiles.cpp > …/logs/fix.patch` | 0 | 299-line patch |
| reverse product diff | `git apply -R …/logs/fix.patch` | 0 | tree back to the pre-fix state |
| no-fix build | `cmake --build build --parallel 12 --target zene RevisionTimelineTest` | 0 | built (`build-4-nofix.log`) |
| no-fix run | `ctest -R RevisionTimelineTest --output-on-failure` | 8 | `Totals: 24 passed, 6 failed` — the four `revisions.*` regressions AND the two sibling regressions FAIL (`test-4-nofix.log`) |
| re-apply | `git apply …/logs/fix.patch` | 0 | fixed tree restored |
| refix build | `cmake --build build --parallel 12 --target zene RevisionTimelineTest` | 0 | built (`build-5-refix.log`) |
| refix run | `ctest -R RevisionTimelineTest --output-on-failure` | 0 | 1/1 target passed (`test-5-refix.log`) |
| family build | `cmake --build build --parallel 12 --target ReversibilityContractTest ReversibilityUndoTest ReversibilityUndoDeviceTest UndoHonestyTest UndoHonestyStateTest UndoBoundsTest ProjectRevIdsTest ControlSuccessPathsTest ProjectStateTest ProjectToolsTest` | 0 | built (`build-6-family.log`) |
| family run | `ctest -R "RevisionTimelineTest\|ReversibilityContractTest\|ReversibilityUndoTest\|ReversibilityUndoDeviceTest\|UndoHonestyTest\|UndoHonestyStateTest\|UndoBoundsTest\|ProjectRevIdsTest\|ControlSuccessPathsTest\|ProjectStateTest\|ProjectToolsTest" --output-on-failure` | 0 | **11/11 targets passed** (`test-6-family.log`) |
| gates (all) | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 0 | `RESULT: PASS` — fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length all PASS (`gate-feature-check.log`) |
| complexity ratchet | `bash tests/complexity-gate.sh --check` | 1 | red on the train: 6 items, none in a file this branch changed (`gate-complexity.log`) |
| file-length ratchet | `bash tests/file-length-gate.sh --check` | 1 | red on the train: 14 items, the only one in a changed file is `tests/src/core/RevisionTimelineTest.cpp`, which was ALREADY over the limit at the base commit (`gate-file-length.log`) |

Affected-family test count: the `RevisionTimelineTest` target holds **28 Qt test
functions** (30 with `initTestCase`/`cleanupTestCase`); the wider family run above
covers **11 ctest targets**, all green.

## The two ratchet gates, item by item

Both gates are red on this train before the change; `feature_check.py` compares their
item lists with the same gates run on the branch's base (`ded355440`) and reports
"no new item in your files" for both.

- complexity: 6 items, e.g. `lmms::control::rotateProjectRevision@src/core/ProjectRevisions.cpp`
  (a file this branch does NOT change) — every item is in the base listing.
- file-length: 14 items; the only one in a file this branch changes is
  `tests/src/core/RevisionTimelineTest.cpp` (`new file over 500 lines`) — the SAME item
  is in the base listing (the file was already 600 code lines at `ded355440`), so this
  branch introduces no new violation. The limit and the baseline were not touched.

## Self-check

**1. The regression test FAILS with the product change reverted and PASSES with it.**
Reverted (`git apply -R …/logs/fix.patch`, exit 0): `ctest -R RevisionTimelineTest` →
**EXIT 8**, `Totals: 24 passed, 6 failed` — `undoOfARestoreThatCannotPutTheFileBackFailsTyped`,
`redoOfARestoreWhoseRevisionIsGoneFailsTyped`, `undoOfRestoreCreatedFileThatCannotBeRemovedFailsTyped`,
`redoOfARestoreWhoseRotationBytesAreGoneFailsTyped`,
`undoOfAProjectRestoreRevisionThatCannotPutTheFileBackFailsTyped` and
`redoOfAProjectRestoreRevisionWhoseRotationBytesAreGoneFailsTyped` all fail with
`control.undo answered 'undone: true'` / `control.redo answered 'redone: true'`.
Re-applied (`git apply …/logs/fix.patch`, exit 0): the same command → **EXIT 0**,
1/1 target passed. The fix.patch holds the five PRODUCT files only (the test file is
not in it), so the proof is about the product change alone.

**2. The test is deterministic.**
No wall-clock timing, no sleeps, no threads, no polling. The failure is produced by the
filesystem state the test sets up itself: the artefact the recorded inverse reads is
removed (`QFile::remove(projectRevisionPath(m_project, 0))`) and the retained copies of
the restored bytes are removed by a bounded loop over revisions 0..2, so the restore
helper's own `bool` result is the seam under test. The fixture is real files in a
`QTemporaryDir`; the timestamp the timeline reports comes from the fixture's own
`savedUTC` sidecar (`2026-09-15T10:00:00Z`), not from the clock. The commands are driven
through `ControlRegistry::invoke()` — the path a socket client takes — after
`ControlRegistry::setReady(true)`.

**3. The fix is at the cause and complete.**
The cause is "a recorded ACTION step's file operation fails and the void closure has no
way to tell the caller". The fix is the channel every such step can report through
(`control::recordActionRestoreFailure()`), read by the two commands that unwind a step
(`control.undo`, `control.redo`) and wrapped into the typed failure
`ControlErrorKind::Refused` by `actionRestoreFailureOf()`.

- Callers of the channel: `src/core/ControlCommandsRevisions.cpp` (undo/redo halves of
  `revisions.restore`) and `src/core/ControlCommandsProjectFiles.cpp` (undo/redo halves of
  `project.restore_revision`) — the two commands that restore a project revision and
  therefore the two that can fail a file operation.
- The other path into the same helper: `control::restoreProjectRevision()` is called by
  `revisions.restore` (`src/core/ControlCommandsRevisions.cpp:152,194`), by
  `project.restore_revision` (`src/core/ControlCommandsProjectFiles.cpp:353,372,395`) and
  by the timeline helper `control::restoreTimelineRevision()`
  (`src/core/RevisionTimeline.cpp:399`). The command path reports the failure in its own
  reply (`ControlErrorKind::Refused` / `NotFound`); the two RECORDED steps report it
  through the channel. All three paths are covered.
- The wider type the value flows into: `ControlErrorKind::Refused` — the same kind the
  restore itself answers (`src/core/ControlCommandsRevisions.cpp:410`), so an undo that
  could not restore is refused like the restore that could not.
- The discipline that keeps the channel honest: `control.undo`
  (`src/core/ControlCommandsControl.cpp:352`) and `control.redo`
  (`src/core/ControlCommandsControl.cpp:570`) CLEAR the record before they unwind a step
  and READ it after (`:360`, `:577`), so a failure recorded by a GUI Ctrl+Z (which does
  not go through the command) can never be attributed to a later undo, and the read
  consumes the record, so one failure is reported once. `undoThroughJournal()` is the one
  path `control.undo` takes for a journalled step (`src/core/ControlCommandsControl.cpp:445`),
  and the inverse-command path (`undoByCommand()`, `src/core/ControlCommandsControl.cpp:385-403`,
  called at `:508`) dispatches a command instead of running a closure, so it records no
  failure and reads none.

**4. No collateral damage.**
- Old projects and saved data: nothing in the product reads or writes a project document
  differently — the change touches only what happens AFTER a restore, when the restore's
  own file operation failed. The restored bytes, the rotation set and the saved project
  are untouched by the fix; the family run (11/11, incl. `ProjectStateTest`,
  `ProjectToolsTest`, `ProjectRevIdsTest`, `ReversibilityContractTest`) is green.
- No limit, cap or check was added: the fix adds a report channel, not a threshold.
  The 8 MiB per-revision cap, the keep-3 policy and every refusal that already existed
  are unchanged.
- The undo stack: one command is still ONE step — the fix adds no step and merges none;
  the recorded pair of each restore stays one undo step.
- The audio thread: nothing here runs on it. The channel is a UI-thread string
  (`src/core/ControlReversibility.cpp:179`), written and read by control commands only.
- No new control command: the command set is unchanged (no id was added, removed or
  renamed), so the command snapshot canary is untouched.

**5. Verdict ALREADY-FIXED / FALSE-POSITIVE?**
Not applicable — the verdict is FIXED, so the evidence is the code that was broken and
the code that fixes it: the reverted tree fails the six regressions above (exit 8) and
the fixed tree passes (exit 0). At the branch's base the defect is visible in
`src/core/ControlCommandsRevisions.cpp` (the closures that dropped the `bool` result)
and in `src/core/ControlCommandsProjectFiles.cpp:364-378` (the sibling that dropped it
too).

**6. Sibling commands in the command files this branch changed.**

- `revisions.list` (`src/core/ControlCommandsRevisions.cpp:226-268`): READ-ONLY
  (`cmd.mutating = false`), it lists artefacts and writes nothing, so it records no
  undo step and can fail no file operation. Unaffected — and it is what the fix's
  message names as the command that reports the retained set.
- `revisions.compare` (`src/core/ControlCommandsRevisions.cpp:274-336`): READ-ONLY
  (`cmd.mutating = false`), it reads two documents and counts elements. Unaffected.
- `revisions.restore` (`src/core/ControlCommandsRevisions.cpp:342-468`): the writer this
  finding is about — its recorded undo/redo halves now report their file operation
  (`src/core/ControlCommandsRevisions.cpp:147-220`).
- `project.restore_revision` (`src/core/ControlCommandsProjectFiles.cpp:309-403`): the
  SIBLING writer, in the file this round changed. It restores a revision through the
  same helper and records its own undo step, so its failure is reported the same way
  (`src/core/ControlCommandsProjectFiles.cpp:369-399`); before this round it discarded
  the result. Covered by the two new regressions.
- `project.save` (`src/core/ControlCommandsProjectFiles.cpp:139-301`): the rotation
  writer. It reports ITS OWN rotation refusal in its own reply (`revision_skipped` /
  a typed refusal) and records NO undo step at all: its transaction names the inverse as
  the COMMAND `project.restore_revision` with `applies: "command"`
  (`src/core/ControlCommandsProjectFiles.cpp:107-112`), so control.undo DISPATCHES that
  command and its own reply carries any refusal — there is no recorded closure whose
  result could be discarded. Unaffected by the fix.
- `project.get_state` (`src/core/ControlCommandsProjectFiles.cpp:410-...`) and
  `project.open`: READ-ONLY inspectors. Unaffected.

## Files committed

- `src/core/ControlCommandsProjectFiles.cpp` (fork-new) — the sibling's recorded halves
  report their file operation.
- `tests/src/core/RevisionTimelineTest.cpp` — two regressions for the sibling
  (`undoOfAProjectRestoreRevisionThatCannotPutTheFileBackFailsTyped`,
  `redoOfAProjectRestoreRevisionWhoseRotationBytesAreGoneFailsTyped`) plus the four
  from the earlier rounds of this cluster.
- `docs/qwen-fixes-2026-10/cluster-new-p11-47-port.md` — this report.

Committed earlier in this cluster (already on the branch):
`src/core/ControlCommandsRevisions.cpp`, `src/core/ControlReversibility.cpp`,
`include/ControlReversibility.h`, `src/core/ControlCommandsControl.cpp`,
`tests/upstream-modifications.txt`.
