# cmds2 batch-06 — clusters 1175, 5737, 5738, 4822, 5739, 5740

Two real defects fixed, both in the `project.restore_revision` handler; four findings
closed as false positives against the source. Build tree: `build` (already configured).

### Cluster 1175 — Undoing a save does not restore the session's saved state

Verdict: SKIPPED-SCOPE

Evidence: `src/core/ControlCommandsProjectFiles.cpp:211` (`song->noteProjectSaved(target)`)
sets the session's file name and modified flag after a successful write, while the recorded
inverse at `src/core/ControlCommandsProjectFiles.cpp:98` is a dispatched COMMAND,
`project.restore_revision {path, revision: 0}`, which rewrites the file and by contract never
touches the session — stated in the handler's own description
(`src/core/ControlCommandsProjectFiles.cpp:247`, "The session in memory is NOT reloaded") and
in `docs/A16-REVERSIBILITY.md:412` item 5. So the observation is true but it is a DOCUMENTED
limitation, not an accident. The only honest fix in the report's own terms — carrying the prior
filename and modified flag in the save's undo action — changes the recorded inverse payload that
`control.undo` dispatches, which is the snapshot-visible transaction shape (lane rule 8), and it
would have to contradict the no-reload contract `project.restore_revision` is documented under.
Both are the parent's call, so the code is left alone.

Commit: none

Test: none

Red/green: none

### Cluster 5737 — A failed revision restore still adds an undo step

Verdict: FIXED

Evidence: `src/core/ControlCommandsProjectFiles.cpp:294` — `addUndoStep()` was called BEFORE
`control::restoreProjectRevision()`, the call that can refuse (revision outside the keep-3
policy, or absent from the retained set). A refused restore therefore failed the command and
still pushed a step whose undo "restores" a file the command never touched, so the caller's next
`control.undo` spent itself on that phantom and pushed the real step one further from the
keyboard. Measured: asking for revision 1 of a file that holds only revision 0 left the stack at
depth 3 where it had been 2. The step is now registered after the restore has succeeded — the
same ordering commit `4c407b786` applied to `plugin.state_load`.

Commit: `7923f533d`

Test: `RevisionTimelineTest::aFailedRestoreRecordsNoUndoStep`

Red/green:
- `cmake --build build --target zene RevisionTimelineTest > build/bunny-b.log 2>&1; echo EXIT=$?` -> EXIT=0
- `cd build/tests && ctest -R '^RevisionTimelineTest$' --output-on-failure > build/bunny-t.log 2>&1; echo EXIT=$?` -> EXIT=8
  (`aFailedRestoreRecordsNoUndoStep`: actual depth 3, expected 2)
- same command after the fix -> EXIT=0 (full executable: 10 passed, 0 failed)
- `bash tests/complexity-gate.sh --check > build/bunny-cx.log 2>&1; echo EXIT=$?` -> EXIT=0
- `bash tests/file-length-gate.sh --check > build/bunny-fl.log 2>&1; echo EXIT=$?` -> EXIT=0

### Cluster 5738 — Redo can restore the wrong revision

Verdict: FIXED

Evidence: `src/core/ControlCommandsProjectFiles.cpp:305` — the recorded redo re-restored the
revision NUMBER the caller named, but every restore rotates the live file in, so `rev0 -> rev1 ->
rev2` shifts under it: after the undo, that number named a different file. Measured: restoring
revision 1, undoing it and redoing it left the project holding the PRE-restore bytes under a redo
that reported success. The redo now identifies its target by CONTENT — the sha256 of the bytes
the restore brought in is captured when the step is recorded, and the redo restores whichever
revision currently holds those bytes (`revisionHoldingBytes()`,
`src/core/ControlCommandsProjectFiles.cpp:59`). If the retained set no longer holds them it
restores nothing rather than overwriting the live file with a different revision's bytes. The
undo is unchanged: revision 0 after a restore is the file that restore replaced, by the rotation's
own definition.

Commit: `07ad796a2`

Test: `RevisionTimelineTest::aRedoRestoresTheBytesTheRestoreSelected`

Red/green:
- `cmake --build build --target zene RevisionTimelineTest > build/bunny-b.log 2>&1; echo EXIT=$?` -> EXIT=0
- `cd build/tests && ctest -R '^RevisionTimelineTest$' --output-on-failure > build/bunny-t.log 2>&1; echo EXIT=$?` -> EXIT=8
  (`aRedoRestoresTheBytesTheRestoreSelected`: live file held `documentWith(3)`, expected `documentWith(1)`)
