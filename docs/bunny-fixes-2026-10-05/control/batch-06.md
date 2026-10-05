# Control lane — batch 06

Six findings, checked against `bugs/bunny-control-2026-10-05` at `4a01e6dd0`.
Two were real and are fixed; one is a false positive; three need a design
decision this lane may not make.

### Cluster 4064 — Recording journal reads accept relative take paths

Verdict: FIXED

Evidence: `readJournal()` in `src/core/ControlRecordingSupport.cpp:105-123` checked only
`take.isEmpty()`, while the sibling writer `journalBegin` in
`src/core/ControlCommandsRecording.cpp:84-90` already refused a non-absolute `take`.
Because `journalPathFor()` (`src/core/RecordingJournal.cpp:147`) is plain
`takePath + ".rec-journal"`, the four read paths resolved a relative name against the
process's current working directory: `record.journal_update`
(`src/core/ControlCommandsRecording.cpp:147`), `record.journal_finish` (`:201`),
`record.recovery_restore` (`src/core/ControlCommandsRecordingRecovery.cpp:95`) and
`record.recovery_discard` (`:140`). A take whose path was relative when journalled could
be read, mutated or deleted against a different file in the next run.

Commit: `066a82d91`

Test: `RecordingRecoveryMenuTest::aRelativeTakeIsRefused`

Red/green:
- Red: `cd build/tests && ctest -R '^RecordingRecoveryMenuTest$' --output-on-failure` -> EXIT=8
  (`journal_update` returned `not_found` (1), expected `invalid_args` (3))
- Green: same command -> EXIT=0; `ctest -R '^RecordingPassTest$' --output-on-failure` -> EXIT=0
- Gates: `complexity-gate.sh --check` -> EXIT=0, `file-length-gate.sh --check` -> EXIT=0

### Cluster 3036 — A take with no route or explicit file gets a `route-1.wav` path

Verdict: FALSE-POSITIVE

Evidence: `routeArg()` (`src/core/ControlRecordingSupport.cpp:271-274`) does return `-1` for
an absent route, and `takePathArg()` (`:284-292`) would format that into
`zene-take-route-1.wav`. But `takePathArg` has exactly one caller —
`armTrack` at `src/core/ControlCommandsRecordingRoutes.cpp:172` — and that call is
guarded by `routeAddressable()` at `:160`, which refuses anything outside
`[0, trackCount)` (`:87-95`). The only other user of `routeArg`, `disarmTrack` (`:215`),
never derives a path. `record.arm_track`'s schema is `integerProperty(0, 1024)`, so a
negative route cannot arrive at all. The described `zene-take-route-1.wav` default is
unreachable in the current tree.

Commit: none

Test: none

Red/green: none

### Cluster 4036 — `requires: human` blocks the GUI action too

Verdict: SKIPPED-SCOPE

Evidence: real, but it is the documented contract, not a slip. `app.online_help` declares
`requires: display, human` (`src/gui/ControlCommandsShell.cpp:183`), `checkRequires()`
refuses `human` unconditionally (`src/core/ControlRegistry.cpp:199-202`), and
`MainWindow::browseHelp()` dispatches through the same registry
(`src/gui/MainWindow.cpp:1722-1727`). The behaviour is asserted on purpose:
`tests/src/core/ControlRegistryTest.cpp:206-221` (`humanRequirementIsRefused`),
`tests/src/core/ControlRegistryTest.cpp:448-482` and the allowlist entry
`tests/agent-surface-allowlist.txt:40-41`. The finding asks for a way to tell a GUI
dispatch from an automated one — a new dispatch concept threaded through every entry
point, i.e. a design change rather than a defect fix.

Commit: none

Test: none

Red/green: none

### Cluster 4038 — Nested mutating commands can record the same undo step twice

Verdict: FIXED

Evidence: `runHandler()` recorded a transaction for any `mutating && changed` call
(`src/core/ControlRegistry.cpp:356-360`), with no outermost-only guard, even though the
observer already fires only at `m_depth == 0` (`include/ControlRegistry.h:289-292`) and
`collab::CommandTap::observed` is handed the depth. Nested invokes are real
(`src/core/ControlCommandsDocuments.cpp:64`, `src/core/ControlCommandsAlternatives.cpp:207`,
`src/core/ControlCommandsRange.cpp:367`, `src/core/ControlCommandsHistoryJump.cpp:53`,
`src/core/McuSurface.cpp:67`), so one undo step could get two transaction records and two
provenance changes. The decision now lives in `recordsItsOwnStep()`
(`src/core/ControlRegistry.cpp:316`), which keeps `runHandler` under the complexity ratchet.

Commit: `4a01e6dd0`

Test: `ControlRegistryTest::nestedMutatingCommandRecordsOneTransaction`

Red/green:
- Red: `cd build/tests && ctest -R '^ControlRegistryTest$' --output-on-failure` -> EXIT=8
  (`transactions.size()` 2, expected 1)
- Green: same command -> EXIT=0; `ctest -R '^(UndoHonestyTest|ReversibilityContractTest|ProvenanceSectionTest|ControlResultSchemaTest)$' --output-on-failure` -> EXIT=0
- Gates: `complexity-gate.sh --check` -> EXIT=0, `file-length-gate.sh --check` -> EXIT=0

### Cluster 4039 — Failed handlers can leave unrecorded project changes

Verdict: SKIPPED-SCOPE

Evidence: the observation holds — `journal->mergeCheckpointsFrom(mark)`
(`src/core/ControlRegistry.cpp:337`) runs regardless of `result.ok`, and `changed`
(`:366`) requires `result.ok`, so a handler that fails after writing leaves a merged undo
step with no transaction describing it. Both remedies the finding offers are design
changes well past this lane: rolling the step back means undoing model writes the
registry never made, and staging writes means changing every handler's shape. Neither is
a minimal behaviour-preserving edit.

Commit: none

Test: none

Red/green: none

### Cluster 4037 — Result-schema failures are returned after the mutation is committed

Verdict: SKIPPED-SCOPE

Evidence: the ordering is real — `recordTransactionOf()` runs at
`src/core/ControlRegistry.cpp:369` while `control::finishResult()` runs at `:374`, and
`finishResult` is what turns a violating reply into `Refused`
(`src/core/ControlSchema.cpp:228-243`). But the two cannot simply be swapped:
`finishResult` strips `__transaction` (`src/core/ControlSchema.cpp:230`) and
`recordTransactionOf` reads exactly that key (`src/core/ControlRegistry.cpp:389`), so
reordering silently drops the recorded inverse. The alternative the finding offers — keep
the reply successful and report the defect diagnostically — contradicts the locked
behaviour asserted in `tests/src/core/ControlResultSchemaTest.cpp:125-131`. Choosing
between "roll back" and "stay successful" is a contract decision, not a fix.

Commit: none

Test: none

Red/green: none

Leftovers: clusters 4036, 4039 and 4037 are confirmed-but-unfixed and need an owner who
can change the dispatch/result contract; the 4037 fix must preserve the `__transaction`
hand-off from `finishResult` to `recordTransactionOf` if it is ever taken up.