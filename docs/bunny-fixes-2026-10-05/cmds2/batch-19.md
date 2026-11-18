# Space Bunny batch 19 — lane `cmds2` (control commands ControlCommands[N-Z])

Worktree `zene-bunny-cmds2`, branch `bugs/bunny-cmds2-2026-10-05`.
Findings adjudicated: 6 — 3 FIXED (4 commits: one shared seam), 1 FALSE-POSITIVE
(locked by a test), 1 REAL-UNTESTED, 1 DUPLICATE (folded into cluster 4906).

### Cluster 4016 — Surface reports lose parent menu names in nested paths

Verdict: FIXED

Evidence: `collectActions` recursed into a submenu passing only
`containerLabel(submenu)` (`src/core/ControlCommandsSurface.cpp:181-182`), so
the parent's path was gone: an action under File → Render Presets → Default
Settings reported `container_path` "Render Presets". The gate keys on
`surface:container_path/text` (`tests/agent_surface_lib.py:92`) and exempts a
whole subtree with `container_path.startswith(value + "/")`
(`tests/agent_surface_lib.py:108`), so a truncated path names an action the
agent can neither find nor exempt. The submenu now extends the parent path
(`src/core/ControlCommandsSurface.cpp:179-188`); no schema changed, only the
value of an existing result field. No baseline moved:
`tests/agent-surface-baseline.txt` has no action lines, and the `path:Tools`
exemption is a top-level menu either way.

Commit: `44d5fe24d`

Test: `AgentSurfaceReportTest::nestedActionsKeepTheirWholeMenuPath`

Red/green: Red `cmake --build build --target zene AgentSurfaceReportTest` EXIT=0;
`cd build/tests && ctest -R '^AgentSurfaceReportTest$' --output-on-failure` EXIT=8
(`surface_report lost menu action "Default Settings" in "File/Render Presets"`).
Green `cmake --build build --target zene AgentSurfaceReportTest` EXIT=0;
`ctest -R '^AgentSurfaceReportTest$'` EXIT=0 (3 passed); `ctest -R '^agent_surface$'`
EXIT=0. Gates CX=0, FL=0.

### Cluster 4964 — Consent dialog reports saved after rejection

Verdict: REAL-UNTESTED

Evidence: `openTelemetryConsentScreen` calls `dialog.exec()` and drops its
return value, then hardcodes `result.insert("saved", true)`
(`src/core/ControlCommandsTelemetry.cpp:252-258`). The code's own comment
(`:254-255`) claims the result "reports the state rather than assuming the
human clicked Save", which the literal `true` contradicts. The wiring makes the
mismatch unfixable by reading `exec()`: `TelemetryConsentDialog::save()` only
saves and refreshes the preview (`src/gui/TelemetryConsentDialog.cpp:128-132`)
and is not the dialog's accept path — `accepted` connects to it and `rejected`
to `reject()` (`:88-89`), so both answers exit with `exec()` == 1 and the value
carries no information about whether Save ran.

Commit: none (no deterministic headless test exists — see Red/green)

Test: none

Red/green: none. The command declares `requires: display, human` and refuses an
unattended run with a typed `Requires` before the handler runs
(`src/core/ControlCommandsTelemetry.cpp:230-241`), so ctest — always unattended —
cannot reach line 252 at all, and the only way to reach it is a human dismissing
a real modal dialog. Per procedure step 2, no product change was made. The honest
fix (reading the consent store's before/after state, or having `save()` report
success) needs a decision this lane cannot make headlessly.

### Cluster 4905 — `tempo_map_add` advertises an incomplete inverse

Verdict: FIXED

Evidence: the handler advertised `tempo_map_remove` at the event's tick for
every add, including one that REPLACED an existing event
(`src/core/ControlCommandsTransportMap.cpp:332`) — reissuing that inverse
deletes the event rather than restoring its old contents. A replacing add now
publishes no inverse command and describes the recorded action checkpoint as
the way back; a plain add to an unoccupied tick keeps its `tempo_map_remove`
inverse, which `contractRowsClassifyTheGroup`
(`tests/src/core/ControlTempoMapCommandsTest.cpp`) requires.

Commit: `873124875`

Test: `ControlTempoMapCommandsTest::addInverseIsAdvertisedOnlyWhereItRestoresThePriorState`

Red/green: Red `cd build/tests && ctest -R '^ControlTempoMapCommandsTest$' --output-on-failure`
EXIT=8. Green `cmake --build build --target zene ControlTempoMapCommandsTest` EXIT=0;
`ctest -R '^ControlTempoMapCommandsTest$'` EXIT=0 (10 passed, including
`contractRowsClassifyTheGroup`). Gates CX=0, FL=0.

