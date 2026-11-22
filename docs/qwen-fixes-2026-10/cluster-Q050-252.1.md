IMPLEMENTED

# Q050-252.1 External Instrument device - stage 1/3: External MIDI device controls and preset state

Branch `feat/050-Q050-252.1-oc` on `050/m0-train-24` (`2a7fd9f9c`). The full account (design, format, limits) is
`docs/050-features/Q050-252.1.md`; this is the evidence.

## Acceptance

Build: `cmake --build build --target zene ExternalMidiDeviceTest ExternalDeviceCardTest -- -j4` (build-6.log) EXIT=0.
Tests: `ctest -R "ExternalMidiDeviceTest|ExternalDeviceCardTest" --output-on-failure` from `build/tests`, EXIT=0 (test-4.log):
`ExternalMidiDeviceTest` 11/11, `ExternalDeviceCardTest` 6/6.

| Criterion | Test (all in the two files above) | Exit |
| --- | --- | --- |
| Command and card configure and report port/channel and bank/program; bank/program changes reach the selected MIDI output | `ExternalMidiDeviceTest::theCommandConfiguresTheDeviceAndThePatchReachesTheOutput` (output handed `cc0=2@5 cc32=3@5 pc17@5` once, in order, on channel 5, to the subscribed port), `aPortOrChannelChangeSendsNoPatchAndOutputIsOffWhenAsked`, `aRefusedCallChangesNothing`; `ExternalDeviceCardTest::theControlsConfigureTheDevice`, `theTrackMenuOpensTheCard` (card driven by combo choice, spin values; menu entry declares `device.external_set` and opens the card) | 0 |
| A device preset saves and recalls its MIDI settings | `ExternalMidiDeviceTest::aPresetSavesAndRecallsItsMidiSettings`, `aPresetWhosePortIsGoneAppliesTheRestAndSaysSo`; `ExternalDeviceCardTest::thePresetButtonsSaveRecallAndDelete` (Save / Recall / Delete clicked) | 0 |
| Project save/load restore the settings | `ExternalMidiDeviceTest::theDeviceSavesWithTheProject` (real `project.save` / `project.open`; a never-used track saves no new attribute; a pre-feature `<midiport>` opens with no bank/presets), `aDamagedPresetListIsBounded` | 0 |
| One `control.undo` restores the prior settings | `ExternalMidiDeviceTest::oneUndoRestoresTheDevice` (5-field set = `control.undo_depth` +1, one undo restores everything, the patch that returns is sent once; preset save/recall/remove one step each; redo), `ExternalDeviceCardTest::eachEditIsOneUndoStepAndTheCardFollowsIt` | 0 |

New commands (group `device`, no new group): `device.external_get_state` (not_mutating), `device.external_set`,
`device.external_preset_save`, `device.external_preset_recall`, `device.external_preset_remove` (true_inverse, Track checkpoint).
Snapshot regenerated live: 883 ids / 77 groups (snapshot-1.log EXIT=0); `python3 tests/doc-figures-gate.py` EXIT=0;
`ReversibilityContractTest` passes after the A16 histogram line moved to `rows=892 true_inverse=438 snapshot=42 irreversible=25 not_mutating=387`.
`ControlCommandsSnapshot` passes.

Also run, all pass (test-7.log, EXIT=0): `ControlRegistryTest`, `ControlSurfaceReferenceTest`, `AgentSurfaceReportTest`,
`ReversibilityContractTest`, `UndoBoundsTest`, `MidiReconnectTest`, `MixerToolsTest`, `ShellTest`.

## Self-check

