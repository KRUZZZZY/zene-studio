IMPLEMENTED

# Q050-066.1 - Drum-rack pad workflow, stage 1/4: sampler-side pad state and commands

Branch `feat/050-Q050-066.1-oc`, cut from `050/m0-train-19` (`5c1c6715d`). Feature record (what was built, the three
`track.drum_rack` defects found and fixed on the way, commands, format, known limits): `docs/050-features/Q050-066.1.md`.
Logs: run folder `~/.local/share/zene-bunny-fix/qwen/runs/Q050-066.1.r1/logs/` (file names in brackets).

**Change.** A shared pad-state contract (`DrumPadState`, `include/DrumPadState.h`, `src/core/DrumPadState.cpp`) read
from the tracks, sampler and channel the project already saves; three registered commands in
`src/core/ControlCommandsDrumRack.cpp` - `track.drum_rack_get_state` (not_mutating), `track.drum_rack_pad_add` and
`track.drum_rack_pad_set` (true_inverse, one undo step each); the GUI path Folder > Drum Rack Pads... ->
`DrumRackPanel` (`src/gui/DrumRackPanel.cpp`, `src/gui/TrackFolderMenu.cpp`) runs the same commands. No project-format
change. `track.drum_rack` now makes a pad's channel before its track (pad 1 played into the bus), loads the sample
before setting the name (the given name was lost), builds with the journal muted and redoes under the same ids.

## Acceptance

All commands from the worktree with the brief's environment plus `CCACHE_TEMPDIR=/tmp/ccache-tmp-q066` (see Self-check).
"Reverted" = `git diff 5c1c6715d HEAD -- <the ten product files>` applied in reverse to the working tree (tests and
test CMake unchanged), `zene` and the tests rebuilt, run; restored with `git checkout HEAD -- <files>` and rebuilt.

| # | Criterion | Test (command) | Reverted | With the change |
| --- | --- | --- | --- | --- |
| 1 | Create sample pads with name and trigger mapping through registered agent commands; read back the same state | `ctest -R '^DrumRackPadsTest$'` (`aRackReadsBackItsPads`, `aPadIsAddedAsOneUndoStep`, `aPadIsEditedAsOneUndoStep`); `ctest -R '^ControlDrumRackPads$'` (socket: pad_add with name + key and pad_set answers equal `track.drum_rack_get_state`) | DrumRackPadsTest EXIT=8, 5 failures [test-revert-1]; ControlDrumRackPads EXIT=8, "the old-style rack reads back its two pads...", "track.drum_rack_pad_add makes a named pad...", "the added pad's answer is what get_state reads" FAILED [test-revert-5] | EXIT=0 [test-13]; 17/17 socket checks ok [test-9, test-14] |
| 1 (GUI) | The same from the interface, same commands | `ctest -R '^DrumRackPanelTest$'`: triggers the folder menu's "Drum Rack Pads..." item, clicks Add Pad..., types a name + Return, a key + Return, clicks Sample...; reads each result through `track.drum_rack_get_state`; an agent edit shows in the panel | build of DrumRackPanelTest EXIT=2 (`DrumRackPanel.h: No such file`) [build-revert-3]; with the stale binary moved aside ctest EXIT=8 Not Run [test-revert-4] | EXIT=0 [test-6, test-13] |
| 2 | Pad state survives save/reopen; edits undo and redo; old projects still load and render unchanged | `DrumRackPadsTest::thePadStateSurvivesASaveAndReopen` (save, edit, reopen: state equals the saved one), `aPadAddedToARedoneRackLandsInIt`, the undo/redo halves of the add/edit cases; `DrumRackPanelTest` (five undos walk back the five panel/agent edits one at a time); `ControlDrumRackPads`: old-style rack (built with `track.drum_rack` alone) saves, reopens with 0 load errors, reads back the same and renders equal (0.000 LSB, floor 0.000); one `control.undo` reverts name+key together, `control.redo` reapplies; the edited rack reopens field for field and renders equal; the saved file has no element the old one lacked; negative control (edited vs old render) fails the comparison at 56585 LSB | as row 1 (both EXIT=8) [test-revert-1, test-revert-5] | EXIT=0 [test-13, test-9] |

Neighbours with the change: `DeviceCopyTest` (the existing drum-rack cases included) EXIT=0 [test-5];
`TrackFolderMenuTest` EXIT=0 [test-6]; `ReversibilityContractTest` + `ControlCommandsSnapshot` EXIT=0 after the A16
update [test-11, test-10 snapshot half]; `agent_surface` + `ControlReversibilityTranscript` EXIT=0 [test-12]. The full
suite was not run (the brief: targeted tests only).

