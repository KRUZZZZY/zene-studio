# GUI batch-11

Six findings, each checked against the tree at `3c1048dea` (the branch head when the batch
started; line numbers in the reports are from older revisions and had moved). Four were fixed
with a red/green regression test, two were left alone with the evidence that says why.

### Cluster 4457 — Session Sync keeps showing an old error tooltip after recovery

Verdict: FIXED

Evidence: `src/gui/SessionSyncAction.cpp:71` (before the fix) - `if (!result.ok)
{ action->setToolTip(result.errorMessage); }` was the only write to the tooltip after the
action was made, so a single refusal (no multicast, say) put the failure in the hover text and
nothing ever took it out again. The tooltip is now the action's own text on every success, so a
later working toggle clears the stale failure.

Commit: fb5107126

Test: tests/src/gui/SessionSyncActionTest.cpp::aRefusedToggleIsForgottenAfterASuccess

Red/green: `cmake --build build --target zene SessionSyncActionTest` -> EXIT=0;
`cd build/tests && ctest -R '^SessionSyncActionTest$' --output-on-failure` -> EXIT=8 (tooltip
stayed the refusal message), then EXIT=0 with the fix. Gates: complexity-gate.sh --check ->
EXIT=0; file-length-gate.sh --check -> EXIT=0.

### Cluster 5322 — Session Sync stops polling while unchecked

Verdict: FIXED

Evidence: `src/gui/SessionSyncAction.cpp:76` (before the fix) - the poll lambda was
`if (action->isChecked()) { show(action, linkState()); }`, which skips exactly the case a poll
exists for: another client enabling sync while this action reads unchecked. The poll now reads
the state whatever the toggle holds.

Commit: 412719e83

Test: tests/src/gui/SessionSyncActionTest.cpp::anEnableMadeElsewhereShowsWhileUnchecked

Red/green: `cmake --build build --target zene SessionSyncActionTest` -> EXIT=0;
`cd build/tests && ctest -R '^SessionSyncActionTest$' --output-on-failure` -> EXIT=8
('sync->isChecked()' returned FALSE), then EXIT=0 with the fix. The case runs, not skips: the
binary reports 6 passed, 0 skipped. Gates: complexity-gate.sh --check -> EXIT=0;
file-length-gate.sh --check -> EXIT=0.

### Cluster 4458 — Transport bar command failures are invisible

Verdict: FIXED

Evidence: `src/gui/SessionTransportBar.cpp:41-44` (before the fix) - `run()` returned
`.result`, so `ControlResult.ok` and `errorMessage` were dropped, and the four handlers at
`:68-77` had nowhere to put a refusal. `session.arrangement_record_land` refuses with a message
naming the launches that have not stopped (`src/core/ControlCommandsSessionRecordLand.cpp:138`),
and the row showed the buttons reverting and nothing else. The row now keeps a status label,
says each refusal there and speaks it.

Commit: 690132472

Test: tests/src/gui/SessionTransportBarTest.cpp::aRefusedButtonSaysWhy

Red/green: `cmake --build build --target zene SessionTransportBarTest` -> EXIT=0;
`cd build/tests && ctest -R '^SessionTransportBarTest$' --output-on-failure` -> EXIT=8 (the
only label shown was "1 event(s) recorded", no refusal anywhere), then EXIT=0 with the fix.
Gates: complexity-gate.sh --check -> EXIT=0; file-length-gate.sh --check -> EXIT=0.

### Cluster 3517 — Remote controls keep targeting the previous selection

Verdict: FIXED

Evidence: `src/gui/shell/ShellSelection.cpp:62-65` (before the fix) - `followRemoteControls`
set the target only when the selection named something followable and fell off the end
otherwise, so selecting a channel with no effects (or clearing the selection) left the knobs
driving the last device. It now clears the target in that case, and
`RemoteControls::followSelection` (`src/core/RemoteControls.cpp:112`, whose only caller is this
one) takes an empty target as "drive nothing" - without sending a page back, since there is no
device whose page that would be.

Commit: 8c47953f9

Test: tests/src/gui/DevicePanelTest.cpp::followingStopsWhenTheNewSelectionHasNothingToFollow

Red/green: `cmake --build build --target zene DevicePanelTest` -> EXIT=0;
`cd build/tests && ctest -R '^DevicePanelTest$' --output-on-failure` -> EXIT=8 (the target
still named the previous selection), then EXIT=0 with the fix. Gates: complexity-gate.sh
--check -> EXIT=0; file-length-gate.sh --check -> EXIT=0.

### Cluster 5360 — A maximum-sized grid creates over 100,000 widgets

Verdict: SKIPPED-SCOPE

Evidence: the report is right about the code - `src/gui/SessionGridView.cpp:203` creates one
`QToolButton` per track/scene pair inside `rebuild()` (`:156`), and `refresh()` (`:236`)
rebuilds whenever the grid's size changes, with no UI-side clamp anywhere in the path. The
model does permit it: `src/core/ControlCommandsSession.cpp:290-291` bounds the grid at 256
tracks and 512 scenes, so 131,072 cells. Both fixes the report offers are outside this lane:
virtualising the cells means replacing the widget grid with a view/delegate (a design change
well over 150 changed lines), and a UI-only limit would make slots the commands can legitimately
launch unreachable, which is a product decision about what the grid may hide. A deterministic
headless regression is not practical either - asserting the cap requires actually building the
131,072-widget grid to show it does not happen.

Commit: none

Test: none

Red/green: none

### Cluster 3514 — The sample editor cannot address frames beyond `INT_MAX`

Verdict: SKIPPED-SCOPE

Evidence: the narrowing is real but has moved, and it is a `SampleSelectionView` line, not the
dialog's: `src/gui/SampleSelectionView.cpp:53` does `static_cast<int>(clip->sample().
sampleSize())` on a `size_t` (`include/Sample.h:86`), and the selection endpoints, zoom and
hit-testing around it are all `int`. The reported file and line no longer hold it -
`src/gui/SampleEditorDialog.cpp:53` is a QHBoxLayout helper now, since the dialog moved to
musical-time units. Reaching the case needs a source longer than 2^31 frames (about 12.4 hours
of stereo audio at 48 kHz, roughly 8.5 GB), and fixing it properly means widening the view's
whole frame API - every endpoint, zoom span and paint computation - which is a design change
past this lane's size.

Commit: none

Test: none

Red/green: none

Leftovers: `DeviceCopyTest::aChainIsBypassedAndCleaned` fails on this host, before and after
this batch: it asserts `!loadAnEffect(target).isEmpty()` and this build loads no effect module,
which is why its eight sibling cases skip. Untouched by these changes, but it is a missing
QSKIP guard next to the ones its siblings have.