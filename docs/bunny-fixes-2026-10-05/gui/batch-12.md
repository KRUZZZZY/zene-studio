# GUI batch 12 — Luna clusters 5373, 5397, 5398, 1768, 2602, 4525

Lane: `gui` — Zene Studio, branch `bugs/bunny-gui-2026-10-05`.

Summary: 3 FIXED, 1 REAL-UNTESTED, 1 SKIPPED-SCOPE, 1 FALSE-POSITIVE.

### Cluster 5373 — A non-string shortcut override can silently unbind an action

Verdict: FIXED

Evidence: `src/gui/ShortcutMap.cpp:69` read the override with
`chosen.value(id).toString()`. `QJsonValue::toString()` returns an empty string for a
non-string value, and `portable()` treats empty input as valid, so the
`chosen.contains(id) && ok` condition cleared the action's shortcut instead of falling
back to its default. `bindings()` reported the same cleared value as `overridden`.
`shortcuts.import` already rejected non-key-text values, but a hand-edited config
reaches `applyTo()`/`bindings()` without passing through that command.

Commit: `78b065115`

Test: `UndoHistoryPanelTest`-style GUI regression —
`ShortcutsCommandsTest::aNonStringOverrideInTheConfigKeepsTheDefault`

Red/green:
- red: `cmake --build build --target zene ShortcutsCommandsTest > build/bunny-b.log 2>&1` EXIT=0;
  `ctest -R '^ShortcutsCommandsTest$' --output-on-failure` EXIT=8 —
  "a non-string override left the action on '', not its default 'Ctrl+N'"
- green: same build EXIT=0; `ctest -R '^ShortcutsCommandsTest$'` EXIT=0 (8 passed);
  `ctest -R '^CommandFormTest$'` EXIT=0; complexity-gate EXIT=0; file-length-gate EXIT=0

### Cluster 5397 — Resampling a clip runs synchronously on the GUI thread

Verdict: SKIPPED-SCOPE

Evidence: `src/gui/StemSplitController.cpp:160` calls
`resampleBuffer(source, StemModelSampleRate)` on the GUI thread before
`m_manager.submit(...)` at line 163, so the observation is correct. Moving that work
into the worker is not a local fix: `StemJobManager::submit` takes an already-prepared
`std::shared_ptr<const SampleBuffer>` plus its rate
(`include/StemSeparation/StemJobManager.h:125`), so the resample would have to move
inside the manager or into the separator interface, changing a core-layer API and its
callers (`src/core/RenderQueue.cpp:121`, `src/core/ControlStemSupport.cpp:240`) —
well past the ~150-line budget and a cross-layer design change. This is the GUI thread,
not an audio callback, so rule 6 does not forbid the current code.

Commit: none

Test: none

Red/green: none

### Cluster 5398 — The async completion handler can dereference a deleted clip

Verdict: REAL-UNTESTED

Evidence: `src/gui/StemSplitController.cpp:162` stores `m_clip = clip` in a raw
`SampleClip*` (declared at `include/StemSplitController.h:82`, no QPointer) and
dereferences it at lines 208-209 in `handleFinished()` via `m_clip->startPosition()`
and `m_clip->name()`. If the clip is deleted, or the project is closed, while the job
runs, that is a use-after-free and can add the tracks to the wrong song. The defect is
real, but no deterministic headless regression is available: `handleFinished` is a
private slot driven by `StemJobManager::jobFinished`, and the controller installs a real
`ExternalProcessStemSeparator` in its constructor (`src/gui/StemSplitController.cpp:54`),
so reaching the dereference needs either the Python backend and its model, or a test
seam the class does not expose. Per the procedure, product code is left unchanged.

Commit: none

Test: none

Red/green: none

### Cluster 1768 — Tempo-map rows show incorrect bar numbers after meter changes

Verdict: FIXED

