# M0 coverage tests - lane gui (2026-10-08)

Worktree `zene-bunny-script`, branch `050/m0-cov-tests-gui`, from `28cc3a0b5`. Line coverage below was measured on a
Debug `-DWANT_COVERAGE=ON` build of this worktree (`build-coverage`, LibAAF and FFmpeg present), counters zeroed, then
the 21 tests listed in "Measured with" run once each (`ctest -R`, from `build-coverage/tests`, all 21 pass); lcov
over the whole build, filtered to the 29 files. Before: the figure in
`research/reviews/m0-2026-10-08/coverage-below-entry-floor-train1.txt`.

Measured with: PluginControlDialogsTest, FollowActionEditorTest, XyPadPairTest, VcaStripViewTest, VcaStripLifetimeTest,
CanvasKeyboardTest, ItemListButtonsTest, ArrangerTimelineTest, MixerModesTest, MixerMeteringTest, MixerWindowsTest,
EffectDeviceMenuTest, EffectPickerOrganiserTest, PluginNativeEditorTest, ReferenceProjectPanelTest,
TelemetryConsentDialogTest, AafImportMenuTest, TrackIconsLevelBarTest, ArrangerKeysNavigatorTest,
NoteBackgroundAudioTest, ArrangerOverlaysVideoLaneTest.

## Why the "test files" were not the cause here

No test file of this lane was under the floor. The files that already had a test (`FollowActionEditor` 0%,
`VcaStripView.h` 25%, `XyPad.h` 20%) were low only because the train-1 coverage run predates the tests that now cover
them (`FollowActionEditorTest` is from 2026-10-06/08) - except `VcaStripViewTest`, whose first case did
`QSKIP("no GuiApplication: a Fader cannot be built in this process")` in every run: the strip itself never ran. It now
starts the real application (`guitest::startGui`) and the strip case runs; the skip is gone.

## Table

| File | Before | After | Test(s) | Main paths covered / not covered |
| --- | --- | --- | --- | --- |
| include/LevelBar.h | 0% | 100% | TrackIconsLevelBarTest | the `Q_OBJECT` line (tooltip `tr`) |
| include/PluginNativeEditor.h | 0% | 100% | PluginNativeEditorTest | `isOpen()` |
| include/ReferenceProjectPanel.h | 0% | 100% | ReferenceProjectPanelTest | `Q_OBJECT` / accessors |
| include/VcaStripView.h | 25% | 100% | VcaStripViewTest (now runs under the real app) | `fader()`, `muteButton()`, `soloButton()` |
| include/XyPad.h | 20% | 100% | XyPadPairTest | `point()`, `channel()`, `area()`, `refresh()`, `sizeHint()` |
| plugins/Gate/GateControlDialog.{cpp,h} | 0% | 100% | PluginControlDialogsTest | real `gate` module loaded; six knobs + mode combo on the effect's own models both ways (param_set -> knob, key -> param_get), hint units |
| plugins/Modulation/ModulationControlDialog.{cpp,h} | 0% | 100% | PluginControlDialogsTest | same for `modulation` (mode + four knobs) |
| plugins/Receiver/ReceiverControlDialog.{cpp,h} | 0% | 100% | PluginControlDialogsTest | `receiver`: level knob, Replace LED click -> `Replace` param |
| src/gui/AafImportMenu.cpp | 30% | 85% | AafImportMenuTest | command name, file dialog cancelled / bad file; lines 46-48 (LibAAF-absent branch) |
| src/gui/ArrangerClipKeys.cpp | 24.7% | 97.3% | ArrangerKeysNavigatorTest | claims() truth table, Alt+Shift+Up/Down across tracks incl. "No clip above/below", Ctrl+Alt+Left/Right resize by snap + undo + 1-tick floor, Ctrl+Shift+L list, describeClip; 142-143 (selectClip refusal) |
| src/gui/ArrangerNavigator.cpp | 34.3% | 99.6% | ArrangerKeysNavigatorTest | click/drag/right-click, wheel (zoom, sideways, empty), all keys, Alt-click / double-click zoom to a section, tooltip, slider set both ways, arrangement.navigator; line 79 |
| src/gui/ArrangerOverlays.cpp | 35.0% | 100% | ArrangerOverlaysVideoLaneTest (+ArrangerTimelineTest) | range / audition / zoom / smart tools, rows between, off-row drags, grab loss / hide / window deactivate / tool change ending an audition, tool names |
| src/gui/ChannelPickDialog.cpp | 0% | 100% | EffectDeviceMenuTest | excluded + master never offered, OK needs a tick, cancel |
| src/gui/EffectDeviceMenu.cpp | 43.6% | 98.8% | EffectDeviceMenuTest | addresses (channel / track / rack chain / orphan), insert before/after, replace, copy to channels + copy chain (one undo), A/B, parameters, fold, delta, recorder, convolution reverb; 275-276 |
| src/gui/FollowActionEditor.cpp | 0% | 90.2% | FollowActionEditorTest (existing) | unchanged; 146, 153-156, 203, 221-231 not reached |
| src/gui/MixerHardwareMenu.cpp | 0% | 100% | MixerWindowsTest | output choices + undo, cue sends, insert form, surround format |
| src/gui/MixerRoutingMatrix.cpp | 47.8% | 97.4% | MixerWindowsTest | sparse model (text, roles, headers, flags), window filter, activated cell -> send form -> send; 95, 178-179 |
| src/gui/MixerWindows.cpp | 29.3% | 98.6% | MixerWindowsTest | Large Meters (bars, loudness tooltip armed/disarmed), Compare EQ, one window each; 230-231 |
| src/gui/PluginNativeEditor.cpp | 5.7% | 98.7% | PluginNativeEditorTest | refused attach, sizing, raise-on-reopen, detach once on close, host header (Bypass follows both ways, A/B, presets), instrument header, editor button refusal, chain owner; 74, 200 |
| src/gui/ReferenceProjectPanel.cpp | 1.8% | 100% | ReferenceProjectPanelTest | tree, targets, copy tracks / clips (original and play position) / devices, one-owner rule, Open / Close, stale marker |
| src/gui/SurroundPanner.cpp | 0% | 98.0% | MixerWindowsTest | click, drag, arrows (+Shift, clamp), LFE / spread / on, room names the 5.1 target; 90-92 |
| src/gui/TelemetryConsentDialog.cpp | 0% | 95.3% | TelemetryConsentDialogTest | opens off, preview + status per tick, Save (version) / withdraw / Close; 48, 118, 120 (the telemetry-compiled-out branches) |
| src/gui/TrackIcons.cpp | 42.3% | 92.3% | TrackIconsLevelBarTest | chosen / unknown / removed icon, header repaint; 52-53 |
| src/gui/VideoLane.cpp | 34.4% | 100% | ArrangerOverlaysVideoLaneTest | attach / detach layout, drawn span, seek click, real FFmpeg clip thumbnails |
| src/gui/modals/EffectPickerOrganiser.cpp | 6.6% | 100% | EffectPickerOrganiserTest | keys, star / hidden / folder roles, filters, preferred format, row menu via plugin.organise incl. folder dialog |
| src/gui/noteeditor/NoteBackgroundAudio.cpp | 0% | 100% | NoteBackgroundAudioTest | overlapping / auto / by id, waveform placement + clipping, silent / off-screen |

