# GUI batch-19 — 2026-10-05

Lane `gui`, branch `bugs/bunny-gui-2026-10-05`. Six findings: three fixed, three false positives.
No finding was a duplicate of another in this batch or of an earlier commit on the branch.

### Cluster 4175 — Clip name and note count go stale in the Inspector

Verdict: FIXED

Evidence: `src/gui/shell/InspectorPanel.cpp:385` and `:392` built the clip section's Name and Notes
labels as one-shot snapshots (`new QLabel(clip->name()...)`, `new QLabel(QString::number(midi->notes().size()))`)
and nothing ever re-read them, while the track's name (`InspectorPanel.cpp:464`) and the channel's name
(`InspectorSections.cpp:225` `syncChannelName()`) were both synced. Nothing could redraw them: the clip
section is keyed by the pointer alone (`InspectorSections.cpp:93` `clipKey()`), so a rename or a note
edit made in the arranger or the note editor changed no key and queued no rebuild. `Clip` has no name or
notes signal (`include/Clip.h:297`, `include/MidiClip.h:171`), so the fix keeps the two labels and re-reads
them in `syncToggles()` through the new `syncClipLabels()`.

Commit: e86f72d29

Test: DevicePanelTest::theClipsNameAndNoteCountFollowTheClip

Red/green:
- Red: `cmake --build build --target zene DevicePanelTest` EXIT=0; `ctest -R '^DevicePanelTest$'` EXIT=8 —
  `FAIL! theClipsNameAndNoteCountFollowTheClip(): Actual "Hook", Expected "Riff"` (11 passed, 1 failed, 6 skipped)
- Green: `cmake --build build --target zene DevicePanelTest` EXIT=0; `ctest -R '^DevicePanelTest$'` EXIT=0
- Green: `ctest -R '^(DevicePanelTest|AccessibilityWalkTest|ShellTest|ControlShell)$'` EXIT=0 (4/4)

### Cluster 4176 — Mixer-channel name goes stale in the Inspector

Verdict: FALSE-POSITIVE