### Cluster 4906 — `tempo_map_remove` inverse can reactivate an inactive map

Verdict: FIXED

Evidence: the remove inverse re-added the event without `active:false` and
without `ramp` (`src/core/ControlCommandsTransportMap.cpp:379-400`); adding an
event defaults to activating the map, and `TempoMap::removeEvent`
(`src/core/TempoMap.cpp:177-187`) never touches the active flag. The inverse now
carries `ramp` (when the removed event had one) and the map's `active` state as
read before the removal. Both are already declared params of `tempo_map_add`, so
no schema moved.

Commit: `d945dae5e`

Test: `ControlTempoMapCommandsTest::removeInverseRebuildsRampAndInactiveState`

Red/green: Red `cd build/tests && ctest -R '^ControlTempoMapCommandsTest$' --output-on-failure`
EXIT=8 (`Actual 0 / Expected 1` — the ramp was lost). Green
`cmake --build build --target zene ControlTempoMapCommandsTest` EXIT=0;
`ctest -R '^ControlTempoMapCommandsTest$'` EXIT=0 (9 passed). Related
`TempoMapTest`, `TempoMapPersistenceTest`, `SmfInterchangeTest`,
`ControlRegistryTest` all EXIT=0. Gates CX=0, FL=0.

### Cluster 4000 — The tempo-map remove inverse drops ramp and active state

Verdict: DUPLICATE

Evidence: the same seam and the same lines as cluster 4906 — its report is a
superset ("drops ramp and active state") of what 4906 reported for
`src/core/ControlCommandsTransportMap.cpp:379-390`. One defect, one fix.

Commit: `d945dae5e` (shared with cluster 4906)

Test: `ControlTempoMapCommandsTest::removeInverseRebuildsRampAndInactiveState` (shared)

Red/green: as cluster 4906. Splitting it would have meant a second commit with
no source change.

### Cluster 4907 — `control.set_undo_depth` reports an undo step it never records

Verdict: FALSE-POSITIVE

Evidence: the handler records a transaction, so the claim is true.
`ControlRegistry::changedNothing` (`src/core/ControlRegistry.cpp:298-303`) only
suppresses journal recording when the result carries no `__transaction`;
`handleSetUndoDepth` (`src/core/ControlCommandsUndo.cpp:135-200`) builds one, so
`recordTransactionOf` (`src/core/ControlRegistry.cpp:368-421`) runs, and
`undoByCommand` (`src/core/ControlCommandsControl.cpp:329-347`) dispatches the
inverse command (`control.set_undo_depth` with the previous caps, `applies:
command`) directly — no separate undo action or checkpoint is required for the
undo to happen.

Commit: `08e4d8979` (test only, no product change)

Test: `UndoBoundsTest::setUndoDepthRecordsAnUndoableCapChange`

Red/green: Green `cmake --build build --target UndoBoundsTest` EXIT=0;
`cd build/tests && ctest -R '^UndoBoundsTest$' --output-on-failure` EXIT=0
(10 passed). Gates CX=0, FL=0. The test locks the verified behaviour: the
transaction IS reversible, its inverse op is `control.set_undo_depth` with
`applies: command`, and one `control.undo` restores both caps. Note for the
next lane: both caps must be set in ONE call (A16 = one command per undo) — a
first draft that used two calls failed and that was a test bug, not a product bug.

## Wider run

`cmake --build build --target zene AgentSurfaceReportTest ControlTempoMapCommandsTest UndoBoundsTest` EXIT=0

`cd build/tests && ctest -R '^(AgentSurfaceReportTest|agent_surface|ControlTempoMapCommandsTest|TempoMapTest|TempoMapPersistenceTest|SmfInterchangeTest|ControlRegistryTest|UndoBoundsTest)$' --output-on-failure` — all EXIT=0.

Gates before each commit: `bash tests/complexity-gate.sh --check` EXIT=0;
`bash tests/file-length-gate.sh --check` EXIT=0. No ratchet was re-anchored; the
new test's first case hit CCN 11 and was split (`reportHas` extracted) rather
than baselined.

Leftovers: cluster 4964 (consent screen always answers `saved: true` — real,
needs a human-driven dialog; `TelemetryConsentDialog::save()` would have to
report whether it saved, since `exec()` cannot). Earlier in the session three
"failing" tests turned out to be stale unbuilt binaries, not regressions. No
other finding from this batch is half-done.