## Configuration dependence

`AafImportMenu.cpp` lines 46-48 are the "this build has no LibAAF" branch; on a build without LibAAF the branch run is
that one and the file would measure about 45%, because the file-dialog lambda (51-61) is then unreachable. The coverage
box has LibAAF; the test asserts the disabled entry and its status tip when `aafImportAvailable()` is false. If a
LibAAF-less coverage build is ever gated, this one file needs a decision.

## Bugs found (product code untouched)

1. **Queued `trackAdded` holds a raw `Track*`** - `src/gui/editors/TrackContainerView.cpp:124-126` connects
   `TrackContainer::trackAdded(Track*)` to `createTrackView` with `Qt::QueuedConnection`; `createTrackView`
   (`:318-327`) calls `_t->createView(this)` on whatever is passed. A track that is deleted in the same event-loop turn it
   was added in (create then `control.undo`, or create then `Song::clearProject()`, without returning to the loop) leaves
   the queued call with a dangling pointer. Observed three ways while writing tests: SIGSEGV in
   `TrackContainerView::createTrackView` (plugin.recorder_land then control.undo), and a view built over a freed track
   whose next `MainWindow::periodicUpdate` crashed in `InstrumentTrackView::corruptStateUpdate`
   (`src/gui/tracks/InstrumentTrackView.cpp:435-440`, `model()` null) / `SampleTrackView::corruptStateUpdate`
   (ReferenceProject copy_tracks + undo). Repro: in a GUI process, `Track::create(...)` then immediately delete the track,
   then `QCoreApplication::processEvents()`. Through the socket every command is its own loop turn, so it is rare in
   practice; the tests let the views build first (`newTrack()` helpers) and say why in a comment. No test is marked
   QEXPECT_FAIL for it because it is a crash, not a failed comparison.
2. Retracted: commit 588255674 added a QEXPECT_FAIL claiming `ReferenceProjectPanel.cpp:257`
   ("At the play position") read the wrong timeline. It is correct: while stopped the playhead lives in the `None`
   timeline (`TimeLineWidget.cpp:343`, `transport.seek`), which is what `getPlayPos()` reads. c2c03bd82 removes the
   QEXPECT_FAIL and the test seeks with `transport.seek`.

## Notes

- Every new executable is registered in `tests/CMakeLists.txt`, `tests/fork-sources.txt` and `tests/all-sources.txt`; the
  tests that load effect modules are in the `ENABLE_EXPORTS` / `LMMS_TEST_PLUGIN_DIR` list. Gates run before each
  commit: complexity, file-length (EffectDeviceMenuTest was split once to pass it), no-tautology, fork-sources,
  all-sources-reproduce, unregistered-tests - all pass.
- `StartGui`-based tests stop the audio device where they set meter values (`stopProcessing`), as VcaStripViewTest does.
