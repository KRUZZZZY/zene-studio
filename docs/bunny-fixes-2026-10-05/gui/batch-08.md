# GUI batch-08 — Space Bunny fixer lane

Six findings triaged against `bugs/bunny-gui-2026-10-05` at `07e58f47b`. Two were real and are
fixed; two were already handled in the current tree and are now held by a regression guard; two
were hypotheses the code does not support.

### Cluster 5139 — MIDI Clock status says "sending" when no output is ready

Verdict: FIXED

Evidence: `src/gui/MidiClockMenu.cpp:89-94` built the status line from `master.enabled` alone, but
`clock.get_state` does report `master.port_ready` (`src/core/MidiClockState.cpp:66-71`), and it is
false for a sequencer client with no subscribed destination (`src/core/MidiClock.cpp:211-223`), so
"enabled with nothing receiving" is a reachable state that read identically to a routed one. The
line now distinguishes `not sending` / `sending` / `sending, no output port`.

Commit: `a5bf4bcaf`
Test: `MidiClockMenuTest::theLineDistinguishesEnabledFromRouted`
Red/green: `cmake --build build --target zene MidiClockMenuTest` EXIT=0 then
`cd build/tests && ctest -R '^MidiClockMenuTest$' --output-on-failure` EXIT=8 (red: the enabled but
unrouted case read identically to a routed one); same build EXIT=0, same ctest EXIT=0 (green).

### Cluster 5145 — Disarming learn mode leaves a pending binding to fire later

Verdict: FIXED

Evidence: `src/gui/MidiLearnGui.cpp:84-107` `syncArmedState()` stopped the bind timer and cleared the
focus target but never cleared `MidiLearn`'s pending slot, so re-arming restarted the timer and
`applyPendingBinding()` (`src/core/MidiLearn.cpp:201-216`) applied the stale CC to whatever control
was focused next. Added `MidiLearn::cancelPendingBinding()`, which claims the pending flag with the
same `m_pendingBinding.exchange(false, acquire)` the apply path uses, so a request a MIDI input
thread is claiming at that moment is taken by exactly one side rather than discarded; called from
`syncArmedState()` after the timer is stopped and the focus cleared.

Commit: `d6fdb3dfe`
Test: `MidiLearnGuiTest::disarmingDropsAPendingRequest`
Red/green: `cmake --build build --target zene MidiLearnGuiTest` EXIT=0 then
`cd build/tests && ctest -R '^MidiLearnGuiTest$' --output-on-failure` EXIT=8 (red:
`applyPendingBinding()` returned true after the disarm/re-arm cycle); green: build EXIT=0,
`ctest -R '^(MidiLearnTest|MidiLearnThreadTest|ControllerSurfaceTest|ControlSuccessPathsTest|AccessibilityHelperTest|MidiLearnGuiTest)$'`
EXIT=0 (6/6, including the pre-existing `MenuTickIsReconciledWithoutReopeningTheMenu`).

### Cluster 5162 — Renaming a bus removes its return-channel tooltip

Verdict: ALREADY-FIXED

Evidence: the constructor's bus tooltip (`src/gui/MixerChannelView.cpp:131-136`) and the one
`renameFinished()` sets (`:438`) are both superseded on the next sync pass: `syncWithMixer()` calls
`describeStrip()` for every channel (`src/gui/MixerView.cpp:358`), and `describeStrip()` re-sets the
tooltip from the channel name and re-appends `"\nReturn - sends into it are pre-fader"` for a bus
(`src/gui/MixerStripInfo.cpp:256-266`). The mixer syncs every 500 ms (`:226-228`), so a renamed bus
keeps reading as a return. No product change was needed; the case below is a guard.

Commit: `9e3c1884b` (test only)
Test: `MixerSendsOnFadersTest::renamingABusKeepsItsReturnTooltip`
Red/green: no red — the case passes on unmodified product code.
`cmake --build build --target zene MixerSendsOnFadersTest` EXIT=0;
`cd build/tests && ctest -R '^MixerSendsOnFadersTest$' --output-on-failure` EXIT=0 (8/8).

### Cluster 4285 — Clearing a channel name leaves the strip label blank

Verdict: FALSE-POSITIVE

Evidence: `src/gui/MixerChannelView.cpp:426-440` — `renameFinished()` accepts a nonempty changed
name with the guarded rename command at `:430-435` and then runs
`m_renameLineEdit->setText(elideName(mc->m_name))` unconditionally at `:436`. An emptied edit is
therefore restored from the channel's unchanged `m_name` (and elided), so the strip label never
stays blank; only the tooltip the strip carries is stale, which is the 5162 finding and is also
already covered.

Commit: none
Test: none
Red/green: none

### Cluster 4284 — Collapsing a nested folder does not hide its channels

Verdict: ALREADY-FIXED

Evidence: `foldedChannels()` (`src/gui/MixerOverlays.cpp:137-147`) iterates
`Engine::getSong()->tracks()`, but membership in a folder is not a container-level nesting: a
folder's children are derived by walking the container for `Track::parentFolder()`
(`include/TrackFolder.h:54-56`, `children()` at `:138`), so the container's own `tracks()` list
(`include/TrackContainer.h:57`, `:82`) is FLAT and a folder nested in a folder is still in it.
`foldedChannels()` therefore does visit the nested folder, and `childChannels()` (`:65-71`) takes
its whole subtree through `subtreeChannels()` (`:50-61`). `TrackFolder::canHold`
(`src/tracks/TrackFolder.cpp:209-230`) refuses only self-holding, cross-container children and
cycles, so nested folders exist and are covered.

Commit: `f8594ad62` (test only)
Test: `MixerSendsOnFadersTest::collapsingANestedFolderHidesItsChannels`
Red/green: no red — the case passes on unmodified product code.
`cmake --build build --target zene MixerSendsOnFadersTest` EXIT=0;
`cd build/tests && ctest -R '^MixerSendsOnFadersTest$' --output-on-failure` EXIT=0 (9/9).

### Cluster 1503 — Mixer channel filters are not applied to strip visibility

Verdict: FALSE-POSITIVE

Evidence: `mixerviewoptions::shows(int)` exists at `src/gui/MixerViewOptions.cpp:249-261` and the
supplied view sync path does call it: `mixeroverlays::apply()` sets
`view->setVisible(!folded.contains(i) && mixerviewoptions::shows(i))` at
`src/gui/MixerOverlays.cpp:165`, and `syncWithMixer()` calls `apply()` at
`src/gui/MixerView.cpp:346`. The filter is applied to strip visibility on every sync pass, which the
mixer's 500 ms timer drives (`:226-228`).

Commit: none
Test: none
Red/green: none

Leftovers: none — every finding has a verdict; the two ALREADY-FIXED ones carry a green regression
guard (`9e3c1884b`, `f8594ad62`) and the two FALSE-POSITIVE ones rest on source-line evidence only.
