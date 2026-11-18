# GUI batch-10

Six findings, each checked against the tree at `189fdeadd` (the branch head when the batch
started was `7b2c2f243`). Four were fixed with a red/green regression test, two were left
alone with the evidence that says why.

### Cluster 4292 — Source controls stay enabled with no modulator selected

Verdict: FALSE-POSITIVE

Evidence: `src/gui/ModulatorPanel.cpp:255-262` - `showSelected()` walks the panel's
`QPushButton`s and disables only those carrying the `needsModulator` property, which
`selectionButton()` sets at `src/gui/ModulatorPanel.cpp:96`. The shape combo and rate spin box
(`src/gui/ModulatorPanel.cpp:128` and `:131`) are not buttons, so they are never in that set.
That is deliberate, not an oversight: `Add LFO` reads `m_shape->currentText()` and
`m_rate->value()` at `src/gui/ModulatorPanel.cpp:164`, so those two controls are how the user
picks the shape and rate of the modulator they are *about to* create. Disabling or clearing
them when nothing is selected - the suggested fix - would make `Add LFO` impossible. The
already-committed work on this branch (`98fe405a8`, `050403a66`) covers the refusals and the
rounded rate; there is nothing stale to clear either, since `showSelected()` refreshes both
from the newly selected modulator at `src/gui/ModulatorPanel.cpp:269-270` under signal blockers.

Commit: none

Test: none

Red/green: none

### Cluster 4326 — Transform prompts show internal parameter names

Verdict: FIXED

Evidence: `src/gui/NoteTransformActions.cpp:77` passed the command argument as the prompt
label, so the prompt said `velocity_jitter` / `position_jitter`. The label is now the action's
own text; the argument still names the command parameter.

Commit: 1d677355c

Test: tests/src/gui/NoteTransformActionsTest.cpp::thePromptNamesTheToolNotItsArgument

Red/green: red `cmake --build build --target zene NoteTransformActionsTest` EXIT=0, then
`cd build/tests && ctest -R '^NoteTransformActionsTest$' --output-on-failure` EXIT=8 with
`FAIL! ... '!label.contains(QLatin1Char('_'))' returned FALSE. (Randomize velocities... prompted
'velocity_jitter')`; green same build EXIT=0, same ctest EXIT=0.

### Cluster 3342 — Exported selection helper dereferences a null clip

Verdict: FIXED

Evidence: `handOverNoteSelection` (`include/NoteTransformActions.h:52`) dereferenced
`clip->notes()` with no null check. `src/gui/shell/InspectorClipFields.cpp:125` calls it with a
`QPointer<MidiClip>` that can be null, so the hazard is live. Now returns false at the top.

Commit: 189fdeadd

Test: tests/src/gui/NoteTransformActionsTest.cpp::noClipHandsOverNothing

Red/green: red `cmake --build build --target zene NoteTransformActionsTest` EXIT=0, then
`cd build/tests && ctest -R '^NoteTransformActionsTest$' --output-on-failure` EXIT=8 with
`Received signal 11 (SIGSEGV) ... noClipHandsOverNothing`; green same build EXIT=0, same ctest
EXIT=0.

### Cluster 652 — Dragging pins does not detect held mouse buttons

Verdict: FIXED

Evidence: `src/gui/PinConnector.cpp:448` asked `QMouseEvent::button()` whether a button was
held. A mouse-move event carries no button of its own - Qt reports held buttons through
`buttons()` - so the guard never fired and a drag took the hover path, keeping the pointing
hand. Now asks `me->buttons()`.

Commit: 9613dd3ab

Test: tests/src/gui/PinConnectorTest.cpp::aDragDoesNotOfferTheHoverCursor

Red/green: red `cmake --build build --target zene PinConnectorTest` EXIT=0, then
`cd build/tests && ctest -R '^PinConnectorTest$' --output-on-failure` EXIT=8 with
`FAIL!  : PinConnectorTest::aDragDoesNotOfferTheHoverCursor() 'view->cursor().shape() !=
Qt::CursorShape::PointingHandCursor' returned FALSE.`; green same build EXIT=0, same ctest
EXIT=0.

### Cluster 4361 — Unbind entries are ambiguous across chains

Verdict: FIXED

Evidence: `src/gui/RackMenu.cpp:213` labelled each unbind entry `fx-<n> <parameter>`, dropping
the chain that the target already carries (`src/core/ControlRackSupport.cpp:113` inserts it, and
`macroTargetJson` is what `rack.get_state` reports). Two targets sharing an effect index and a
parameter across chains offered two identical items. The label now goes through the exported
`unbindTargetText()` (`include/RackMenu.h`), which prefixes the chain.

Commit: 529db2015

Test: tests/src/gui/RackMenuTest.cpp::unbindItemsNameTheirChain

Red/green: red `cmake --build build --target zene RackMenuTest` EXIT=0, then
`cd build/tests && ctest -R '^RackMenuTest$' --output-on-failure` EXIT=8 with
`FAIL!  : RackMenuTest::unbindItemsNameTheirChain() 'onFirst != onSecond' returned FALSE. (two
chains' targets share one label, so the user cannot tell which one is removed)`; green same
build EXIT=0, same ctest EXIT=0.

### Cluster 3412 — Recording recovery creates an unjournaled track and clip

Verdict: SKIPPED-SCOPE

Evidence: `src/gui/RecordingRecoveryMenu.cpp:56-62` creates the `SampleTrack` and `SampleClip`
directly after `record.recovery_restore` succeeds, so undo reverts only the recovery command and
leaves the imported audio on a new track. The defect is real, but the fix the report asks for -
one journaled command covering the state change *and* the track/clip creation - needs a command
that does not exist yet and a result schema to go with it, which rule 8 puts outside this lane.
The GUI-side workaround of suppressing the direct creation would instead break recovery
altogether. Left the code alone.

Commit: none

Test: none

Red/green: none

## Leftovers

Cluster 3412 needs a new control command (track creation plus clip placement, journaled as one
step) and its schema - parent's lane, not the GUI lane. `unbindTargetText` was added to
`include/RackMenu.h` as a test seam in the same shape as the existing `rackStatusText`;
`PinConnectorTest` is registered in `tests/CMakeLists.txt` and `src/gui/PinConnector.cpp` was
already listed in `tests/fork-sources.txt`, so that file needed no change.