Gates: `feature_check.py` could not run as installed - it creates `<worktree>/../zene-feat-q050-066.1-feature-check`,
outside this lane's write area (PermissionError [feature-check-1.log]). An unmodified copy with only `ROOT` and the log
directory changed (`/tmp/q066/feature_check_tmplogs.py`, diff shown in the log) ran every gate: fork-sources,
all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures PASS; complexity and
file-length "no new item in your files"; RESULT PASS, EXIT=0 [feature-check-2.log]. The same gates run one by one
also exit 0 [gate-*-1.log]. `submit_check.py`'s "gates" row will show FAIL for the same PermissionError.

## Self-check

- **Each new test fails with the product change reverted, passes restored.** Reverse-applied the ten product files
  (`src/core/ControlCommandsDrumRack.cpp`, `include/DrumPadState.h`, `src/core/DrumPadState.cpp`,
  `src/core/CMakeLists.txt`, `src/core/ControlReversibilityTableDevices.cpp`, `include/DrumRackPanel.h`,
  `src/gui/DrumRackPanel.cpp`, `include/TrackFolderMenu.h`, `src/gui/TrackFolderMenu.cpp`, `src/gui/CMakeLists.txt`),
  rebuilt `zene` EXIT=0 [build-revert-1, build-revert-4]: `DrumRackPadsTest` EXIT=8 [test-revert-1];
  `DrumRackPanelTest` does not compile, EXIT=2 [build-revert-3] - the first ctest run passed only because the earlier
  binary was still there, so it was moved aside and ctest reported Not Run, EXIT=8 [test-revert-2, test-revert-4];
  `ControlDrumRackPads` EXIT=8 [test-revert-3 first version, test-revert-5 committed version]. Restored with
  `git checkout HEAD -- <files>`, rebuilt EXIT=0 [build-restore-1, build-restore-2]: all three EXIT=0 [test-13, test-14].
- **Every acceptance criterion is proven by a test I ran.** Table above, one row per criterion with its exit codes.
- **One undo step; honest reversibility classes.** `track.drum_rack_pad_add`: one `addUndoStep` action (undo removes
  the track and channel it made; redo rebuilds under the same ids) - `true_inverse`. `track.drum_rack_pad_set`: one
  Track checkpoint taken before the writes, and the key write uses `setValue(..., journal=false)` - `true_inverse`.
  `track.drum_rack_get_state`: reads only - `not_mutating`. `track.drum_rack` keeps `true_inverse`; its build now runs
  with the journal muted, so no model write inside it pushes a second step. Proven by the single-undo checks in
  `DrumRackPadsTest`, `DrumRackPanelTest` and `ControlDrumRackPads`; table rows in
  `src/core/ControlReversibilityTableDevices.cpp`; `ReversibilityContractTest` EXIT=0 with the A16 histogram moved to
  `rows=880 true_inverse=434 snapshot=42 irreversible=25 not_mutating=379`.
- **Realtime.** Nothing added runs on the audio thread. New functions: `readDrumPad`/`drumPadsOf`/`drumRackState`
  (UI thread; they serialise the sampler's settings into a QDomDocument there), `buildPad`/`rebuildPad`/`unjournalled`/
  `padAdd`/`padSet`/`rackState` (UI-thread command handlers and undo closures), `DrumRackPanel` (GUI). The sampler file
  swap is `Instrument::loadFile`, the path the sampler's own view, `plugin.open_file` and `record.sampler_capture`
  already use. No playback code changed. TSan does not run on this box; this is by reading.
- **Old projects load and render as before; new data round-trips.** No format change: a pad is the instrument track,
  sampler, channel and send the format already had. `ControlDrumRackPads` builds a rack with the pre-existing command
  only, saves, reopens (0 load errors), reads it back unchanged and renders it equal (0.000 LSB); the edited rack
  round-trips field for field and in sound; the saved file adds no element. Not separately run: opening this build's
  file in the base build. Behaviour change for NEW racks only: `track.drum_rack`'s first pad now plays into its own
  channel (it played into the bus), and a given pad name is kept. A rack saved by the base build keeps the channel
  assignment its file holds.
- **Portability.** No Qt-6-only API (QJson*, QDom*, QLineEdit/QSpinBox/QToolButton, `QLatin1String` comparison - all
  Qt 5.15); no `min`/`max`/`near`/`far`/`small`/`interface` identifiers (`std::min` only as a call); no windows.h macro
  names; no `std::filesystem`; no POSIX-only calls. Not compiled here on Qt 5.15, MSVC or macOS.
- **Environment notes.** ccache's configured temp directory under `zene-bunny-fix/dsh/home` is not writable from this
  lane (build-1 EXIT=2, `ccache: error: failed to create temporary file`), so every build after it set
  `CCACHE_TEMPDIR=/tmp/ccache-tmp-q066`. New `tests/*.py` transcript not listed in `tests/fork-sources.txt`, like its
  recent neighbours (e.g. `tests/control-graph-waveform-load.py`).