Evidence: the finding says the channel name is "copied into a `QLabel`" at `InspectorPanel.cpp:419`. It is
not a `QLabel` at all: `channelSection()` makes a `QLineEdit` at `InspectorPanel.cpp:432`
(`m_channelName = channelNameField(channel, id, section)`), held as
`QPointer<QLineEdit> m_channelName` (`include/InspectorPanel.h:127`, whose own comment says "kept in step
with renames made elsewhere"). The rename path is already wired: `syncChannelName()`
(`InspectorSections.cpp:208-214`) resolves the channel by the id stored on the field and resets its text
when it differs, and it runs from `syncToggles()` (`InspectorPanel.cpp:460`), which the 250 ms `m_sync`
timer drives while the panel is shown. `DevicePanelTest::theChannelsNameIsEditedInPlace` covers the edit
and the undo of a channel rename through this field.

### Cluster 4290 — Modulator card rate readout can stay stale

Verdict: FALSE-POSITIVE

Evidence: the `source` capture at `src/gui/shell/ModulatorCard.cpp:84` is indeed by value, but the card's
reuse key is the modulator's whole description: `DevicePanel.cpp:335` builds
`"mod:" + compact-JSON(modulator)`, and that key contains the rate. `collectReusable()` (`DevicePanel.cpp:380`)
reuses a widget only when its key matches exactly, while `poll()` (`DevicePanel.cpp:438-445`) rebuilds
whenever `signature()` differs — and `signature()` (`DevicePanel.cpp:419-436`) ends with the full
`modulator.get_state` JSON. So a rate change, wherever it comes from (the card's own field, the Modulators
panel, the control surface), changes the key and forces a fresh `ModulatorCard` built from the new JSON.
`ModulatorCard` has exactly one use site (`DevicePanel.cpp:336`), so no other caller keeps a stale card
mounted. The card also exposes no refresh hook, confirming the rebuild is the intended path.

### Cluster 1710 — Track audio-input action omits its track target

Verdict: FALSE-POSITIVE

Evidence: `record.input_set` is not track-scoped. Its `argsSchema` (`src/core/ControlCommandsRecordingInput.cpp:233-238`)
accepts only `device`, `channels`, `left` and `right`, and its own description says the four keys "are
written to the config file under `audioinput`" — a project/engine-level capture plan, not a per-track
route. Passing `self` (a `{track, parameter}` object) would be rejected by the schema as unknown keys.
The empty fixed arguments at `src/gui/shell/ShellCommandMenus.cpp:138` are therefore correct, and the
finding's own hypothesis ("If the command sets that track's input route") does not hold. The finding's own
confidence was `low`, and it offers the alternative "otherwise move the action to a project-level menu" —
a product decision, not a defect.

### Cluster 5372 — Multiple pinned panels in one region cannot all stay open

Verdict: FIXED

Evidence: `reopenPinned()` (`src/gui/shell/ShellLayout.cpp:168-174`) inserted every pinned panel into its
home region through `current().openPanel`, a `QHash<int, QString>` of one panel id per region
(`include/ShellLayout.h:131`), and the header states the rule at `include/ShellLayout.h:41-42`: "Each side
region shows at most ONE panel at a time". So pinning Devices then Detail left both in `m_pinned` and both
marked pinned, while each insert replaced the previous — only the last was ever open, and the pair fought
over the region again on every view switch. `setPinnedQuietly()` now answers the pin already in that
region, so a region holds at most one pin.

Commit: e5b16b90c

Test: ShellLayoutTest::onlyOnePanelHoldsAPinInARegion

Red/green:
- Red: `cmake --build build --target zene ShellLayoutTest` EXIT=0; `ctest -R '^ShellLayoutTest$'` EXIT=8 —
  `FAIL! onlyOnePanelHoldsAPinInARegion() '!layout.isPinned("devices")' returned FALSE.` (13 passed, 2 failed)
- Green: `cmake --build build --target zene ShellLayoutTest` EXIT=0; `ctest -R '^ShellLayoutTest$'` EXIT=0 (13 passed)
- Green: `ctest -R '^(ShellTest|ShellHomesTest|AccessibilityHelperTest)$'` EXIT=0 (3/3)

### Cluster 2542 — Closing a pinned panel leaves it pinned

Verdict: FIXED

Evidence: `close()` (`src/gui/shell/ShellLayout.cpp:256-280`) removed the panel from the current view's
region but never touched `m_pinned`, so the pin mark stayed on (`Shell.cpp:378` re-reads
`m_layout.isPinned()` for the header button) and `reopenPinned()` brought the panel straight back at the
next `setView()`. `close()` now takes the pin with it, which is what closing asks for; `setPinned(id, false)`
was already the documented way to take a pin out deliberately.

Commit: b0e757ef6

Test: ShellLayoutTest::closingAPinnedPanelTakesItsPinWithIt

Red/green:
- Red: `cmake --build build --target zene ShellLayoutTest` EXIT=0; `ctest -R '^ShellLayoutTest$'` EXIT=8 —
  `FAIL! closingAPinnedPanelTakesItsPinWithIt() '!layout.isPinned("devices")' returned FALSE.` (14 passed, 1 failed)
- Green: `cmake --build build --target zene ShellLayoutTest` EXIT=0; `ctest -R '^ShellLayoutTest$'` EXIT=0 (14 passed)
- Green: `ctest -R '^(ShellTest|ShellHomesTest|AccessibilityHelperTest|ControlShell)$'` EXIT=0 (4/4, including
  the `window.pin` coverage at `tests/src/gui/HelpAccessTest.cpp:216`)

Leftovers: none. Both gates passed EXIT=0 before each of the three commits
(`tests/complexity-gate.sh --check`, `tests/file-length-gate.sh --check`). Two first-draft test assertions
of mine were wrong and were corrected against the code before the RED was accepted, not after: in
`onlyOnePanelHoldsAPinInARegion` the Edit view's `detail` is its centre and so is correctly
`isVisible()`, and in `theClipsNameAndNoteCountFollowTheClip` the clip section's own title label
(`deviceSection`) had to be excluded along with the captions. The product changes are small and additive:
no command added, removed or renamed, and no schema touched.