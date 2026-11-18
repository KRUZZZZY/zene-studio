# GUI batch-09 — Space Bunny fixer lane

Six findings triaged against `bugs/bunny-gui-2026-10-05` at `58b6f4a4d`. Five were real and are
fixed; one is a hypothesis the code does not support.

### Cluster 5168 — Same-count mixer reorders leave strips bound to the old channels

Verdict: FIXED

Evidence: `src/gui/MixerView.cpp` `syncWithMixer()` rebuilt the strip views only when the channel
count changed, so an external `mixer.order_by_tracks` with an unchanged count was invisible to the
mixer. `MixerChannelView` binds `m_muteModel` / `m_soloModel` / `m_panModel` / `m_volumeModel` once
at construction by index (`src/gui/MixerChannelView.cpp:142-162`), so the surviving strips kept
driving the channels that used to sit at their index. Added a private `m_channelOrderSignature` to
`include/MixerView.h` and an anonymous-namespace `channelOrderSignatureOf(Mixer*)` helper that
concatenates the channel ids in order; `syncWithMixer()` now rebuilds when the count **or** the
signature changed, and the signature is refreshed on every path that rebuilds anyway
(`refreshDisplay()`, `addNewChannel()`, `deleteChannel()`, `moveChannelLeft()`) so those do not
re-enter the rebuild.

Commit: `c42062354`
Test: `MixerModesTest::aReorderWithTheSameCountRebindsEveryStrip`
Red/green: `cmake --build build --target zene MixerModesTest` EXIT=0 then
`cd build/tests && ctest -R '^MixerModesTest$' --output-on-failure` EXIT=8 (red: the solo button was
still bound to the channel that had moved); green: build EXIT=0, ctest EXIT=0, and
`ctest -R '^(MixerToolsTest|VcaStripLifetimeTest|MixerSendsOnFadersTest|MixerLatencyTextTest)$'`
EXIT=0 (4/4).

### Cluster 3273 — Deleting a channel before the selected one changes the selection

Verdict: FIXED

Evidence: `src/gui/MixerView.cpp` `deleteChannel()` saved `selLine` before the removal but never
renumbered it, so deleting a channel at a lower index left the stale index selecting a different
channel. Added `if (selLine > index) { --selLine; }` after `m_mixerChannelViews.remove(index)`.

Commit: `6ffba84ea`
Test: `MixerModesTest::deletingAChannelInFrontKeepsTheSelectedChannelSelected`
Red/green: `cmake --build build --target zene MixerModesTest` EXIT=0 then
`cd build/tests && ctest -R '^MixerModesTest$' --output-on-failure` EXIT=8 (red: selection landed on
index 4, expected the selected channel now at index 3); green: build EXIT=0, ctest EXIT=0 (14
passed).

### Cluster 4261 — External channel changes reset mixer selection to Master

Verdict: FIXED

Evidence: `src/gui/MixerView.cpp` `refreshDisplay()` destroyed the strip views and then called
`setCurrentMixerChannel(0)` unconditionally, so any outside channel-count change (a script adding or
removing a channel) threw the user's selection back to Master. `refreshDisplay()` now captures the
selected channel's stable id from `m_currentMixerChannel->mixerChannel()->id()` before the rebuild
and restores the selection by matching that id, falling back to 0 only when the channel is gone.

Commit: `de253d0d6`
Test: `MixerModesTest::addingAChannelKeepsTheSelectedChannelSelected`
Red/green: `cmake --build build --target zene MixerModesTest` EXIT=0 then
`cd build/tests && ctest -R '^MixerModesTest$' --output-on-failure` EXIT=8 (red: selection read
"Master", expected "Selected"); green: build EXIT=0, ctest EXIT=0 (15 passed), and
`ctest -R '^(MixerSendsOnFadersTest|VcaStripLifetimeTest|MixerLatencyTextTest)$'` EXIT=0 (3/3).

### Cluster 5166 — Mixer channel filters are not applied to strip visibility

Verdict: FALSE-POSITIVE

Evidence: the filter is applied. `src/gui/MixerOverlays.cpp:165` calls `mixerviewoptions::shows(i)`
when applying the options, and `MixerView`'s sync path calls that `apply()` at
`src/gui/MixerView.cpp:346`. The finding's claim that `shows()` is never consulted does not hold in
the current tree; no code was changed.

Commit: none
Test: none
Red/green: none

### Cluster 4291 — Changing a modulator's shape can silently change its rate

Verdict: FIXED

Evidence: the rate box is created with `setDecimals(2)` (`src/gui/ModulatorPanel.cpp:129`) and the
shape combo's change handler called `setShapeAndRate(m_shape->currentText(), m_rate->value())`, so
choosing another shape wrote the box's rounded value back and a rate of `1.234` Hz became
`1.23` Hz. `modulator.rate_set` documents that an argument a call omits keeps the value it had
(`src/core/ControlCommandsModulator.cpp:321-322`), so the fix is the shape-only call: added
`ModulatorPanel::setShape()`, which sends just `shape`, and pointed the combo at it. The rate box's
own `editingFinished` still sends both, which is unchanged and correct.

Commit: `050403a66`
Test: `ModulatorPanelTest::changingOnlyTheShapeKeepsTheExactRate`
Red/green: `cmake --build build --target zene ModulatorPanelTest` EXIT=0 then
`cd build/tests && ctest -R '^ModulatorPanelTest$' --output-on-failure` EXIT=8 (red: "rate became
1.23"); green: build EXIT=0, ctest EXIT=0.

### Cluster 4293 — Modulator command failures are hidden from the user

Verdict: FIXED

Evidence: `src/gui/ModulatorPanel.cpp:158-164` (pre-fix numbering) connected Add, Remove, Bind and
Unbind to `addModulator()`, `removeSelected()`, `bind()` and `unbindRow()`, each of which returns
the command's refusal message, and every handler discarded it. A refused button therefore changed
nothing and said nothing. Added a status `QLabel` and a private `ModulatorPanel::say()`, and routed
the four handlers through it; a non-empty message is also spoken with
`a11y::announceStatus(..., a11y::Politeness::Assertive)`, following the existing status-label
precedent in `src/gui/SampleEditorDialog.cpp`. A label was chosen over a modal `QMessageBox` so the
panel stays usable and the offscreen test can read the reason.

Commit: `98fe405a8`
Test: `ModulatorPanelTest::aRefusedButtonSaysWhy`
Red/green: `cmake --build build --target zene ModulatorPanelTest` EXIT=0 then
`cd build/tests && ctest -R '^ModulatorPanelTest$' --output-on-failure` EXIT=8 (red: no label in the
panel carried the refusal); green: build EXIT=0, ctest EXIT=0, and running the binary directly shows
`changingOnlyTheShapeKeepsTheExactRate` PASS and `aRefusedButtonSaysWhy` PASS (5 passed, 0 failed,
1 skipped).

Leftovers: `ModulatorPanelTest::theLayerIsEditedFromThePanel` skips on this build host with "this
host loads no effect module"; that skip is pre-existing and unrelated to these two fixes. Cluster
5166 was not re-tested with a new regression because the code already applies the filter.