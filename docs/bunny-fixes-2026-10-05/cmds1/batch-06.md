# Space Bunny lane `cmds1` — batch 06

Six findings triaged on branch `bugs/bunny-cmds1-2026-10-05`: 2 FIXED, 1 FALSE-POSITIVE,
1 SKIPPED-SCOPE, 2 SKIPPED-SCOPE (contract-level design changes).

### Cluster 5612 — Media-folder containment checks can be bypassed by a symlink

Verdict: FIXED

Evidence: `src/core/ControlCommandsCollectMedia.cpp:71-74` — `insideFolder()` compared lexical
`absoluteFilePath()` values, and `mediaFolderName()` accepted a `media` entry on that basis. A
project whose `media` directory is a symlink to an outside library therefore passed containment and
`mkpath` plus the file copy wrote outside the project folder. Fixed with `resolvedPath()`
(`canonicalFilePath()`, falling back to the absolute path), an `insideFolder()` that resolves BOTH
sides, and a `mediaFolderIsContained()` check enforced AFTER `mkpath` in both `collectMedia()` and
`collectProjectMediaInto()` — the handoff path hard-codes `"media"` too, so it needed the same
guard. Refused with `ControlErrorKind::Refused`.

Commit: deab3b029

Test: ControlProjectArchiveTest::collectMediaRefusesAMediaFolderThatSymlinksOutOfTheProject

Red/green: `cmake --build build --target zene ControlProjectArchiveTest` EXIT=0.
`cd build/tests && ctest -R '^ControlProjectArchiveTest$'` EXIT=8 — `'!collected.ok' returned
FALSE. (a media folder that resolves outside the project must be refused)` at
`ControlProjectArchiveTest.cpp:423`. Same build EXIT=0, same ctest EXIT=0 (13 passed). Gates:
complexity EXIT=0, file-length EXIT=0.

### Cluster 1935 — Consolidation reads clip IDs through deleted pointers

Verdict: FALSE-POSITIVE

Evidence: `src/core/ControlCommandsConsolidate.cpp:216-217` — `sources` is captured from
`region.inside` through `control::clipIdOf(clip)` BEFORE `replaceWithConsolidated()` is called at
line 223, and that call is where the `removeClip`/`delete` loop runs (~lines 175-179).
`result.insert("consolidated", sources)` at line 233 then reuses the already-captured array. The
report's line numbers predate the current revision, in which the capture already happens before the
deletion; there is no dangling read.

Commit: none

Test: none

Red/green: none

### Cluster 5614 — `control.undo` can undo a newer GUI edit instead of the recorded command

Verdict: SKIPPED-SCOPE

Evidence: `src/core/ControlCommandsControl.cpp:380` — `undoLastCommand()` checks only eviction
(`top->step < journal->oldestStepSerial()`) and then unwinds the journal's current top, so a step a
human pushed after the recorded command can be taken back while the reply names the recorded
command. The hole is real. Every correct guard needs to tell a NEWER step pushed by a human or an
unrecording client from a NEWER step the control surface itself pushed — `control.redo` re-pushes a
step above the last recorded transaction, and coalesced runs legitimately leave a record naming a
serial ABOVE the top. Implemented and measured: a `record.step < journal->topStepSerial()` refusal
(passing `!=` first broke coalesced runs) took `ControlUndoStructuralTranscript` from passing to
failing — refusals at transcript ids 27 and 50 for `track.remove` (step 13 under top 14) and
`track.move` (step 17 under top 18), both after a `control.redo`. Distinguishing the two sources is
a journal-provenance design change, not a local guard, so the product code was reverted unchanged.

Commit: none

Test: none (the regression case written for it, `UndoHonestyTest::
undoRefusesWhenANewerStepSitsAboveTheRecordedOne`, was reverted with the product change)

Red/green: build EXIT=0; with the fix `ctest -R '^ControlUndoStructuralTranscript$'` EXIT=8 with the
two new refusals above; with it reverted the same test passes again.

### Cluster 1049 — `control.redo` cannot redo command-based inverses

Verdict: SKIPPED-SCOPE

Evidence: `src/core/ControlCommandsControl.cpp:470-477` — the `control.redo` handler replays only
`journal->redo()`, while `undoByCommand()` at lines 332-348 dispatches the recorded inverse command
(`applies == "command"`, as `crash.enable`/`crash.disable` record at
`src/core/ControlCommandsCrashControl.cpp:179-189, 241-247`). So an undone paired command has no
redo path. Fixing it means giving command-based inverses their own redo stack — recording the
forward command at undo time and replaying it — which is a new mechanism in `control.redo` and the
transaction record, well past a behaviour-preserving local change and into schema territory.

Commit: none

Test: none

Red/green: none

### Cluster 4786 — Duplicate control names select an arbitrary MIDI binding

Verdict: FIXED

Evidence: `src/core/ControlCommandsController.cpp:166-168` (pre-fix) — `findBoundControl` returned
the first bound control whose name matched, so two bound controls sharing a `fullDisplayName()`
silently sent the command to whichever came first. The collision is reachable: `Model::
fullDisplayName()` (`src/core/Model.cpp:57-66`) is the display name under the parent's, so two
identically named devices in one track answer to one name. `findBoundControl` already refuses an
empty `control` when more than one control is bound (`all.size() > 1`), so the same unanswerable
question asked by name is now refused too — typed, naming both bindings, changing nothing. The
ambiguity scan was split into `otherBoundWithName()` to hold `findBoundControl` at CCN <= 10.

Commit: 478415124

Test: ControllerSurfaceTest::DuplicateControlNamesAreRefusedNotResolvedToTheFirstBinding

Red/green: `cmake --build build --target zene ControllerSurfaceTest` EXIT=0.
`cd build/tests && ctest -R '^ControllerSurfaceTest$'` EXIT=8 — `'!result.ok' returned FALSE. (a
control name two bound controls answer to must be refused)`. Same build EXIT=0, same ctest EXIT=0
(12 passed). Also green: `ControllerSuccessPathsTest`, `MidiControllerMenuTest`, and the
`ControlCommandsSnapshot` canary. Gates: complexity EXIT=0, file-length EXIT=0.

### Cluster 3833 — Soft-takeover changes cannot be undone

Verdict: SKIPPED-SCOPE

Evidence: `src/core/ControlCommandsController.cpp:306-357` — the `controller.soft_takeover` handler
records `reversible: false` and an inverse that is NOT marked `applies: "command"`, so
`control.undo` refuses rather than replaying the captured `enabled`/`target`. That matches the
declared A16 contract exactly: `src/core/ControlReversibilityTableController.cpp:75-88` states the
row is `RC::Snapshot, false` because the ControllerConnection is not a JournallingObject, and
documents the manual `controller.soft_takeover` replay as the intended fallback. Turning the row
into a paired-command inverse the way `crash.enable` does would change the published reversibility
contract, not just the code path.

Commit: none

Test: none

Red/green: none

Leftovers: Cluster 5614's hole is real and unfixed — the guard it needs (journal step provenance,
so a human's newer step is distinguished from one `control.redo` pushed) is a design change for
the parent lane; the reverted attempt and its two transcript refusals are recorded above.