# cmds1 batch-07 — control commands (Clusters 1940, 1941, 2773, 5629, 2772, 4749)

Branch `bugs/bunny-cmds1-2026-10-05`. Lane: `ControlCommands[A-M]*.cpp`. Six findings, two
fixed, one already fixed, three out of the fixer's reach.

Totals: FIXED 2 · ALREADY-FIXED 1 · SKIPPED-SCOPE 3 · FALSE-POSITIVE 0 · REAL-UNTESTED 0.

---

### Cluster 4749 — A directory at the report path is listed as a crash report

Verdict: FIXED

Evidence: `src/core/ControlCommandsCrash.cpp` asked `QFileInfo::exists()` in three places —
`fileJson` reporting `exists`/`modified_unix`, the `crash.list_reports` append guard and the
acknowledge guard. `exists()` is true for a directory, so a directory planted at
`zene-crash-report.txt` was counted in `report_count`, reported as a zero-byte report, and could
be acknowledged. The acknowledge path would then consume a marker that named no report. All
three now test `isFile()`.

Commit: `8a76ac02c`

Test: `CrashReporterTest::a_directory_at_the_report_path_is_not_a_crash_report`

Red/green:
```
cmake --build build --target zene CrashReporterTest > build/bunny-b.log 2>&1; echo EXIT=$?   # EXIT=0
cd build/tests && ctest -R '^CrashReporterTest$' --output-on-failure                         # EXIT=8
  "Compared values are not the same" — report_count 1, expected 0
cmake --build build --target zene CrashReporterTest CrashReporterArmTest                     # EXIT=0
cd build/tests && ctest -R '^(CrashReporterTest|CrashReporterArmTest)$' --output-on-failure   # EXIT=0 (2/2)
bash tests/complexity-gate.sh --check                                                        # EXIT=0
bash tests/file-length-gate.sh --check                                                        # EXIT=0
```

---

### Cluster 1940 — Undoing a MIDI remap loses a custom soft-takeover target

Verdict: FIXED

Evidence: `MidiBinding` (`src/core/ControlCommandsControllerMap.cpp:76-82` before the fix)
captured channel, controller number, soft-takeover and feedback but not the takeover POINT.
`bindTo` rebuilt the binding through `bindMidiController`, which allocates a fresh
`MidiController` whose `m_softTakeoverTarget` is the default `0.0f`
(`include/MidiController.h:151`). So the undo closure restored the flag and the address and
silently reset the point the user had set. `bindingOf` now reads `softTakeoverTarget()` and
`bindTo` restores it AFTER the flag, because `setSoftTakeoverTarget` rearms against whatever
enabled state it finds (`src/core/midi/MidiController.cpp:158-162`).

A binding captured from `controller.map`'s own arguments has no point to carry, and `0.0f` is
exactly what a fresh controller holds, so the forward path is unchanged. The journal inverse-args
path (`mapArgsOf`) still cannot carry a point — that would be an args-schema change, which rule 8
forbids — but `control.undo` executes the recorded closure, which is the path this fixes.

Commit: `27b06c3fd`

Test: `UndoHonestyTest::undoingARemapKeepsTheSoftTakeoverTarget`

Red/green:
```
cmake --build build --target zene UndoHonestyTest > build/bunny-b.log 2>&1; echo EXIT=$?     # EXIT=0
cd build/tests && ctest -R '^UndoHonestyTest$' --output-on-failure                          # EXIT=8
  "Compared floats are not the same (fuzzy compare)" — Actual 0, Expected 0.75
cmake --build build --target zene UndoHonestyTest ControllerSurfaceTest \
  ClipJoinRippleTest ControlSuccessPathsTest                                                    # EXIT=0
cd build/tests && ctest -R '^UndoHonestyTest$' --output-on-failure                          # EXIT=0
cd build/tests && ctest -R '^(ControllerSurfaceTest|ClipJoinRippleTest|ControlSuccessPathsTest)$' \
  --output-on-failure                                                                        # EXIT=0 (3/3)
bash tests/complexity-gate.sh --check                                                        # EXIT=0
bash tests/file-length-gate.sh --check                                                        # EXIT=0
```

---

### Cluster 1941 — Mapping over a non-MIDI controller cannot be undone correctly

Verdict: ALREADY-FIXED

Evidence: the report says `bindingOf` treats any non-MIDI connection as "no binding" so the undo
sees `old.channel < 0` and only unmaps. Both halves were fixed before this batch.
`PreviousConnection` carries `QPointer<Controller> song` alongside the MIDI binding, and
`previousOf` (`src/core/ControlCommandsControllerMap.cpp:128-136`) fills it for any connection
whose controller is not a `Dummy` and is not MIDI; `restoreByName` (`:139-145`) re-makes the
connection on that controller. The `QPointer` is the guard the finding does not mention: a
controller deleted since is skipped, never dereferenced. The behaviour is already covered by
`UndoHonestyTest::undoingAMapOverAnLfoRestoresIt` (LFO connection → `controller.map` → undo → the
LFO is back, redo → MIDI again).

