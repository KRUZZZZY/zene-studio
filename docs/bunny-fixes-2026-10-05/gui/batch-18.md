# GUI batch-18 — Space Bunny fixer lane, 2026-10-05

Findings 5191, 4303, 5029, 2222, 4168, 4177 (Luna). Checked against
`bugs/bunny-gui-2026-10-05` at 6ec2958e4; the line numbers in the reports were from older
revisions (e5a603132, b9a463ad4, 57980beef) and have moved, so every verdict below cites the
current tree.

### Cluster 5191 — MPE lane strokes create one undo step per note

Verdict: SKIPPED-SCOPE

Evidence: REAL as described, and deliberately built that way. `notelanes::writeFor()`
(`src/gui/noteeditor/NoteLanes.cpp:112-129`) returns `note.edit` for velocity, pan, chance,
release, spread and repeats, but `note.expression_set` for the three MPE axes (timbre, pressure,
pitch) because `note.edit` carries no such field. `NoteLaneView::commit()`
(`src/gui/noteeditor/NoteEditorParts.cpp:362-371`) therefore batches the `note.edit` writes into a
single `edits` array (one undo step) and runs each `note.expression_set` on its own. So a timbre
stroke over three notes is three undo steps, as reported. The class doc
(`include/NoteEditorParts.h:108-112`) states this as the intended contract ("one
`note.expression_set` per note (the MPE axes, which note.edit does not carry)"), and the handler
(`src/core/ControlCommandsNoteExpression.cpp:137-191`) is strictly one clip + one note: its schema
requires both (`ControlCommandsNoteExpression.cpp:148-149`) and journals a single note's
`before` state. Making the stroke one undo step means either adding a batch expression command or
widening `note.expression_set` to many notes — both change the command surface and its
params/result schema, which rule 8 reserves for the parent lane. No product code touched.

Commit: none

Test: none

Red/green: none

### Cluster 4303 — Scale root is ignored when testing scale membership

Verdict: FALSE-POSITIVE

Evidence: `NoteGrid::inScale()` (`src/gui/noteeditor/NoteGrid.cpp:115-118`) indexes the mask by
`key % 12` and never uses `m_scaleRoot`, which is true — but the mask it receives is already
root-absolute. `scalePitchClasses()` (`src/core/ControlScaleSupport.cpp:96-104`) rotates the scale
degrees by the root (`(root + degree) % 12`) before they become classes, and `scaleMask()`
(`ControlScaleSupport.cpp:106-115`) writes those classes straight into the 12-character mask
("index 0 = C"). `scaleContextState()`/`resolveScale()`
(`ControlScaleSupport.cpp:205-235`) both build classes that way, so `scale.get_state`'s `mask` is
absolute pitch classes, and `NoteEditorToolbar.cpp:330-334` passes it to `setScaleMask()` unchanged.
Rotating again in `inScale()` would double-apply the root and break every non-C root.
`m_scaleRoot` is kept for the UI that names the scale (the toolbar's label at
`NoteEditorToolbar.cpp:335`), not for the mask test. The existing case
`NoteGridTest::foldingToTheScaleHidesTheOtherKeys` (`tests/src/gui/NoteGridTest.cpp:150-158`)
confirms root-0 behaviour, and the producer above is what makes a non-C root correct.

Commit: none

Test: none

Red/green: none

### Cluster 5029 — Runtime theme edits leave the active palette stale

Verdict: FALSE-POSITIVE

Evidence: `palette()` (`src/gui/shell/DesignTokens.cpp:202-208`) really does read once into a
function-local static, with a comment saying so, and `accessibleMode()`
(`DesignTokens.cpp:245-249`) is the same pattern. That is the documented, advertised behaviour, not
an oversight: `appearance::startedWith()` (`src/core/Appearance.cpp:113-117`) is likewise a
read-once snapshot, `toJson()` reports `"restart_pending"` whenever the saved options differ from it
(`Appearance.cpp:123`), and `applyTiming()` returns `"applies": "next_start"` with
`"restart_required": true` and the words "The interface keeps its current look until Zene Studio is
restarted" (`Appearance.cpp:106-111`). So `theme.set_token` saving the colour while the current
session keeps its look is exactly what the command reports back. Refreshing the palette mid-session
would contradict `restart_pending` and the tooltips built on it.

Commit: none

Test: none

Red/green: none

### Cluster 2222 — Device panel rebuilds accumulate old widgets

Verdict: FALSE-POSITIVE

Evidence: the rebuild path does not drop only cards, and does not leak. `rebuild()`
(`src/gui/shell/DevicePanel.cpp:394-417`) first calls `collectReusable()`
(`DevicePanel.cpp:380-392`), which takes *every* item out of the row and files it under its
`KeyProperty` — not just `DeviceCard`s — then re-places each widget through `place()`
(`DevicePanel.cpp:241-252`), which stamps the key on whatever `make()` built. Sections
(`addSection`, `DevicePanel.cpp:289-299`), the channel meter and macro knobs
(`DevicePanel.cpp:367-369`) and modulator cards (`addModulators`, `DevicePanel.cpp:319-338`) all go
through `place()`, so they are all tracked. Whatever is left in `m_reusable` after the rebuild is
exactly the leftover set, and `rebuild()` retires it (`DevicePanel.cpp:414-416`). `dropCards()`
(`DevicePanel.cpp:199-207`) likewise retires every row item except `m_empty` and `m_add`, not just
cards; only `dropLeavingCards()`/`dropInstrumentCards()`
(`DevicePanel.cpp:209-232`) narrow to cards, because those are called on a signal from one chain and
must leave the others standing. The stale-UI and refresh-old-macros symptoms cannot arise: nothing
generated by a rebuild outlives the rebuild that dropped it.

Commit: none

Test: none

Red/green: none

### Cluster 4168 — Inspector command refusals lose their error message

Verdict: SKIPPED-SCOPE

Evidence: REAL as a gap, but the fix is a design change. `invoke()`
(`src/gui/shell/InspectorClipFields.cpp:60-63`) returns `ControlRegistry::instance()->invoke(id,
args).ok` and drops `errorMessage`, so every note/clip field edit
(`InspectorClipFields.cpp:141,155,167,194,202,221`) reports only ok/not-ok. The reason it is
written this way is the field's own contract: `ReadoutField::bindText()` takes
`std::function<bool(const QString&)>` (`include/TransportReadouts.h:62`) — a single bool, with no
channel for a message. Widening it means changing `ReadoutField` (a shared widget with 18
`bindText` call sites across four files) and adding an error display path to it, or replacing
these fields with the floating-message runner `runCommand()`/`announce()`
(`src/gui/CommandRun.cpp:87-113`), which changes the interaction from an inline field to a
transient popup. Both are interface decisions rather than a defect repair, and the first touches a
widget well outside this lane's blast radius. Code left alone.

Commit: none

Test: none

Red/green: none

### Cluster 4177 — Track color swatch does not follow color changes

Verdict: FIXED

Evidence: `Swatch` was built once from `track->color()` and held the value for life
(`src/gui/shell/InspectorPanel.cpp:123-140`, constructed at `InspectorPanel.cpp:304`), while
`Track::setColor()` emits `colorChanged()` (`src/core/Track.cpp:1278-1282`, declared
`include/Track.h:517`). `syncToggles()` (`InspectorPanel.cpp:429-442`) refreshed the live fields and
the name but never the swatch, and a rebuild happens only on a selection change
(`InspectorPanel.cpp:265`), so a `track.set_color` from the header, a script or an undo left the
Inspector showing the previous colour — exactly as reported. The swatch now connects to
`colorChanged` and repaints, and carries the colour it shows as a dynamic property so the test can
read it without the file-private type.

Commit: 4abd880eb

Test: DevicePanelTest::theTrackColourSwatchFollowsARecolour

Red/green:
- RED `cmake --build build --target zene DevicePanelTest` EXIT=0; `cd build/tests && ctest -R '^DevicePanelTest$' --output-on-failure` EXIT=8 — `FAIL! : theTrackColourSwatchFollowsARecolour() 'swatch != nullptr' returned FALSE.`
- GREEN same build EXIT=0; same ctest EXIT=0 (1/1 passed; the executable's own tally 10 passed, 0 failed, 6 skipped — the 6 skips are this host loading no effect module, unchanged from the red run)
- Gates: `bash tests/complexity-gate.sh --check` EXIT=0; `bash tests/file-length-gate.sh --check` EXIT=0

Leftovers: none. Cluster 5191 (one undo step per MPE note) and cluster 4168 (refusal messages in
the Inspector fields) are left for the parent lane: both need a command-surface or shared-widget
interface change, which rules 8 and this lane's scope reserve. Clusters 4303, 5029 and 2222 needed
no code change — the reported behaviour is the designed one.
