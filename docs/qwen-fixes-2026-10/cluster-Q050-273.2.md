IMPLEMENTED

Q050-273.2 stage 2 - save and reuse MIDI launcher patterns in the shared library. Feature record:
`docs/050-features/Q050-273.2.md`. Built on the stage-1 audio path (Q050-273.1); the round-3 session left no
commit, so its saved patch (truncated at the last hunk; one hunk re-applied by hand) and helper files were
restored and then finished here: the missing A16 row, the failure/exhaustion and realtime cases, the gates.

## Acceptance

| Criterion | Test (ran myself) | Result |
| --- | --- | --- |
| A populated MIDI cell offers Save to Library; the entry holds all notes of its pattern | `SessionLibraryMidiTest::aMidiCellSavesEveryPatternNoteFromItsMenu` (clicks the menu entry and the form's Run; notes across two store instruments) | ctest exit 0 (`test-final.log`) |
| After reopening, the entry populates another cell as a playable pattern, notes intact | `SessionLibraryMidiTest::theSavedEntryIsReusedInAnotherCellAfterReopening` (browser drag payload onto a cell); `ControlSessionLibraryMidi` | ctest exit 0 |
| The agent path saves and reads back without a window | `tests/control-session-library-midi.py`, ctest `ControlSessionLibraryMidi` | ctest exit 0 |
| Refusals / exhaustion leave nothing behind | `ControlSessionLibraryMidi` refusal matrix; `aSaveThatCannotBeWrittenFailsWithoutAFile` (SMF delta and tick exhaustion, with a one-note control) | ctest exit 0 |
| Realtime | `savingAndImportingWhileAMidiCellPlaysLeavesTheAudioPathAlone` | ctest exit 0 |

Red/green (behaviour hunks of `ControlCommandsLibraryClips.cpp`, `ControlCommandsSessionPatterns.cpp`,
`ControlReversibilityTableAction.cpp`, `SessionGridDrop.cpp`, `SessionGridMenus.cpp`, `SessionGridView.h`
reversed, rebuilt): `SessionLibraryMidiTest` 2 passed / 4 failed, ctest exit 8 (`test-5-reverted-gui.log`);
`ControlSessionLibraryMidi` ctest exit 8 (`test-4-reverted-agent.log`). Restored and rebuilt: both exit 0
(`test-final.log`).

Also run green (exit 0, `test-final.log`): `SessionLibrarySaveTest`, `ControlSessionLibrarySave`,
`LibraryBrowserTest`, `SessionGridViewTest`, `ReversibilityContractTest`, `ControlCommandsSnapshot`; earlier
(`test-7.log`): `SessionGridRecordTest`, `SessionCellPlaybackMenuTest`.

Stage-1's `SessionLibrarySaveTest` asserted that a MIDI cell offered no Save to Library entry and that the
command named MIDI in its refusal; both assertions now say the opposite on purpose (the feature reverses them),
and the refusal assertion became "a MIDI pattern with no notes". No check was removed.

## Gates

- `python3 tests/doc-figures-gate.py` exit 0 (936 ids / 78 groups); snapshot regenerated from a live instance with
  `tools/mcp-zene-control/snapshot_commands.py --socket` (the `snapshot_live.py` named in the brief does not exist
  on this box); A16 histogram line moved to `rows=945 true_inverse=466` (`ReversibilityContractTest` passes).
- `tests/fork-sources-gate.sh`, `all-sources-reproduce.sh`, `no-upstream-regression-gate.sh`,
  `no-tautology-gate.sh`, `tests/scripted/check-namespace`: exit 0. `feature_check.py`: RESULT PASS.
- Screenshot of the launcher grid with the MIDI cell: `logs/midi-cell.png`.

## Not run here

Qt 5.15, MSVC, mingw and macOS builds (this box builds Qt 6 only). The new code uses no Qt-6-only API, no
`std::filesystem`, and no `small/near/far/interface/min/max` identifiers. TSan does not run on this box; the
design has no shared state with the audio thread (command handlers on the control/UI thread only).

## Self-check

1. **Red then green.** Behaviour hunks of `ControlCommandsLibraryClips.cpp`, `ControlCommandsSessionPatterns.cpp`,
   `ControlReversibilityTableAction.cpp`, `SessionGridDrop.cpp`, `SessionGridMenus.cpp`, `SessionGridView.h` reversed
   and rebuilt: `ctest -R SessionLibraryMidiTest` exit 8 (2 passed / 4 failed, `test-5-reverted-gui.log`),
   `ctest -R ControlSessionLibraryMidi` exit 8 (`test-4-reverted-agent.log`). Restored and rebuilt: both exit 0.
   Re-run at the end of this session on the committed tree (`build-11.log` build exit 0, `test-r4-final2.log`
   ctest exit 0): `SessionLibraryMidiTest`, `SessionLibrarySaveTest`, `ControlSessionLibraryMidi`,
   `ReversibilityContractTest` all pass.
2. **Criteria and tests.** See the Acceptance table: menu save -> `aMidiCellSavesEveryPatternNoteFromItsMenu`
   (0); reopen and reuse -> `theSavedEntryIsReusedInAnotherCellAfterReopening` and `ControlSessionLibraryMidi`
   (0); agent path -> `tests/control-session-library-midi.py` (0).
3. **Undo / reversibility.** `browser.save_clip` (true_inverse: undo deletes the file and tag, redo rewrites it)
   and `session.import_midi_cell` (true_inverse: undo restores the captured `<session>` block and removes the
   pattern track). Each is one undo step; both tests assert `control.undo` once per edit, and the agent test
   reads `control.transactions`. `ReversibilityContractTest` passes with the new A16 row (`rows=945
   true_inverse=466`).
4. **Audio thread.** No audio-path function is touched; the diff adds only command handlers (control/UI thread),
   a GUI menu/drop, and the SMF writer/reader helpers, which run on the calling thread. No lock, signal or queue
   is shared with the render thread; the pattern is read through the store the handler already owns on the UI
   thread. `savingAndImportingWhileAMidiCellPlaysLeavesTheAudioPathAlone` shows rendering continues and the audio
   path's allocation count does not grow over 200 periods. TSan does not run here; this is reasoning, not a run.
5. **Projects.** No format change: the pattern and cell reference use existing `PatternStore` / `<z:scenes>`
   serialisation. The reopen test (`project.save`, `project.new`, `project.open`) shows the new pattern
   round-trips and imports from the saved entry; old projects are untouched because no loader code changed.
6. **Completeness / edge cases tried.** Empty cell, pattern with no notes, notes across two store instruments,
   missing/relative/directory/empty/damaged/over-16-MiB/no-notes/past-song-length files, bad instrument and bad
   address, SMF delta and tick exhaustion (with a one-note control that saves), duplicate save, undo/redo of save
   and import, reopen after save, import into a different cell, and re-saving the imported cell (identical notes).
7. **GUI drives the visible control.** `SessionLibraryMidiTest` triggers the cell menu's Save to Library action,
   clicks the form's Run button, and delivers a real `QDropEvent` carrying the browser's drag payload onto a cell;
   it then checks that `control.undo` reverses exactly one step per edit, which only holds when the registered
   command ran.
8. **Portability.** No Qt-6-only API, no `std::filesystem`, no `small/near/far/interface/min/max` identifiers in
   the new files; the namespace is closed the repo's way (`a33504e19a`). Qt 5.15 / MSVC / macOS builds were not
   run on this box.
