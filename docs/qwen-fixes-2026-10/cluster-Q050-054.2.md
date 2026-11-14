IMPLEMENTED

# Q050-054.2 - MIDI take-lane comp UI

Feature record: `docs/050-features/Q050-054.2.md`. Logs: `/tmp/q0542-logs`.

## Acceptance

| Criterion | Test | Command | Exit code |
| --- | --- | --- | --- |
| GUI MIDI lane selection gates playback by note start and survives save/load; strip draws notes | `MidiCompLaneGuiTest` | `ctest -R '^MidiCompLaneGuiTest$' --output-on-failure` | 0 (1 test passed) |
| Registered workflow schemas and undo coverage; comp edits undo and redo | `MidiCompLaneGuiTest`, `TakeLaneCompTest`, `TakeLaneCompPlaybackTest`, `ReversibilityContractTest`, `ControlCommandsSnapshot`; stage-1 `MidiTakeRecordTest` for capture | GUI command above; `ctest -R '^(TakeLanesPanelTest|TakeLaneCompTest|TakeLaneCompPlaybackTest)$' --output-on-failure`; `ctest -R '^(ReversibilityContractTest|ControlCommandsSnapshot)$' --output-on-failure` | 0 / 0 / 0 (1 / 3 / 2 tests passed); capture is stage-1 proof |

## RED/GREEN

| State | Test | Exit code | Evidence |
| --- | --- | --- | --- |
| RED: product hunks reversed | `MidiCompLaneGuiTest` | 8 | Both GUI cases fail because the menu action is absent or disabled; `/tmp/q0542-logs/005-test-red.log` |
| RED-2: MIDI note drawing removed | `MidiCompLaneGuiTest` | 8 | Note drawing case fails its ink assertion; `/tmp/q0542-logs/007-test-red2.log` |
| GREEN: restored | `MidiCompLaneGuiTest` | 0 | 1 test passed; `/tmp/q0542-logs/010-test-green.log` |
| GREEN: neighbours | `TakeLanesPanelTest`, `TakeLaneCompTest`, `TakeLaneCompPlaybackTest` | 0 | 3 tests passed; `/tmp/q0542-logs/011-test-neighbours.log` |
| GREEN: contracts | `ReversibilityContractTest`, `ControlCommandsSnapshot` | 0 | 2 tests passed; `/tmp/q0542-logs/012-test-contract.log` |

## Self-check

- (a) RED exits 8 and RED-2 exits 8; restored GREEN exits 0. The GREEN build and feature checks also exit 0 (`/tmp/q0542-logs/008-build-green.log`, `/tmp/q0542-logs/009-gates-green.log`). The build guard was bypassed with `ZENE_BUILD_GUARDED=1` and ccache disabled because default lock, temp and log directories were not writable in the sandbox; `-j4` was retained.
- (b) GUI selection, note-start playback, save/load and drawing -> `MidiCompLaneGuiTest`, exit 0. Schema and undo coverage -> that test, neighbour suite and contract suite, exits 0 / 0 / 0; capture is covered by the stage-1 `MidiTakeRecordTest` proof.
- (c) Each strip drag asserts one `comp.select` and undo depth +1; `control.undo` and `control.redo` are asserted. No command is new or changed: existing `comp.get_state` is `not_mutating`, `comp.assign` and `comp.select` are `true_inverse`; stage-1 `record.midi_capture_commit` is `true_inverse`.
- (d) The diff touches no audio-path function. The `TrackOperationsWidget` menu and `CompSwipeStrip` paint run on the GUI thread; playback gating is unchanged stage-1 `InstrumentTrack::takePlays` / `compPlays`.
- (e) No format change: existing lane/takegroup attributes and `<z:lanes>` are used. The test saves and reopens with `project.save` / `project.open`, comparing segments, lanes and playback.
- (f) User path: track menu > Take lanes > drag in the strip. Agent path: `comp.select` / `comp.get_state`. Invalid lane 5 and begin > end are refused without an undo step. Boundary notes at 168, 240 and 300 are checked: playback from 168 gives 62, from 240 includes 65 and not 64, and 300 does not play 67; playback from 0 gives 60. Undo/redo and reopening after save are checked, including reopening the panel on the reloaded track with its new id.
- (g) The GUI test triggers the real menu action and mouse-drags the strip, checking undo depth +1.
- (h) Only Qt 5/6-common API is used; no `windows.h` names or `std::filesystem`.
- WARNs: no command file changed because the existing registered comp commands cover the action; the manual is updated.