- **Each new test FAILS with the product change reverted, then PASSES restored.** Broken in place (not removed - the tests do not
  compile without the new API): no bank select sent (`sendOutputPatch`), `MidiPort::loadSettings` unable to unsubscribe, device
  presets not saved, and the card's spin boxes not running the command. Rebuilt and run (build-neg.log, test-neg.log): ctest
  EXIT=8; `ExternalMidiDeviceTest` 4 failed of 11 (`theCommandConfiguresTheDeviceAndThePatchReachesTheOutput`,
  `aPresetSavesAndRecallsItsMidiSettings`, `oneUndoRestoresTheDevice`, `theDeviceSavesWithTheProject`), `ExternalDeviceCardTest`
  3 failed of 6 (`theControlsConfigureTheDevice`, `eachEditIsOneUndoStepAndTheCardFollowsIt`, `thePresetButtonsSaveRecallAndDelete`).
  The files were restored byte for byte, rebuilt (build-8.log EXIT=0) and the same tests pass, ctest EXIT=0 (test-7.log).
- **Every acceptance criterion proven by a test I ran:** see the table; every row exit 0.
- **One undo step per edit; honest class:** `oneUndoRestoresTheDevice` reads `control.undo_depth` before and after a 5-field
  `device.external_set` (+1); `aRefusedCallChangesNothing` shows a refusal takes no step. Classes: get_state `not_mutating`;
  the four writers `true_inverse` by the Track journal checkpoint (the whole `<midiport>`, presets included) - the test proves the
  inverse with a real `control.undo`. Hardware is told the restored patch again, so the gear follows the undo; the sent MIDI itself
  is not "undone" and the row text says so.
- **Audio thread:** nothing added there. `MidiPort::processOutEvent` and the note path are untouched; the new code
  (`sendOutputPatch`, `setOutputPatch`, preset list, the five commands, the card) runs on the main thread, the same thread the
  PROG box already sent its program change from. No allocation/lock/wait/I-O/signal was added to `InstrumentTrack::play` or the
  period callback. Local TSan does not run on this box: the reasoning is that the new state (bank models, preset vector,
  `m_patchBatch`) has only main-thread readers and writers.
- **Old projects / round trip:** bank attributes and presets are written only when set, so an unchanged track saves the same
  bytes (`theDeviceSavesWithTheProject` asserts no `outputbank` / `devicepreset` for a plain track); a `<midiport>` without them
  loads as bank -1, no presets (same test); new data round-trips through `project.save` / `project.open`. I did not render a
  project before/after: no audio path changed, so a render comparison was not run.
- **Portability:** Qt API used is in 5.15 (`QOverload<int>::of`, `QSignalBlocker`, `QDomElement::firstChildElement`,
  `QComboBox::currentIndexChanged(int)`), no Qt-6-only call, no `min`/`max`/`near`/`far`/`small`/`interface` identifiers, no
  `std::filesystem`, no POSIX-only call. Built here only with Qt 6.12/GCC; Qt 5.15, MSVC and macOS were not built.
- **Gates:** `feature_check.py` could not create its log directory (`<worktree>-feature-check`, outside the write jail:
  PermissionError), so `submit_check`'s "gates" row fails in this session for that reason only. I ran a copy of the same script
  with only the log directory and FIX-BASE path pointed inside the run folder (`/tmp/fc-q252.py`, unchanged logic): all eight rows
  PASS (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length;
  fc-5.log). The first run flagged `setDevice` (CCN 28 -> 15 -> 11) and I split it until "no new item in your files".
- **Environment notes:** ccache's temp dir in the supplied environment was outside the jail, so builds ran with
  `CCACHE_TEMPDIR=/tmp/ccache-tmp-q252`; there is no ALSA sequencer on this box (`/dev/snd/seq` absent), so the tests use a
  recording `MidiClient` through the new `MidiPort::rebindClientForTest`, and no real MIDI loopback was measured.
- **A change to inherited code beyond the feature:** `MidiPort::loadSettings` now unsubscribes a writable port the restored
  state does not list (it could only subscribe, so undo could not leave a port - this also affected `track.midi_out`). Declared in
  `tests/upstream-modifications.txt`, held by `oneUndoRestoresTheDevice`. The input side has the same latent pattern and was left alone.