Commit: none

Test: `UndoHonestyTest::undoingAMapOverAnLfoRestoresIt` (pre-existing)

Red/green: none — no change made.

---

### Cluster 2773 — Applying a template with no resolvable targets records an irreversible no-op

Verdict: SKIPPED-SCOPE

Evidence: the defect is real as described — `registerControllerTemplateApply`
(`src/core/ControlCommandsControllerTemplates.cpp:254`) inserts `__transaction` whatever
`created` is, so an apply that bound nothing still records an irreversible entry on top of the
real edit before it, and `undoLastCommand` refuses the whole stack
(`src/core/ControlCommandsControl.cpp:359-364`). The finding's own first option ("record a
transaction only when at least one binding was applied") is not enough: I implemented it and
re-ran, and the record is still written. `ControlRegistry::runHandler` records every successful
mutating call unless `changedNothing` fires, and that helper only suppresses `true_inverse`
commands (`src/core/ControlRegistry.cpp:298-303`); `controller.template_apply` is classed
`Snapshot` in the contract table. Removing the payload just makes the registry write the "handler
supplied nothing" honest record instead — still a record, still on top, still irreversible.
Reaching the real fix means the registry honouring a handler-declared no-op, which is a
cross-cutting change to every mutating command and outside this lane's file scope.

Commit: none

Test: none — the regression case was written, confirmed RED, then withdrawn with the code change.

Red/green (the evidence that produced the verdict, not a kept test):
```
cmake --build build --target zene UndoHonestyTest                                           # EXIT=0
cd build/tests && QT_QPA_PLATFORM=offscreen ./UndoHonestyTest \
  aNoOpTemplateDeleteDoesNotBlockThePreviousUndo aNoOpTemplateApplyDoesNotBlockThePreviousUndo
# EXIT != 0 — "Actual "controller.template_apply", Expected "controller.map""
# ...and still EXIT != 0 with the handler fix applied, which is why the fix was withdrawn.
```

---

### Cluster 5629 — Applying a controller template has no undo step

Verdict: SKIPPED-SCOPE

Evidence: true but by design at this level, and the same handler as 2773. `template_apply`
creates model-owned `<connection>` state that no `ProjectJournal` checkpoint owns, so its own
recorded mechanism (`:260-264`) says exactly that and records `reversible: false` — an honest
irreversible record is what the contract asks for when there is no inverse. Making it reversible
means capturing each affected model's previous connection and adding an undo step, which is the
broader undo-capture mechanism 2773 also needs; it is a design change well past a batch-sized
diff. 2773 is the narrower, defensible half, and it is blocked at the registry (above).

Commit: none

Test: none

Red/green: none — no change made.

---

### Cluster 2772 — Deleting a nonexistent template records an irreversible transaction

Verdict: SKIPPED-SCOPE

Evidence: real as described — `registerControllerTemplateDelete`
(`src/core/ControlCommandsControllerTemplates.cpp:299`) attaches the transaction whatever
`removeTemplate` returned, so `removed: false` — a call that changed no file — still pushes an
irreversible record over the caller's previous edit. The same registry rule as 2773 blocks the
narrow fix: `changedNothing` (`src/core/ControlRegistry.cpp:298-303`) fires only for
`true_inverse`, and `controller.template_delete` is `Snapshot`
(`src/core/ControlReversibilityTableController.cpp:123`). I confirmed this by applying the
handler guard and re-running: the no-op record survives it, so the change was withdrawn rather
than committed as a half-fix.

Commit: none

Test: none — the regression case was written, confirmed RED, then withdrawn with the code change.

Red/green (evidence for the verdict, not a kept test):
```
cd build/tests && QT_QPA_PLATFORM=offscreen ./UndoHonestyTest \
  aNoOpTemplateDeleteDoesNotBlockThePreviousUndo
# "Actual "controller.template_delete", Expected "controller.map"" — FAIL
```

---

Leftovers: Clusters 2773, 5629 and 2772 are one shared blocker, not three independent ones — all
three no-op records are written by `ControlRegistry::runHandler`, whose `changedNothing` seam
suppresses a record only for `true_inverse` commands. One decision outside this lane fixes all
three: teach the registry to honour a handler-declared no-op (or reclassify the three commands),
which needs the contract table and the canary reviewed by the parent. Their regression cases were
written and confirmed RED and are described above; they were not kept, so nothing on this branch
currently pins that behaviour. Cluster 1940's journal inverse-args path (`mapArgsOf`) still cannot
carry a takeover point — fixing that needs an args-schema change, which rule 8 forbids.