- same command after the fix -> EXIT=0 (full executable: 11 passed, 0 failed)
- `cmake --build build --target ProvenanceSectionTest > build/bunny-b3.log 2>&1; echo EXIT=$?` -> EXIT=0
  (its ctest entry points at `build/tests/ProvenanceSectionTest`, which the tree had never
  built, so it reported "Not Run" until the target was built — a build-tree gap, not a regression)
- `cd build/tests && ctest -R '^(ProvenanceSectionTest|ControlRegistryTest)$' ... ; echo EXIT=$?` -> EXIT=0
- `cd build/tests && ctest -R '^(ControlRegistryTest|ReversibilityUndoTest)$' ... ; echo EXIT=$?` -> EXIT=8
  (`ReversibilityUndoTest::firstAutomationPointIsOneUndoableStep` is PRE-EXISTING on this branch,
  recorded in the batch-04 baseline; the other 11 cases pass)
- `bash tests/complexity-gate.sh --check > build/bunny-cx.log 2>&1; echo EXIT=$?` -> EXIT=0
- `bash tests/file-length-gate.sh --check > build/bunny-fl.log 2>&1; echo EXIT=$?` -> EXIT=0

### Cluster 4822 — C++17 build fails on defaulted equality operator

Verdict: FALSE-POSITIVE

Evidence: the project requires C++20 — `src/CMakeLists.txt:25` `SET(CMAKE_CXX_STANDARD 20)`
(the repo's own `docs/reports/ARCH-7-VERDICT.md:45` cites that same line). The defaulted
comparison at `src/core/ControlCommandsProjectInfo.cpp:67` is legal there, and the tree proves it
empirically: `cmake --build build --target zene` completed EXIT=0 during this batch with that
file in the build. C++17 complaints on this lane are false by the lane's standing rule as well.

Commit: none

Test: none

Red/green: none

### Cluster 5739 — `project.new` replaces the session without a transaction or undo step

Verdict: FALSE-POSITIVE

Evidence: `src/core/ControlReversibilityTableShell.cpp:47` already declares `project.new` as
`RC::Irreversible, reversible=false`, with the reason "a new project replaces the whole session,
exactly as project.open does, and the engine keeps no snapshot of what it replaced (unsaved edits
included)" and the fallback "save first (project.save), then reopen that file with
project.open". The handler's description repeats it
(`src/core/ControlCommandsProjectLifecycle.cpp:85`, "The previous session is gone (unsaved edits
included), exactly as project.open replaces it"), and `project.open` behaves identically and is
classified the same way (`ControlReversibilityTablePassive.cpp:79`). The command is honest about
having no inverse; snapshotting a whole displaced session is a product design change, not a
defect in this handler.

Commit: none

Test: none

Red/green: none

### Cluster 5740 — `project.import` changes the session without an undo step

Verdict: FALSE-POSITIVE

Evidence: `src/core/ControlReversibilityTableShell.cpp:57` declares `project.import` as
`RC::Irreversible, reversible=false`, reason "the import filters add tracks with the project
journal paused (ImportFilter::import), so there is no checkpoint for control.undo to apply", with
the fallback "save before importing, or remove the added tracks (the reply counts them) with
track.remove". The handler says the same in its own description
(`src/core/ControlCommandsProjectLifecycle.cpp:172`, "Not undoable: the filters write with the
project journal paused"). The report's second half — that undo may consume an unrelated step — is
a property of `control.undo` across the whole irreversible surface (`project.open`,
`project.new`, `project.save_version` all behave the same way), not of this command, so changing
it is out of this lane's scope.

Commit: none

Test: none

Red/green: none

Leftovers: none. Cluster 1175's session-state asymmetry is left as code, deliberately (rule 8 —
its fix changes the recorded inverse payload for `project.save` and contradicts the documented
no-reload contract of `project.restore_revision`; both are the parent's decision). One unrelated
build-tree gap was closed as a side effect: `ProvenanceSectionTest` is registered with ctest at
`build/tests/ProvenanceSectionTest`, which no build target produces, so it reported "Not Run"
until the target was built explicitly; it passes (EXIT=0) once built.