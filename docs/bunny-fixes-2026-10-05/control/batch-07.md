# Control batch-07 — Luna findings (2026-10-05)

Branch `bugs/bunny-control-2026-10-05`. Six findings, all verified against current code.

### Cluster 5000 — Undoing media collection leaves its copied files behind

Verdict: FALSE-POSITIVE

Evidence: `src/core/ControlReversibilityTableArchive.cpp:81-85` classifies
`project.collect_media` `TrueInverse` and its mechanism column states outright that the recorded
step "writes the file's previous bytes back (the copies stay on disk)". The handler agrees:
`src/core/ControlCommandsCollectMedia.cpp:161-172,198-205` restores only the project file's
previous bytes and the description repeats that the copies remain. The row is accurate and
deliberately scoped to project state; deleting copies on undo would be a new design (and could
remove files the user has since edited), which is outside this lane.

Commit: none

Test: none

Red/green: none

### Cluster 5003 — Range consolidation creates multiple undo steps for one command

Verdict: ALREADY-FIXED

Evidence: `src/core/ControlReversibilityTableArranger.cpp:83-86` (row), the per-track
`clip.consolidate` invocation at `src/core/ControlCommandsRange.cpp:358-372`, and the merging in
`src/core/ControlRegistry.cpp:333,347` (`journal->mergeCheckpointsFrom(mark)`) implemented at
`src/core/ProjectJournal.cpp:290-330`: the window collapses into one step, keeping the earliest
capture per object and all actions. Together with `recordsItsOwnStep`
(`src/core/ControlRegistry.cpp:311-319`) and commit `4a01e6dd0`, nested per-track steps from one
`range.consolidate` are undone by a single undo. The finding describes pre-`4a01e6dd0` behaviour.

Commit: none (already fixed by `4a01e6dd0`)

Test: none

Red/green: none

### Cluster 5002 — Arrangement nudge is advertised as non-mutating despite moving a clip

Verdict: FALSE-POSITIVE

Evidence: `src/core/ControlReversibilityTableArranger.cpp:87-89` states the command runs
`clip.move`, which "records itself; nothing of its own is written", and the handler
(`src/gui/ControlCommandsArrangerView.cpp:333-350`) plus its non-mutating registration
(`:423-426`) match. The undo is the nested `clip.move` Clip checkpoint; `control.undo` falls back
to the engine journal when a command has no step of its own
(`src/core/ControlCommandsControl.cpp:299-347`). The contract describes the command accurately —
it writes no project state beyond the child command's recorded step.

Commit: none

Test: none

Red/green: none

### Cluster 5001 — Persistent arranger preferences are classified as non-mutating

Verdict: FALSE-POSITIVE

Evidence: `src/core/ControlReversibilityTableArranger.cpp:95-105` covers
`arrangement.modifier_tools`, `arrangement.navigator`, `arrangement.ruler_lanes` and
`arrangement.modes`; each row names the config-file write, classifies it as "no write to the
project, and the config write is not journalled", and gives the manual fallback ("set the previous
bindings" / "set it back" / "set the previous lanes" / "set the previous modes"). The finding's
claim that the rows "provide no undo classification or fallback" is contradicted by the rows.
Journalling user preferences would also be a design change, and config writes are deliberately
outside the project journal.

Commit: none

Test: none

Red/green: none

### Cluster 3039 — `macro.run` can create many undo steps while classified as read-only

Verdict: FALSE-POSITIVE

Evidence: no `macro.run` command exists in the current tree (`rg -n -l 'macro\.run' src tests docs`
returns nothing). The recorded-command feature was renamed to workflows
(`src/core/ControlReversibilityTableCustomisation.cpp:99`: "new in 0.5.0, never shipped as
macro.*"), and `workflow.run` at `:107-111` is `NotMutating` with the per-step semantics the
`mastering.run` async-ACK design requires — a known-by-design case. The finding targets a command
name that was never shipped.

Commit: none

Test: none

Red/green: none

### Cluster 1313 — `plugin.ab` does not describe an inverse for storing a slot

Verdict: REAL-UNTESTED

Evidence: the row at `src/core/ControlReversibilityTableDevices.cpp:45-47` says a recall or toggle
puts stored settings on a device while "storing writes only the session's A/B slots", and claims a
`TrueInverse`. The store branch at `src/core/ControlCommandsDeviceCopy.cpp:334-338` calls
`storeSlot(...)` and sets `pair.onB` with no `addUndoStep`, unlike `recallState`
(`:277-288`) and `abRecall` (`:296-310`). Because `changedNothing()`
(`src/core/ControlRegistry.cpp:298-303`) suppresses the transaction for a `TrueInverse` row when no
step was journalled, an undo following a store can pop an unrelated earlier step instead of
restoring the slot. The defect looks genuine.

Not fixed here: this build tree has no loadable native effects (`DeviceCopyTest` runs with
"plugin-scan: 1 file(s) ... 1 plugin(s)" while 73 LV2 plugins load, so every effect-backed case
skips with "this build loads no effect"). A regression test for `plugin.ab` store/undo therefore
skips and cannot produce RED evidence; per the lane rules, no product code was changed without a
deterministic failing test. A candidate test was written and then reverted rather than committed
unrunnable.

Commit: none

Test: none (candidate `DeviceCopyTest::storedSlotsAreTakenBackByAnUndo` written, skipped, reverted)

Red/green: none. Environment probes:
`cmake --build build --target zene DeviceCopyTest > build/bunny-b.log 2>&1` EXIT=0;
`cd build/tests && ctest -R '^DeviceCopyTest$' --output-on-failure > build/bunny-t.log 2>&1`
EXIT=8 (pre-existing `aChainIsBypassedAndCleaned` fails and 9 of 18 cases SKIP with "this build
loads no effect"); `ctest -R '^DeviceToolsTest$'` EXIT=8 for the same reason.

Leftovers: cluster 1313 is an unfixed product defect (`plugin.ab` store leaves the slot changed
after an undo) awaiting a build tree with loadable native effects, or a test that can fake the
effect without one.