Evidence: `src/gui/TempoMapPanel.cpp:86` computed every row's bar as
`tick / std::max(1, ticksPerBar) + 1` from a single global
`Engine::getSong()->ticksPerBar()`, ignoring the time-signature events in the map. The
fix adds an anonymous-namespace `barAt(events, row, projectTicksPerBar)` that walks the
events in ascending tick order and applies each event's own
`DefaultTicksPerBar * numerator / denominator`; `eventCells()` now takes the computed
bar instead of a raw `ticksPerBar`. `musicaltime::positionText(tick)` is untouched — the
Position column was never the wrong one.

Commit: `263e8c7b7`

Test: `TempoMapPanelTest::theBarColumnCountsTheTimeSignatureEvents`

Red/green:
- red: `cmake --build build --target zene TempoMapPanelTest > build/bunny-b.log 2>&1` EXIT=0;
  `ctest -R '^TempoMapPanelTest$' --output-on-failure` EXIT=8 —
  `Actual "2" / Expected "3"` at `TempoMapPanelTest.cpp:175`
- green: same build EXIT=0; `ctest -R '^TempoMapPanelTest$'` EXIT=0;
  `ctest -R '^(TakeLanesPanelTest|SampleEditorDialogTest|RecordingPassTest)$'` EXIT=0 (3/3);
  complexity-gate EXIT=0; file-length-gate EXIT=0

### Cluster 2602 — Undo History rows do not reliably identify journal steps

Verdict: FIXED

Evidence: `src/gui/UndoHistoryPanel.cpp:155-158` built the jump offset straight from the
visible row (`steps: -(row + 1)`). The list holds one row per COMMAND step while the
journal also holds direct editor edits with no row, so a jump stopped one step short of
the row it named — exactly the panel's own note at lines 128-131 admits. Each row now
keeps the `step` serial that `control.transactions` already reports
(`src/core/ControlTransactions.cpp:140`), and `stepsBackTo()` counts from that serial to
`ProjectJournal::topStepSerial()`, falling back to the row count when a row names no
step. No command or schema changed.

Commit: `dc59c5200`

Test: `UndoHistoryPanelTest::aJumpToARowCoversTheUnlistedEditorStepsAfterIt`

Red/green:
- red: `cmake --build build --target zene UndoHistoryPanelTest > build/bunny-b.log 2>&1` EXIT=0;
  `ctest -R '^UndoHistoryPanelTest$' --output-on-failure` EXIT=8 —
  `Actual 2 / Expected 1` tracks after jumping to the older command row
- green: same build EXIT=0; `ctest -R '^UndoHistoryPanelTest$'` EXIT=0 (5 passed);
  complexity-gate EXIT=0; file-length-gate EXIT=0

### Cluster 4525 — VCA strip labels go stale after edits

Verdict: FALSE-POSITIVE

Evidence: `src/gui/VcaStripView.cpp:49-63` does set the name, member count, tooltip and
accessible name only in the constructor — but those values are not the strip's source of
truth after construction, because `MixerView` deletes and rebuilds every VCA strip when
the group set changes. `src/gui/MixerView.cpp:379` compares `vcaSignatureOf()` on each
`syncWithMixer()` pass and calls `refreshVcaStrips()`, and `vcaSignatureOf()`
(`src/gui/MixerView.cpp:331-340`) includes each group's `name()` and `members().size()`.
A rename or a membership change therefore alters the signature and rebuilds the strip
within the 500 ms sync timer (`src/gui/MixerView.cpp:226-228`), so no label can go stale.
`VcaGroup` needs no new signal for this.

Commit: none

Test: none (`tests/src/gui/VcaStripViewTest.cpp` and `VcaStripLifetimeTest.cpp` cover the
strip; the rebuild path is in `MixerView`)

Red/green: none

Leftovers: cluster 5398 (dangling `m_clip` in `StemSplitController::handleFinished`) is a
confirmed use-after-free left unfixed — it needs a test seam (an injectable separator or
a public completion entry point) before a headless regression is possible. Cluster 5397
(resample on the GUI thread) needs the resample moved behind
`StemJobManager::submit`/`StemSeparator`, a core-layer API change left for the core lane.